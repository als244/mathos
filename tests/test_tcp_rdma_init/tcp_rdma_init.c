#include "tcp_rdma_init.h"


// USED FOR BOTH SERVER + CLIENTS!
int process_rdma_init_connection(int sockfd, Net_World * net_world, bool * is_rdma_init_successful, uint32_t * ret_node_id_added) {

}



// After successful join request, the node will a receive an array of all Node_Config
// with ip addresses that it should connect to. It will call this function for each of these
int connect_to_rdma_init_server(Net_World * net_world, char * rdma_init_server_ip_addr){


	int ret;

	// 1.) Loop until successfully shared and retrieved rdma info with server

	int client_sockfd;
	bool is_rdma_init_successful = false;
	uint32_t node_id_added;

	// loop until successfully join network
	// probably want to have a terminating case so it doesn't hang if the server leaves network
	// also, should ideally have threads to connect to multiple other nodes in parallel
	while (!is_rdma_init_successful){

		client_sockfd = connect_to_server(rdma_init_server_ip_addr, NULL, RDMA_INIT_PORT);
		if (client_sockfd == -1){
			fprintf(stderr, "Couldn't connect to master. Timeout and retrying...\n");
			// timeout before trying again
			// defined within config.h => default is 1 sec
			usleep(RDMA_INIT_TIMEOUT_MICROS);
			continue;
		}

		// Note: this function will handle closing the socket
		ret = process_rdma_init_connection(client_sockfd, net_world, &is_rdma_init_successful, &node_id_added);
		if (ret == -1){
			fprintf(stderr, "Error: fatal problem within processing join response\n");
			return -1;
		}		
	}

	return 0;
}


// Started in a thread at the end of processing join_request
// Note; This function never terminates!
void * run_tcp_rdma_init_server(void * _net_world) {

	int ret;

	// cast the argument passed in by pthread_create
	Net_World * net_world = (Net_World *) _net_world;

	char * ip_addr = net_world -> self_rdma_init_server_ip_addr;
	uint32_t max_nodes = net_world -> max_nodes;

	// 1.) create server TCP socket
	int serv_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sockfd == -1){
		fprintf(stderr, "Error: could not create server socket\n");
		return NULL;
	}

	// 2.) Init server info
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;

	// set IP address of server
	// INET_ATON return 0 on error!
	ret = inet_aton(ip_addr, &serv_addr.sin_addr);
	if (ret == 0){
		fprintf(stderr, "Error: master join server ip address: %s -- invalid\n", ip_addr);
		return NULL;
	}
	// defined within config.h
	serv_addr.sin_port = htons(RDMA_INIT_PORT);

	// 3.) Bind server to port
	ret = bind(serv_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
	if (ret != 0){
		fprintf(stderr, "Error: could not bind server socket to address: %s, port: %u\n", ip_addr, JOIN_NET_PORT);
		return NULL;
	}

	// 4.) Start Listening
	ret = listen(serv_sockfd, max_nodes);
	if (ret != 0){
		fprintf(stderr, "Error: could not start listening on server socket\n");
		return NULL;
	}


	// 5.) Handle requests

	int connected_sockfd;
	// client_addr will be read based upon accept() call 
	// use this to obtain ip_addr and can look up
	// node_id based on net_world -> ip_to_node table (created based upon config file)
	struct sockaddr_in remote_sockaddr;
	socklen_t remote_len = sizeof(remote_sockaddr);

	bool is_rdma_init_successful;
	uint32_t node_id_added;

	// run continuously
	while(1){

		// 1.) accept new connection (blocking)
		connected_sockfd = accept(serv_sockfd, (struct sockaddr *) &remote_sockaddr, &remote_len);
		if (connected_sockfd < 0){
			fprintf(stderr, "Error: could not process accept within master join server\n");
			// fatal error
			return NULL;
		}

		// 2.) Actually do data transimission and send/receive RDMA initialization data

		// If initializing a very large cluster, this part should be handled in seperate thread
		// with a thread pool!
		// This function will handle closing socket
		ret = process_rdma_init_connection(connected_sockfd, net_world, &is_rdma_init_successful, &node_id_added);
		if (ret != 0){
			fprintf(stderr, "Error: could not process connection from remote addr: %s\nA fatal error occured on server end, exiting\n", inet_ntoa(remote_sockaddr.sin_addr));
			return NULL;
		}

		// FOR NOW: Print out the result of processing request
		if (is_rdma_init_successful){
			printf("RDMA Initialization Successful! Node ID: %u (ip addr: %s) added to table", node_id_added, inet_ntoa(remote_sockaddr.sin_addr));
		}
		else {
			printf("Error: Unsuccessful RDMA Initialization from ip addr: %s\nNot fatal error, likely a connection error, continuing...\n\n", inet_ntoa(remote_sockaddr.sin_addr));
		}

	}

}