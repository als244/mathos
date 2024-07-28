#include "join_net.h"

// Join_Response is defined in config.h
// The master server will be sending this to worker

// If fatal error (memory allocation), return -1, to let calling function handle termination
// If connection error, return 0, and set successful to false
// Upon success, return 0 and set successful to join and set the join response
int process_join_net(char * master_ip_addr, Self_Net * self_net, int sockfd, bool * ret_is_join_successful, Join_Response ** ret_join_response, Net_World ** ret_net_world) {

	int ret;
	ssize_t byte_cnt;

	Join_Response * join_response = (Join_Response *) malloc(sizeof(Join_Response));
	if (join_response == NULL){
		fprintf(stderr, "Error: malloc failed to allocate join response\n");
		close(sockfd);
		*ret_is_join_successful = false;
		return -1;
	}

	// 1.) Read the header send from master
	byte_cnt = recv(sockfd, &(join_response -> header), sizeof(Join_Response_H), MSG_WAITALL);
	if (byte_cnt != sizeof(Join_Response_H)){
		fprintf(stderr, "Error: Couldn't receive the join response header. %zd/%zu bytes. Errno String: %s", byte_cnt, sizeof(Join_Response_H), strerror(errno));
		close(sockfd);
		free(join_response);
		*ret_is_join_successful = false;
		return 0;
	}


	// 2.) Allocate an array to store the Node_Configs (if node_cnt > 0)
	//		- this will be used to form connetions to these nodes' RDMA_INIT tcp servers

	uint32_t node_cnt = join_response -> header.cur_node_cnt;

	if (node_cnt > 0){
		join_response -> node_config_arr = (Node_Config *) malloc(node_cnt * sizeof(Node_Config));
		// BAD ALLOCATION: FATAL ERROR
		if (join_response -> node_config_arr == NULL){
			fprintf(stderr, "Error: malloc failed when allocating node_config_arr, terminating\n");
			close(sockfd);
			free(join_response);
			*ret_is_join_successful = false;
			return -1;
		}
	}
	else{
		join_response -> node_config_arr = NULL;
	}

	// 3.) Receive the node_configs (if node_cnt > 0)

	if (node_cnt > 0){
		byte_cnt = recv(sockfd, join_response -> node_config_arr, node_cnt * sizeof(Node_Config), MSG_WAITALL);
		if (byte_cnt != node_cnt * sizeof(Node_Config)) {
			fprintf(stderr, "Error: Couldn't receive the join response node configs. %zd/%zu bytes. Errno String: %s", byte_cnt, node_cnt * sizeof(Node_Config), strerror(errno));
			close(sockfd);
			free(join_response -> node_config_arr);
			free(join_response);
			*ret_is_join_successful = false;
			return 0;
		}
	}


	// 4.) Send successful confirmation back to server
	//		- purpose is to force blocking behavior for debuggability

	bool ack = true;
	byte_cnt = send(sockfd, &ack, sizeof(bool), 0);
	if (byte_cnt != sizeof(bool)){
		fprintf(stderr, "Error: Couldn't send the ack indicating success. Errno String: %s", strerror(errno));
		close(sockfd);
		free(join_response -> node_config_arr);
		free(join_response);
		*ret_is_join_successful = false;
		return 0;
	}

	// 5.) Block for confirmation of addition to table
	//		- ensures that this node has been properly added and that it will receive rdma_init connection requests from future joiners
	byte_cnt = recv(sockfd, &ack, sizeof(bool), MSG_WAITALL);
	if (byte_cnt != sizeof(bool)){
		fprintf(stderr, "Error: Didn't receive confirmation that the node config was added to table. Errno String: %s", strerror(errno));
		close(sockfd);
		free(join_response -> node_config_arr);
		free(join_response);
		*ret_is_join_successful = false;
		return 0;
	}


	// 6.) Retrieve Node ID from header use this info to initialize this node's net_world

	uint32_t assigned_node_id = join_response -> header.node_id;
	uint32_t max_nodes = join_response -> header.max_nodes;
	uint32_t min_init_nodes = join_response -> header.min_init_nodes;


	struct in_addr self_rdma_init_server_sin_addr;
	self_rdma_init_server_sin_addr.s_addr = join_response -> header.s_addr;

	char * self_rdma_init_server_ip_addr = inet_ntoa(self_rdma_init_server_sin_addr);


	Net_World * net_world = init_net_world(self_net, assigned_node_id, max_nodes, min_init_nodes, self_rdma_init_server_ip_addr);
	if (net_world == NULL){
		fprintf(stderr, "Error: failed to initialize net world\n");
		close(sockfd);
		free(join_response -> node_config_arr);
		free(join_response);
		*ret_is_join_successful = false;
		// fatal error
		return -1;
	}


	// 7.) Start rdma_init TCP server to begin sharing rdma info (beginning with Master)
	//		- this thread should always be running

	ret = pthread_create(&(net_world -> tcp_rdma_init_server_thread), NULL, run_tcp_rdma_init_server, (void *) net_world);
	if (ret != 0){
		fprintf(stderr, "Error: pthread_create failed to start join server\n");
		close(sockfd);
		free(join_response -> node_config_arr);
		free(join_response);
		destroy_net_world(net_world);
		*ret_is_join_successful = false;
		return -1;
	}
	

	// 8.) Connect to the master's rdma_init_server

	// this will loop infinitely until connection...
	// only returns upon fatal error on this end
	ret = connect_to_rdma_init_server(net_world, master_ip_addr, true);
	if (ret != 0){
		fprintf(stderr, "Error: fatal error occured when trying to connect to master's rdma_init server\n");
		close(sockfd);
		free(join_response -> node_config_arr);
		free(join_response);
		destroy_net_world(net_world);
		*ret_is_join_successful = false;
		return -1;
	}

	// 9.) Set return values

	*ret_is_join_successful = true;
	*ret_join_response = join_response;
	*ret_net_world = net_world;

	// 7.) Close successful connection
	close(sockfd);

	return 0;
}

int join_net(Self_Net * self_net, char * master_ip_addr, Join_Response ** ret_join_response, Net_World ** ret_net_world) {

	int ret;

	// Loop until successfully joined network
	//	- communicates with master join_net server who then returns a response

	int client_sockfd;
	bool is_join_successful = false;

	// loop until successfully join network
	while (!is_join_successful){

		client_sockfd = connect_to_server(master_ip_addr, self_net -> ip_addr, JOIN_NET_PORT);
		if (client_sockfd == -1){
			fprintf(stderr, "Couldn't connect to master. Timeout and retrying...\n");
			// timeout before trying again
			// defined within config.h => default is 1 sec
			usleep(JOIN_NET_TIMEOUT_MICROS);
			continue;
		}

		// Note: this function will handle closing the socket
		ret = process_join_net(master_ip_addr, self_net, client_sockfd, &is_join_successful, ret_join_response, ret_net_world);
		if (ret == -1){
			fprintf(stderr, "Error: fatal problem within processing join response\n");
			return -1;
		}		
	}

	return 0;
}
