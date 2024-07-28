#include "tcp_rdma_init.h"


// USED FOR BOTH SERVER + CLIENTS!
// Returns -1 on fatal error, otherwise 0
// If there was a connection error then ret_is_rdma_init_successful is set to false
// Upon success, the remote node id is set
int process_rdma_init_connection(int sockfd, Net_World * net_world, bool * ret_is_rdma_init_successful, uint32_t * ret_node_id_added) {

	ssize_t byte_cnt;

	Rdma_Init_Info * self_rdma_init_info = net_world -> self_rdma_init_info;


	Rdma_Init_Info * remote_rdma_init_info = malloc(sizeof(Rdma_Init_Info));
	if (remote_rdma_init_info == NULL){
		fprintf(stderr, "Error: malloc failed to allocate remtoe rdma_init info\n");
		close(sockfd);
		*ret_is_rdma_init_successful = false;
		return -1;
	}

	// 1.) send / recieve header

	byte_cnt = send(sockfd, &(self_rdma_init_info -> header), sizeof(Rdma_Init_Info_H), 0);
	if (byte_cnt != sizeof(Rdma_Init_Info_H)){
		fprintf(stderr, "Error: Bad sending of rdma init info header. Only sent %zd bytes out of %zu\n", byte_cnt, sizeof(Rdma_Init_Info_H));
		close(sockfd);
		free(remote_rdma_init_info);
		*ret_is_rdma_init_successful = false;
		return 0;
	}

	byte_cnt = recv(sockfd, &(remote_rdma_init_info -> header), sizeof(Rdma_Init_Info_H), MSG_WAITALL);
	if (byte_cnt != sizeof(Rdma_Init_Info_H)){
		fprintf(stderr, "Error: Bad receiving of rdma init info header. Only received %zd bytes out of %zu\n", byte_cnt, sizeof(Rdma_Init_Info_H));
		close(sockfd);
		free(remote_rdma_init_info);
		*ret_is_rdma_init_successful = false;
		return 0;
	}

	
	// 2.) Allocate memory based on the header

	uint32_t remote_num_ports = remote_rdma_init_info -> header.num_ports;
	uint32_t remote_num_endpoints = remote_rdma_init_info -> header.num_endpoints;

	remote_rdma_init_info -> remote_ports_init = (Remote_Port_Init *) malloc(remote_num_ports * sizeof(Remote_Port_Init));
	if (remote_rdma_init_info -> remote_ports_init == NULL){
		fprintf(stderr, "Error: malloc failed to allocate remote_ports_init array\n");
		close(sockfd);
		free(remote_rdma_init_info);
		*ret_is_rdma_init_successful = false;
		return -1;
	}

	remote_rdma_init_info -> remote_endpoints = (Remote_Endpoint *) malloc(remote_num_endpoints * sizeof(Remote_Endpoint));
	if (remote_rdma_init_info -> remote_endpoints == NULL){
		fprintf(stderr, "Error: malloc failed to allocate remote_ports_init array\n");
		close(sockfd);
		free(remote_rdma_init_info -> remote_ports_init);
		free(remote_rdma_init_info);
		*ret_is_rdma_init_successful = false;
		return -1;
	}
	
	
	// 3.) send / receive ports array

	uint64_t sending_ports_size = self_rdma_init_info -> header.num_ports * sizeof(Remote_Port_Init);
	byte_cnt = send(sockfd, self_rdma_init_info -> remote_ports_init, sending_ports_size, 0);
	if (byte_cnt != sending_ports_size){
		fprintf(stderr, "Error: Bad sending of rdma init info ports. Only sent %zd bytes out of %zu\n", byte_cnt, sending_ports_size);
		close(sockfd);
		free(remote_rdma_init_info -> remote_ports_init);
		free(remote_rdma_init_info -> remote_endpoints);
		free(remote_rdma_init_info);
		*ret_is_rdma_init_successful = false;
		return 0;
	}


	uint64_t recv_ports_size = remote_num_ports * sizeof(Remote_Port_Init);
	byte_cnt = recv(sockfd, remote_rdma_init_info -> remote_ports_init, recv_ports_size, MSG_WAITALL);
	if (byte_cnt != recv_ports_size){
		fprintf(stderr, "Error: Bad receiving of rdma init info ports. Only received %zd bytes out of %zu\n", byte_cnt, recv_ports_size);
		close(sockfd);
		free(remote_rdma_init_info -> remote_ports_init);
		free(remote_rdma_init_info -> remote_endpoints);
		free(remote_rdma_init_info);
		*ret_is_rdma_init_successful = false;
		return 0;
	}

	// 3.) send / receive endpoints array

	uint64_t sending_endpoints_size = self_rdma_init_info -> header.num_endpoints * sizeof(Remote_Endpoint);
	byte_cnt = send(sockfd, self_rdma_init_info -> remote_endpoints, sending_endpoints_size, 0);
	if (byte_cnt != sending_endpoints_size){
		fprintf(stderr, "Error: Bad sending of rdma init info endpoints. Only sent %zd bytes out of %zu\n", byte_cnt, sending_endpoints_size);
		close(sockfd);
		free(remote_rdma_init_info -> remote_ports_init);
		free(remote_rdma_init_info -> remote_endpoints);
		free(remote_rdma_init_info);
		*ret_is_rdma_init_successful = false;
		return 0;
	}


	uint64_t recv_endpoints_size = remote_num_endpoints * sizeof(Remote_Endpoint);
	byte_cnt = recv(sockfd, remote_rdma_init_info -> remote_endpoints, recv_endpoints_size, MSG_WAITALL);
	if (byte_cnt != recv_endpoints_size){
		fprintf(stderr, "Error: Bad receiving of rdma init info endpoints. Only received %zd bytes out of %zu\n", byte_cnt, recv_endpoints_size);
		close(sockfd);
		free(remote_rdma_init_info -> remote_ports_init);
		free(remote_rdma_init_info -> remote_endpoints);
		free(remote_rdma_init_info);
		*ret_is_rdma_init_successful = false;
		return 0;
	}


	// 4.) Create node based on other's rdma_init_info

	Net_Node * node = net_add_node(net_world, remote_rdma_init_info);
	if (node == NULL){
		fprintf(stderr, "Error: net_add_node failed\n");
		close(sockfd);
		free(remote_rdma_init_info -> remote_ports_init);
		free(remote_rdma_init_info -> remote_endpoints);
		free(remote_rdma_init_info);
		*ret_is_rdma_init_successful = false;
		// fatal error if couldn't add
		return -1;
	}

	// 5.) send / receive confirmation

	bool ack = true;
	byte_cnt = send(sockfd, &ack, sizeof(bool), 0);
	if (byte_cnt != sizeof(bool)){
		fprintf(stderr, "Error: Couldn't send the ack indicating success. Errno String: %s", strerror(errno));
		close(sockfd);
		free(remote_rdma_init_info -> remote_ports_init);
		free(remote_rdma_init_info -> remote_endpoints);
		free(remote_rdma_init_info);
		destroy_remote_node(net_world, node);
		*ret_is_rdma_init_successful = false;
		return 0;
	}

	byte_cnt = recv(sockfd, &ack, sizeof(bool), MSG_WAITALL);
	if (byte_cnt != sizeof(bool)){
		fprintf(stderr, "Error: Didn't receive confirmation that the node was successfully added on other end. Errno String: %s", strerror(errno));
		close(sockfd);
		free(remote_rdma_init_info -> remote_ports_init);
		free(remote_rdma_init_info -> remote_endpoints);
		free(remote_rdma_init_info);
		destroy_remote_node(net_world, node);
		*ret_is_rdma_init_successful = false;
		return 0;
	}

	// 6.) free the rdma init info we allocated now that node has all the info
	free(remote_rdma_init_info -> remote_ports_init);
	free(remote_rdma_init_info -> remote_endpoints);
	free(remote_rdma_init_info);


	// 7.) check the table count and see if it equals min_init_nodes
	//		- if so, then post to the semaphore to let main init function return

	uint32_t min_init_nodes = net_world -> min_init_nodes;

	uint32_t connected_nodes_cnt = (uint32_t) get_count(net_world -> nodes);
	if (connected_nodes_cnt == min_init_nodes){
		sem_post(&(net_world -> is_init_ready));
	}


	// 8.) Report success

	*ret_is_rdma_init_successful = true;
	*ret_node_id_added = node -> node_id;

	return 0;
}



