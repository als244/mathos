#include "master.h"

// Internal data structure

typedef struct worker_connection {
	Master * master;
	// The 2 fields below are dependent & populated based on return of accept()
	struct sockaddr_in worker_sockaddr;
	int sockfd;
} Worker_Connection;



uint64_t node_ip_config_hash_func(void * node_ip_config, uint64_t table_size){
	// here ip_addr is the network-byte order of ip addr equivalent to inet_addr(char *)
	uint32_t key = ((Node_Ip_Config *) node_ip_config) -> node_id;
	// Take from "https://gist.github.com/badboy/6267743"
	// Credit: Robert Jenkins
	key = (key+0x7ed55d16) + (key<<12);
   	key = (key^0xc761c23c) ^ (key>>19);
   	key = (key+0x165667b1) + (key<<5);
   	key = (key+0xd3a2646c) ^ (key<<9);
   	key = (key+0xfd7046c5) + (key<<3);
   	key = (key^0xb55a4f09) ^ (key>>16);
	return (uint64_t) key % table_size;

}

int node_ip_config_cmp(void * node_ip_config, void * other_node_ip_config){
	uint32_t id_a = ((Node_Ip_Config *) node_ip_config) -> node_id;
	uint32_t id_b = ((Node_Ip_Config *) other_node_ip_config) -> node_id;
	return id_a - id_b;
}


