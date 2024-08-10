#include "master.h"

// Internal data structure

typedef struct worker_connection {
	Master * master;
	// The 2 fields below are dependent & populated based on return of accept()
	struct sockaddr_in remote_sockaddr;
	int sockfd;
} Worker_Connection;


uint64_t node_config_hash_func(void * node_config, uint64_t table_size){
	// here ip_addr is the network-byte order of ip addr equivalent to inet_addr(char *)
	uint32_t key = ((Node_Config *) node_config) -> node_id;
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

int node_config_cmp(void * node_config, void * other_node_config){
	uint32_t id_a = ((Node_Config *) node_config) -> node_id;
	uint32_t id_b = ((Node_Config *) other_node_config) -> node_id;
	if (id_a == id_b){
		return 0;
	}
	else if (id_a > id_b){
		return 1;
	}
	else{
		return -1;
	}
}


Master * init_master(char * ip_addr, uint32_t max_nodes, uint32_t min_init_nodes) {

	int ret;

	Master * master = (Master *) malloc(sizeof(Master));
	if (master == NULL){
		fprintf(stderr, "[Master] Error: malloc failed to allocate master server\n");
		return NULL;
	}

	master -> ip_addr = ip_addr;
	master -> max_nodes = max_nodes;
	master -> min_init_nodes = min_init_nodes;

	// FOR NOW: pretty useless. Only have a single thread processing the Join_Net Server
	//	=> if becomes issue for larger net, or init is annoying slow, should think harder about init design
	ret = pthread_mutex_init(&(master -> id_to_assign_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "[Master] Error: could not initialize id_to_assign_lock\n");
		return NULL;
	}

	// NOTE: start assigning at 1 because master id is given 0
	master -> id_to_assign = 1;

	// Ensure that there is room in the table before processing a new join_net request
	ret = sem_init(&(master -> avail_node_cnt_sem), 0, max_nodes);
	if (ret != 0){
		fprintf(stderr, "[Master] Error: could not initialize avail_node_cnt_sem\n");
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

	Hash_Func hash_func_node_config = &node_config_hash_func;
	Item_Cmp item_cmp_node_config = &node_config_cmp;
	Table * node_configs = init_table(min_nodes, max_nodes, load_factor, shrink_factor, hash_func_node_config, item_cmp_node_config);
	if (node_configs == NULL){
		fprintf(stderr, "[Master] Error: could not initialize master node_config table\n");
		return NULL;
	}

	master -> node_configs = node_configs;


	Self_Net * self_net = default_master_config_init_self_net(ip_addr);
	if (self_net == NULL) {
		fprintf(stderr, "[Master] Error: could not initialize master's self_net\n");
		return NULL;
	}

	// MASTER_NODE_ID defined within config.h
	Net_World * net_world = init_net_world(self_net, MASTER_NODE_ID, max_nodes, min_init_nodes, ip_addr);
	if (net_world == NULL){
		fprintf(stderr, "[Master] Error: could not initialize master's net_world\n");
		return NULL;
	}

	master -> net_world = net_world;

	// start master's rdma_init server here
	// this thread will never return

	// SHOULD HAVE SIGNAL HANDLING INSTEAD OF THIS
	// currently not checking if this thread terminates
	//	- should be fatal to master if this terminates and need to know!
	// ok for now i guess...
	ret = pthread_create(&(master -> tcp_rdma_init_server_thread), NULL, run_tcp_rdma_init_server, (void *) net_world);
	if (ret != 0){
		fprintf(stderr, "[Master] Error: could not start rdma_init tcp server\n");
		return NULL;
	}

	// also create all cq threads here


	Work_Pool * work_pool = init_work_pool(MAX_WORK_CLASS_IND);
	if (work_pool == NULL){
		fprintf(stderr, "Error: failed to intialize work_pool\n");
		return NULL;
	}


	Master_Worker_Data master_worker_data;
	master_worker_data.net_world = net_world;

	// WILL OVERWRITE THE ARGUMENT IN STEP 7 WHEN READY TO RUN!
	//	- each worker will have their own worker_data specified in their worker file
	ret = add_work_class(work_pool, MASTER_CLASS, NUM_MASTER_WORKER_THREADS, MASTER_WORKER_MAX_TASKS_BACKLOG, sizeof(Ctrl_Message), run_master_worker, &master_worker_data);
	if (ret != 0){
		fprintf(stderr, "Error: unable to add master worker class within init_master\n");
		return NULL;
	}

	// TODO: FIGURE OUT WHAT TO DO ABOUT MASTER'S CQ's
	// WHAT FIFO BUFFER SHOULD THOSE TASKS GO TO...?
	ret = activate_cq_threads(net_world, work_pool);
	if (ret != 0){
		fprintf(stderr, "[Master] Error; failure to activate_cq_threads\n");
		return NULL;
	}
	
	return master;
}


// A non-zero return value indicates a major problem: either memory allocation error, bookkeeping error of bad insert to table
//	=> The master server thread should terminate because program cannot continue
// A zero return value indicates no major issues, but not necessarily successful join: could have a timeout error
//	=> the successful join is indicated by the value set in ret_is_successful
// If successful, the id_assigned return argument is set

// Have the id_to_assign lock in case we want to try speeding up with threadpool handling joins (for very large network)
// The partner function to this is "process_join_net_response" within join_net.c 
//	- this will be called by worker nodes
int process_join_net_request(Worker_Connection * worker_connection, bool * ret_is_join_successful, uint32_t * ret_id_assigned){

	int ret;
	ssize_t byte_cnt;

	// Retriever the pointer to master
	Master * master = worker_connection -> master;

	// Retrieve the socket that will be used for sending the previously joined node configs
	// This was the sockfd returned from accept()
	int sockfd = worker_connection -> sockfd;

	// in case the connecting node didn't set its ip,
	// let it know what it was so it can start its rdma_init server
	uint32_t s_addr = worker_connection -> remote_sockaddr.sin_addr.s_addr;

	
	// 1.) Determine ID to assign node

	// master -> id_to_assign is incremented upon each previous successful addition
	// All Joins are Entirely Serialized
	//	- want to ensure id assignment has no gaps (monotonically increasing assignment, but gaps can occur if nodes leave)
	//		and thus requires waiting for successful confirmation that the joining node received the configuration information
	pthread_mutex_lock(&(master -> id_to_assign_lock));
	uint32_t id_to_assign = master -> id_to_assign;

	// 2.) Obtain all of the exisiting nodes in the table
	//		- Calling this function completely locks table for duration (no inserts/removals can occur during)
	//		- However there still may be race condition between a node leaving after this get_all_items_call and before client receives response
	//			- This is OK. No issues will occur except that the client's attempted TCP connection to the node that left will timeout => no worries
	//				- Probably expect a timeout like this to trigger a message to the master to confirm that the node did indeed leave

	Node_Config ** all_node_config;
	bool to_start_rand = false;
	bool to_sort = true;
	uint32_t node_cnt;
	ret = (uint32_t) get_all_items_table(master -> node_configs, to_start_rand, to_sort, (uint64_t *) &node_cnt, (void *) &all_node_config);
	if (ret != 0){
		fprintf(stderr, "[Master Server] Error: failure in get_all_items_table()\n");
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		close(sockfd);
		*ret_is_join_successful = false;
		return -1;
	}

	// 3.) Prepare Join Response
	Join_Response join_response;

	// a.) Specify the header
	join_response.header.node_id = id_to_assign;
	join_response.header.max_nodes = master -> max_nodes;
	join_response.header.cur_node_cnt = node_cnt;
	join_response.header.min_init_nodes = master -> min_init_nodes;
	join_response.header.s_addr = s_addr;
	
	// b.) Create dynamically sized array containing packed Node_Configs

	if (node_cnt > 0){
		join_response.node_config_arr = (Node_Config *) malloc(node_cnt * sizeof(Node_Config));
		if (join_response.node_config_arr == NULL){
			fprintf(stderr, "[Master Server] Error: malloc failed to allocate node_config array\n");
			pthread_mutex_unlock(&(master -> id_to_assign_lock));
			close(sockfd);
			// ensure to free the array returned from get_all_items table
			free(all_node_config);
			*ret_is_join_successful = false;
			return -1;
		}
		// pack the all_node_config into single buffer to send for convenience
		for (uint32_t i = 0; i < node_cnt; i++){
			memcpy(&(join_response.node_config_arr[i]), all_node_config[i], sizeof(Node_Config));
		}
		// ensure to free the array returned from get_all_items table
		free(all_node_config);
	}	
	else{
		join_response.node_config_arr = NULL;
	}

	// 4.) Send the join response header

	printf("\n[Master Server] Sending join response:\n\tNode ID: %u\n\tMax Nodes: %u\n\tCurrent Node Count: %u\n\tMin Init Nodes: %u\n\n",
			join_response.header.node_id, 
			join_response.header.max_nodes, 
			join_response.header.min_init_nodes, 
			join_response.header.cur_node_cnt);

	byte_cnt = send(sockfd, &join_response.header, sizeof(Join_Response_H), 0);
	if (byte_cnt != sizeof(Join_Response_H)){
		fprintf(stderr, "[Master Server] Error: Bad sending of join response header. Only sent %zd bytes out of %zu\n", byte_cnt, sizeof(Join_Response_H));
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		close(sockfd);
		free(join_response.node_config_arr);
		*ret_is_join_successful = false;
		return 0;
	}
	
	// 5.) Send the array (if non-null)

	if (node_cnt > 0){
		// benefit of
		byte_cnt = send(sockfd, join_response.node_config_arr, node_cnt * sizeof(Node_Config), 0);
		if (byte_cnt != node_cnt * sizeof(Node_Config)){
			fprintf(stderr, "[Master Server] Error: Bad sending of join response's node_config_array. Only sent %zd bytes out of %zu\n", byte_cnt, node_cnt * sizeof(Node_Config));
			pthread_mutex_unlock(&(master -> id_to_assign_lock));
			close(sockfd);
			free(join_response.node_config_arr);
			*ret_is_join_successful = false;
			return 0;
		}
		// successfully send the array of node_configs so can free
		free(join_response.node_config_arr);
	}

	// 6.) Block for confirmation (client sends back ACK, for the purposes of causing this to block)

	// The client will only send an ack upon success
	// Otherwise, the client will close the connection and this recv will return with connection failure
	bool ack;
	byte_cnt = recv(sockfd, &ack, sizeof(bool), MSG_WAITALL);
	if (byte_cnt != sizeof(bool)){
		fprintf(stderr, "[Master Server] Error: Didn't receive confirmation that worker was successful. Errno String: %s", strerror(errno));
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		close(sockfd);
		*ret_is_join_successful = false;
		return 0;
	}

	// 7.) The receiving end successfully obtained join_response
	//		now need to add its node_config to table for future joiners
	
	// a.) Create configuration for node (dynamically allocated because inserting to table)
	
	Node_Config * node_config = (Node_Config *) malloc(sizeof(Node_Config));
	if (node_config == NULL){
		fprintf(stderr, "[Master Server] Error: malloc failed allocating node_config\n");
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		close(sockfd);
		*ret_is_join_successful = false;
		return -1;
	}

	node_config -> node_id = id_to_assign;

	// Retrieve the worker node's ip address that was part of accept()
	struct sockaddr_in worker_sockaddr = worker_connection -> remote_sockaddr;
	uint32_t worker_s_addr = (uint32_t) (worker_sockaddr.sin_addr.s_addr);
	node_config -> s_addr = worker_s_addr;
	

	// b.) Insert node into table for future joiners

	// 	- should probably check for duplicate first...
	ret = insert_item_table(master -> node_configs, node_config);
	// IF FAILURE, THEN THE CURRENT CONNECTED WORKER WILL NOT RECEIVE RDMA_INIT CONNECTION REQUESTS FROM FUTURE JOINERS
	if (ret != 0){
		fprintf(stderr, "[Master Server] Error: failed to insert node with id: %u\n", id_to_assign);
		pthread_mutex_unlock(&(master -> id_to_assign_lock));
		free(node_config);
		close(sockfd);
		*ret_is_join_successful = false;
		return -1;
	}

	// 8.) Now send confirmation ack that this node was successfully added to table
	//		- ensures the receiving end is blocking until properly added

	ack = true;
	byte_cnt = send(sockfd, &ack, sizeof(bool), 0);
	if (byte_cnt != sizeof(bool)){
		fprintf(stderr, "[Master Server] Error: Couldn't send the ack indicating success of addition to node_config table. Errno String: %s", strerror(errno));
		close(sockfd);
		*ret_is_join_successful = false;
		return 0;
	}

	// 9.) Complete the connection

	//	a.) Set is_successful to true and populate id_assigned
	//	b.) Update counters
	//			- Increment id_to_assign
	//			- Decrement avail_node_cnt_sem (sem_wait)
	//	c.) Release id_to_assign lock
	//	d.) Close connection
	
	// a.) Set is successful to true and populate id_assigned
	*ret_is_join_successful = true;
	*ret_id_assigned = id_to_assign;

	// b.) Update counter 
	// Note: The semaphore counter has been updated optimistically and already accounted for this join
	master -> id_to_assign = id_to_assign + 1;
	
	// c.) Release lock
	pthread_mutex_unlock(&(master -> id_to_assign_lock));

	// d.) Close connection
	close(sockfd);

	return 0;

}


// NOTE: This function should be run in a seperate thread than main program.
void * run_join_net_server(void * _master) {

	int ret;



	// cast the argument passed in by pthread_create
	Master * master = (Master *) _master;


	// 1.) Start server

	char * ip_addr = master -> ip_addr;
	uint32_t max_nodes = master -> max_nodes;

	// defined wihtin tcp_connection.c
	int serv_sockfd = start_server_listen(ip_addr, JOIN_NET_PORT, max_nodes);
	if (serv_sockfd < 0){
		fprintf(stderr, "Error: failed to start join net server in master\n");
		return NULL;
	}


	// 2.) Process accepts
	// SHOULD REALLY BE MORE EFFICIENT (multi-threaded / event driver), but fine for now...
	
	// remote_addr and sockfd are dependent upon call to accept() so will be modified then
	Worker_Connection worker_connection;
	worker_connection.master = master;


	int connected_sockfd;
	// client_addr will be read based upon accept() call 
	// use this to obtain ip_addr and can look up
	// node_id based on net_world -> ip_to_node table (created based upon config file)
	struct sockaddr_in remote_sockaddr;
	socklen_t remote_len = sizeof(remote_sockaddr);

	// values populated by process_join_net_request
	bool is_join_successful;
	uint32_t id_assigned;

	int sem_val;

	while(1){

		sem_getvalue(&(master -> avail_node_cnt_sem), &sem_val);
		if (sem_val <= 0){
			printf("[Master Server] Network is full (%u nodes). Waiting for a node to leave before allowing more connections...\n\n", master -> max_nodes);
		}

		// Is blocking until there is an "available node" (means max_nodes - node_cnt)
		// for master to accept new joiner
		sem_wait(&(master -> avail_node_cnt_sem));

		printf("[Master Server] Waiting for clients to connect...\n\n");

		// now we decremented avail_node_count and reserved a spot for a new jointer

		// 1.) accept new connection (blocking)
		connected_sockfd = accept(serv_sockfd, (struct sockaddr *) &remote_sockaddr, &remote_len);
		if (connected_sockfd < 0){
			fprintf(stderr, "[Master Server] Error: could not process accept within master join server\n");
			return NULL;
		}

		// 2.) Modify the connection fields that depend on accpet
		worker_connection.sockfd = connected_sockfd;
		worker_connection.remote_sockaddr = remote_sockaddr;

		// 3.) Actually do data transimission and send/receive RDMA initialization data

		// If initializing a very large cluster, this part should be handled in seperate thread
		// with a thread pool!
		// This function will handle closing socket
		ret = process_join_net_request(&worker_connection, &is_join_successful, &id_assigned);
		if (ret != 0){
			fprintf(stderr, "[Master Server] Error: could not process connection from remote addr: %s\nA fatal error occured on server end, exiting\n", inet_ntoa(remote_sockaddr.sin_addr));
			return NULL;
		}

		// FOR NOW: Print out the result of processing request
		if (is_join_successful){
			printf("[Master Server] Successful join! Worker IP Address: %s, got assigned to id: %u\n\n", inet_ntoa(remote_sockaddr.sin_addr), id_assigned);
		}
		else {
			printf("[Master Server] Error: Unsuccessful join from Worker IP Address: %s\nNot fatal error, likely a connection error, continuing...\n\n", inet_ntoa(remote_sockaddr.sin_addr));
			// we optimistically decremeneted the semaphore, but there was an error so we should post back
			sem_post(&(master -> avail_node_cnt_sem));
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

	printf("[Master] Starting Master Server!\n\n");

	ret = pthread_create(&join_net_server_thread, NULL, run_join_net_server, (void *) master);
	if (ret != 0){
		fprintf(stderr, "Error: pthread_create failed to start join server\n");
		return -1;
	}


	// SHOULD HAVE SIGNAL HANDLING INSTEAD OF THIS

	// Should Be Infinitely Blocking 
	// (unless error or shutdown message)
	ret = pthread_join(join_net_server_thread, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: pthread_join failed for join server\n");
		return -1;
	}

	return 0;
}