// After successful join request, the node will a receive an array of all Node_Config
// with ip addresses that it should connect to. It will call this function for each of these
int connect_to_rdma_init_server(Net_World * net_world, char * rdma_init_server_ip_addr, bool to_master){


	int ret;

	// 1.) Loop until successfully shared and retrieved rdma info with server

	int client_sockfd;
	bool is_rdma_init_successful = false;
	uint32_t node_id_added;

	unsigned short rdma_init_server_port;
	if (to_master){
		rdma_init_server_port = MASTER_RDMA_INIT_PORT;
	}
	else{
		rdma_init_server_port = WORKER_RDMA_INIT_PORT;
	}


	// loop until successfully join network
	// probably want to have a terminating case so it doesn't hang if the server leaves network
	// also, should ideally have threads to connect to multiple other nodes in parallel
	while (!is_rdma_init_successful){



		client_sockfd = connect_to_server(rdma_init_server_ip_addr, NULL, rdma_init_server_port);
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
			fprintf(stderr, "Error: fatal problem within processing rdma init connection\n");
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

	char * rdma_init_serv_ip_addr = net_world -> self_rdma_init_server_ip_addr;
	uint32_t max_nodes = net_world -> max_nodes;

	// 1.) Start server listening

	unsigned short server_port;
	if (net_world -> self_node_id == MASTER_NODE_ID){
		server_port = MASTER_RDMA_INIT_PORT;
	}
	else{
		server_port = WORKER_RDMA_INIT_PORT;
	}

	int serv_sockfd = start_server_listen(rdma_init_serv_ip_addr, server_port, max_nodes);
	if (serv_sockfd < 0){
		fprintf(stderr, "Error: unable to start tcp rdma init server on ip addr: %s and port: %u\n", rdma_init_serv_ip_addr, server_port);
		return NULL;
	}


	// 2.) Handle requests

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
			fprintf(stderr, "[RDMA_Init TCP Server] Error: could not process accept within master join server\n");
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
			printf("[Node %u -- RDMA Init Server] RDMA Initialization Successful! To Node ID: %u (ip addr: %s). Added to net_world table.\n", 
				net_world -> self_node_id, node_id_added, inet_ntoa(remote_sockaddr.sin_addr));
		}
		else {
			printf("[Node %u -- RDMA Init Server]  Error: Unsuccessful RDMA Initialization from ip addr: %s\nNot fatal error, likely a connection error, continuing...\n\n", 
				net_world -> self_node_id, inet_ntoa(remote_sockaddr.sin_addr));
		}

	}

}