Master * init_master(char * ip_addr, uint32_t max_nodes) {

	int ret;

	Master * master = (Master *) malloc(sizeof(Master));
	if (master == NULL){
		fprintf(stderr, "Error: malloc failed to allocate master server\n");
		return NULL;
	}

	master -> ip_addr = ip_addr;
	master -> max_nodes = max_nodes;

	ret = pthread_mutex_init(&(master -> id_to_assign_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not initialize id_to_assign_lock\n");
		return NULL;
	}

	master -> id_to_assign = 0;

	ret = sem_init(&(master -> avail_node_cnt_sem), 0, max_nodes);
	if (ret != 0){
		fprintf(stderr, "Error: could not initialize avail_node_cnt_sem\n");
		return NULL;
	}


	// SETTING DEFAULT TABLE PARAMS HERE...
	// should really change location / args to config this better
	// MIGHT WANT TO HAVE THE TABLE BE FIXED SIZE SO THESE FACTORS DON'T MATTER
	// Keeping it this way for now, for flexiblity
	float load_factor = 0.5f;
	float shrink_factor = 0.1f;

	// setting min_nodes == max_nodes
	uint32_t min_nodes = max_nodes;

	Hash_Func hash_func_node_ip_config = &node_ip_config_hash_func;
	Item_Cmp item_cmp_node_ip_config = &node_ip_config_cmp;
	Table * node_configs = init_table(min_nodes, max_nodes, load_factor, shrink_factor, hash_func_node_ip_config, item_cmp_node_ip_config);
	if (node_configs == NULL){
		fprintf(stderr, "Error: could not initialize master node_ip_config table\n");
		return NULL;
	}

	master -> node_configs = node_configs;

	return master;
}

int process_join_net_request(Worker_Connection * worker_connection){

	int ret;
	ssize_t byte_cnt;

	// Retriever the pointer to master
	Master * master = worker_connection -> master;

	// Retrieve the socket that will be used for sending the previously joined node configs
	// This was the sockfd returned from accept()
	int sockfd = worker_connection -> sockfd;

	
	// 1.) Determine ID to assign node

	// master -> id_to_assign is incremented upon each previous successful addition
	// All Joins are Entirely Serialized
	//	- want to ensure id assignment has no gaps and thus requires waiting for successful confirmation
	//		that the joining node received the configuration information
	pthread_mutex_lock(&(master -> id_to_assign_lock));

	uint32_t id_to_assign = master -> id_to_assign;
	

	// 2.) Create configuration for node
	Node_Ip_Config * node_ip_config = (Node_Ip_Config *) malloc(sizeof(Node_Ip_Config));
	if (node_ip_config == NULL){
		fprintf(stderr, "Error: malloc failed allocating node_ip_config\n");
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		close(sockfd);
		return -1;
	}

	node_ip_config -> node_id = id_to_assign;

	// Retrieve the worker node's ip address that was part of accept()
	struct sockaddr_in worker_sockaddr = worker_connection -> worker_sockaddr;
	uint32_t worker_s_addr = (uint32_t) (worker_sockaddr.sin_addr.s_addr);
	node_ip_config -> s_addr = worker_s_addr;

	// NOTE: For all send/recv, return 0 and not -1
	//	- The error may have occurred with that specific connection
	//		- The overall join_net_server could be healthy and shoudn't exit 
	//			- If return val != 0 the main run_join_net_server thread terminates

	// 3.) Send the Assigned ID and Network's Max Nodes values to the worker

	// sending the id that that the master assigned to the joining node
	byte_cnt = send(sockfd, &id_to_assign, sizeof(uint32_t), 0);
	if (byte_cnt != sizeof(uint32_t)){
		fprintf(stderr, "Error: Bad sending of id_to_assign. Only sent %zd bytes out of %zu\n", byte_cnt, sizeof(uint32_t));
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		free(node_ip_config);
		close(sockfd);
		return 0;
	}

	// sending the maximum number of nodes that can join the network
	//	- the worker needs this info to size it's Net_World -> nodes table properly
	byte_cnt = send(sockfd, &(master -> max_nodes), sizeof(uint32_t), 0);
	if (byte_cnt != sizeof(uint32_t)){
		fprintf(stderr, "Error: Bad sending of master -> max_nodes. Only sent %zd bytes out of %zu\n", byte_cnt, sizeof(uint32_t));
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		free(node_ip_config);
		close(sockfd);
		return 0;
	}

	// 4.) Obtain all of the node_ip_config in table

	Node_Ip_Config ** all_node_ip_config;
	uint32_t all_node_ip_config_cnt = (uint32_t) get_all_items_sorted_table(master -> node_configs, &all_node_ip_config);

	// 5.) Sending the configs to worker who will need this to assume client role and connect to these nodes' rdma_init TCP servers!

	// sending the total number of node configs that master has received
	// (that are still active)
	byte_cnt = send(sockfd, &all_node_ip_config_cnt, sizeof(uint32_t), 0);
	if (byte_cnt != sizeof(uint32_t)){
		fprintf(stderr, "Error: Bad sending of all_node_ip_config_cnt. Only sent %zd bytes out of %zu\n", byte_cnt, sizeof(uint32_t));
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		free(node_ip_config);
		close(sockfd);
		return 0;
	}

	// send each item in the all_node_ip_config table
	// the joining node can then assume role as client 
	// and form connection with each of the nodes in this array
	//	- their node_ids must be < than the currently assigned node id by construction
	for (uint32_t i = 0; i < all_node_ip_config_cnt; i++){
		byte_cnt = send(sockfd, *(all_node_ip_config[i]), sizeof(Node_Ip_Config), 0);
		if (byte_cnt != sizeof(Node_Ip_Config)){
			fprintf(stderr, "Error: Bad sending of node_ip_config #%u. Only sent %zd bytes out of %zu\n", i, byte_cnt, sizeof(Node_Ip_Config));
			pthread_mutex_unlock(&(master -> id_to_assign_lock));
			free(node_ip_config);
			close(sockfd);
			return 0;
		}
	} 


	// 6.) Block for confirmation (client sends back ACK)
	bool is_worker_successful;
	byte_cnt = recv(sockfd, &is_worker_successful, sizeof(bool), MSG_WAITALL);
	if (byte_cnt != sizeof(bool)){
		fprintf(stderr, "Error: Didn't confirmation that worker was successful. Errno String: %s", strerror(errno));
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		free(node_ip_config);
		close(sockfd);
		return 0;
	}

	// If there was an error on the worker end, it will send back a false boolean
	// This isn't an "error" according to the master, but it shoudn't add to table/update counters
	if (!is_worker_successful){
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		free(node_ip_config);
		close(sockfd);
		return 0;
	}

	// 7.) Successfully added a node to network, so should:

	// 	a.) Add this node to the table, so future joiners will receive its Node_Ip_Config data
	//	b.) Update counters
	//			- Increment id_to_assign
	//			- Decrement avail_node_cnt_sem (sem_wait)
	//	c.) Release id_to_assign lock
	//	d.) Close connection
	

	// Insert this config into table
	// 		- should probably check for duplicate first...
	ret = insert_item_table(master -> node_configs, node_ip_config);
	// SHOULD NEVER FAIL (by construction of max_nodes table limit + avail_node_count semaphore)
	// 	- IF FAILURE, THEN THE WORKER WILL NOT RECEIVE CONNECTION REQUESTS FROM FUTURE JOINERS
	if (ret != 0){
		fprintf(stderr, "Error: failed to insert node with id: %u\n", id_to_assign);
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		free(node_ip_config);
		close(sockfd);
		return -1;
	}

	// update counters
	master -> id_to_assign = id_to_assign + 1;
	sem_wait(&(master -> avail_node_cnt_sem));
	
	// release lock
	pthread_mutex_unlock(&(master -> id_to_assign_lock));

	// close connection
	close(sockfd);

	return 0;

}


// NOTE: This function should be run in a seperate thread than main program.
void * run_join_net_server(void * _master) {

	int ret;

	// cast the argument passed in by pthread_create
	Master * master = (Master *) _master;

	char * ip_addr = master -> ip_addr;
	uint32_t max_nodes = master -> max_nodes;

	// 1.) create server TCP socket
	int serv_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sockfd != 0){
		fprintf(stderr, "Error: could not create server socket\n");
		return NULL;
	}

	// 2.) Init server info
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;

	// set IP address of server
	ret = inet_aton(ip_addr, &serv_addr.sin_addr);
	if (ret != 0){
		fprintf(stderr, "Error: join server ip address: %s -- invalid\n");
		return NULL;
	}
	// defined within common.h
	serv_addr.sin_port = htons(MASTER_JOIN_PORT);

	// 3.) Bind server to port
	ret = bind(serv_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
	if (ret != 0){
		fprintf(stderr, "Error: could not bind server socket to address: %s, port: %u\n", ip_addr, MASTER_JOIN_PORT);
		return NULL;
	}

	// 4.) Start Listening
	ret = listen(serv_sockfd, max_nodes);
	if (ret != 0){
		fprintf(stderr, "Error: could not start listening on server socket\n");
		return NULL;
	}

	// 5.) Process accepts
	// SHOULD REALLY BE MORE EFFICIENT (multi-threaded / event driver), but fine for now...
	
	// remote_addr and sockfd are dependent upon call to accept() so will be modified then
	Worker_Connection worker_connection;
	worker_connection.master = master;


	int accept_sockfd;
	// client_addr will be read based upon accept() call 
	// use this to obtain ip_addr and can look up
	// node_id based on net_world -> ip_to_node table (created based upon config file)
	struct sockaddr_in remote_sockaddr;
	socklen_t remote_len = sizeof(remote_sockaddr);

	uint32_t cur_node_count;

	while(1){

		// Is blocking until there is an "available node" (means max_nodes - node_cnt)
		// for master to accept new joiner
		ret = sem_wait(&(master -> avail_node_cnt_sem));
		if (ret != 0){
			fprintf(stderr, "Error: sem_wait failed for avail_node_cnt_sem\n");
			return NULL;
		}

		// now we decremented avail_node_count and reserved a spot for a new jointer

		// 1.) accept new connection (blocking)
		accept_sockfd = accept(serv_sockfd, (struct sockaddr *) &remote_sockaddr, &remote_len);
		if (accept_sockfd < 0){
			fprintf(stderr, "Error: could not process accept within master join server\n");
			return NULL;
		}

		// 2.) Modify the connection fields that depend on accpet
		worker_connection.sockfd = accept_sockfd;
		worker_connection.remote_sockaddr = remote_sockaddr;

		// 3.) Actually do data transimission and send/receive RDMA initialization data

		// If initializing a very large cluster, this part should be handled in seperate thread
		// with a thread pool!
		ret = process_join_net_request(&worker_connection);
		if (ret != 0){
			fprintf(stderr, "Error: could not process connection from remote addr: %s\n", inet_ntoa(remote_sockaddr.sin_addr));
			return NULL;
		}
	}

	return NULL;


}


// Responsible for creating all server threads
int run_master(Master * master) {

	int ret;

	// Start all the tcp servers here (or maybe just a single server...??)

	// For now, just join server for testing
	// Should never return...

	pthread_t join_net_server_thread;

	ret = pthread_create(&join_net_server_thread, NULL, run_join_net_server, (void *) master);
	if (ret != 0){
		fprintf(stderr, "Error: pthread_create failed to start join server\n");
		return -1;
	}

	// Should Be Infinitely Blocking 
	// (unless error or shutdown message)
	ret = pthread_join(join_net_server_thread);
	if (ret != 0){
		fprintf(stderr, "Error: pthread_join failed for join server\n");
		return -1;
	}

	return 0;
}