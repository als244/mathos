#include "establish_net.h"

// MIGHT WANT TO CONVERT THREADS TO LIBEVENT FOR BETTER SCALABILITY (if initialization time is a concern)
//	- Ref: https://github.com/jasonish/libevent-examples/blob/master/echo-server/libevent_echosrv1.c

typedef struct connection_data {
	Net_World * net_world;
	// FOr server: remote_addr is the client_addr returned as part of accept()
	// For client: remote_addr is the server_addr (found using net_world -> node_to_ip table)
	struct sockaddr_in remote_sockaddr;
	// For server: sockfd = returned socket from accpet()
	// For client: sockfd = client_fd used as part of connect()
	int sockfd;
} Connection_Data;

// Called for each thread
// SAME FUNCTION IS CALLED BY BOTH SERVER ACCEPTING AND CLIENT CONNECTING!
int process_tcp_connection_for_rdma_init(Connection_Data * connection_data) {
	
	int ret;


	Net_World * net_world = connection_data -> net_world;

	// the other side's ip address
	struct sockaddr_in remote_sockaddr = connection_data -> remote_sockaddr;
	uint32_t remote_addr = (uint32_t) (remote_sockaddr.sin_addr.s_addr);

	// 1.) Look up from within net_world table to obtain node_id
	Table * ip_to_node = net_world -> ip_to_node;
	Node_Ip_Config target_node;
	target_node.ip_addr = remote_addr;

	Node_Ip_Config * found_node = find_item_table(ip_to_node, &target_node);
	if (found_node == NULL){
		fprintf(stderr, "Error: could not find node in ip_to_node table with ip addr: %u\n", remote_addr);
		return -1;
	}

	uint32_t remote_node_id = found_node -> node_id;


	// Socket to write/read from!
	int sockfd = connection_data -> sockfd;

	// HANDLE PROCESSING THE CONNECTION HERE....

	// 1.) Write data from self_net containing RDMA_details


	// 2.) Read other end from socket 
	//		and create node with RDMA details and add to net_world -> nodes
	

	return 0;
}


// NOTE: This function should be run in a seperate thread than main program.

// When exchanging rdma init info between node i and node j, if i < j => i is the server, j is the client
// max_accepts should be set >= known number of connecteding nodes (i.e. the number of nodes with id's greater than server node id)
// If max_accepts is set equal to known number upon intializaition (based on config file) the thread will terminate.

// However, max_accepts can be set larger to account for new joiners of system, even while system is runnning (meaning, acutally processing/computing requests)
// BUT, other functionality is necessary to account for new joiners (rebalancing exchange metadata).
// Assumption is that new joiners will have node id > this node id, meaning that in order to accept new joiner to system this tcp server needs to be running 
void * run_tcp_server_for_rdma_init(void * _server_thread_data){

	int ret;

	// Cast the argument (passed in by pthread_create)
	Server_Thread_Data * server_thread_data = (Server_Thread_Data *) _server_thread_data;
	// Obtain "true" arguments
	Net_World * net_world = server_thread_data -> net_world;
	uint32_t max_accepts = server_thread_data -> max_accepts;
	uint32_t num_accepts_processed = server_thread_data -> num_accepts_processed;


	// 1.) create server TCP socket
	int serv_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sockfd != 0){
		fprintf(stderr, "Error: could not create server socket\n");
		return NULL;
	}

	// 2.) Init server info
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;

	// normally would use inet_aton (or inet_addr) here with a char * ipv4 dots
	// however, we intialized the net_world struct with the converted network-ordered uint32 value already
	uint32_t self_network_ip_addr = net_world -> self_net -> ip_addr;
	serv_addr.sin_addr.s_addr = self_network_ip_addr;
	// Using the same port on all nodes for initial connection establishment to trade rdma info
	// defined within establish_net.h
	serv_addr.sin_port = htons(ESTABLISH_NET_PORT);

	// 3.) Bind server to port
	ret = bind(serv_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
	if (ret != 0){
		fprintf(stderr, "Error: could not bind server socket to address: %s, port: %u\n", inet_ntoa(serv_addr.sin_addr), ESTABLISH_NET_PORT);
		return NULL;
	}

	// 4.) Start Listening
	ret = listen(serv_sockfd, max_accepts);
	if (ret != 0){
		fprintf(stderr, "Error: could not start listening on server socket\n");
		return NULL;
	}

	// 5.) Process accepts
	
	// Setup connection data to have the net_world field populated
	// remote_addr and sockfd are dependent upon call to accept() so will be modified then
	Connection_Data * connection_data = (Connection_Data *) malloc(sizeof(Connection_Data));
	if (connection_data == NULL){
		fprintf(stderr, "Error: malloc failed to allocate connection data\n");
		return NULL;
	}
	connection_data -> net_world = net_world;


	int accept_sockfd;
	// client_addr will be read based upon accept() call 
	// use this to obtain ip_addr and can look up
	// node_id based on net_world -> ip_to_node table (created based upon config file)
	struct sockaddr_in remote_sockaddr;
	socklen_t remote_len = sizeof(remote_sockaddr);

	while(num_accepts_processed < max_accepts){

		// 1.) accept new connection (blocking)

		accept_sockfd = accept(serv_sockfd, (struct sockaddr *) &remote_sockaddr, &remote_len);
		if (accept_sockfd < 0){
			fprintf(stderr, "Error: could not process accept within tcp inti server\n");
			return NULL;
		}

		// 2.) Modify the connection data fields that depend on accpet
		connection_data -> sockfd = accept_sockfd;
		connection_data -> remote_sockaddr = remote_sockaddr;

		// 3.) Actually do data transimission and send/receive RDMA initialization data

		// If initializing a very large cluster, this part should be handled in seperate thread
		// with a thread pool!
		ret = process_tcp_connection_for_rdma_init(connection_data);
		if (ret != 0){
			fprintf(stderr, "Error: could not process connection from remote addr: %s\n", inet_ntoa(remote_sockaddr.sin_addr));
			return NULL;
		}

		// 4.) Update number of accepts processed 
		// and indicate to the thread that called pthread_thread create
		num_accepts_processed++;
		server_thread_data -> num_accepts_processed = num_accepts_processed;
	}

	return NULL;
}


// NOTE: This function should be run in a seperate thread than main program.





