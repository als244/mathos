#include "join_net.h"

// returns file descriptor of client_sockfd to read/write with
// in case of error, returns -1
// passing in master_port_num in case we want to open more ports to serve different content...
int connect_to_master(char * self_ip_addr, char * master_ip_addr, unsigned short master_port) {

	int ret;

	// 1.) Create client socket
	int client_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_sockfd == -1){
		fprintf(stderr, "Error: could not create client socket\n");
		return -1;
	}

	// 2.) Bind to the appropriate local network interface 
	struct sockaddr_in local_addr;
	local_addr.sin_family = AF_INET;
	// local binding on any port works
	local_addr.sin_port = 0;
	// use the interface assoicated with IP address passed in as argument
	// INET_ATON return 0 on error!
	ret = inet_aton(self_ip_addr, &local_addr.sin_addr);
	if (ret == 0){
		fprintf(stderr, "Error: self ip address: %s -- invalid\n", self_ip_addr);
		return -1;
	}
	ret = bind(client_sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr));
	if (ret != 0){
		fprintf(stderr, "Error: could not bind local client (self) to IP addr: %s\n", self_ip_addr);
		close(client_sockfd);
		return -1;
	}

	// 3.) Prepare server socket ip and port
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	// port is defined in config.h and master/workers agree
	serv_addr.sin_port = htons(master_port);
	ret = inet_aton(master_ip_addr, &serv_addr.sin_addr);
	// INET_ATON return 0 on error!
	if (ret == 0){
		fprintf(stderr, "Error: master ip address: %s -- invalid\n", master_ip_addr);
		close(client_sockfd);
		return -1;
	}

	ret = connect(client_sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	if (ret != 0){
		fprintf(stderr, "Error: could not connect to master (Address: %s, Port: %u)\n", master_ip_addr, master_port);
		close(client_sockfd);
		return -1;
	}

	return client_sockfd;
}


// Join_Response is defined in config.h
// The master server will be sending this to worker

// If fatal error (memory allocation), return -1, to let calling function handle termination
// If connection error, return 0, and set successful to false
// Upon success, return 0 and set successful to join and set the join response
int process_join_net_response(int sockfd, bool * ret_is_join_successful, Join_Response * ret_join_response){

	ssize_t byte_cnt;

	// Deference the pointer passed in that we are going to populate
	//	- doing this for readability
	// ASSUMES THE CALLER ALLOCATED MEMORY (either on stack or dynamically) for ret_join_response
	Join_Response join_response = *ret_join_response;

	// 1.) Read the header send from master
	byte_cnt = recv(sockfd, &join_response.header, sizeof(Join_Response_H), MSG_WAITALL);
	if (byte_cnt != sizeof(Join_Response_H)){
		fprintf(stderr, "Error: Couldn't receive the join response header. %zd/%zu bytes. Errno String: %s", byte_cnt, sizeof(Join_Response_H), strerror(errno));
		close(sockfd);
		*ret_is_join_successful = false;
		return 0;
	}

	// 2.) Allocate an array to store the Node_Configs (if node_cnt > 0)
	//		- this will be used to form connetions to these nodes' RDMA_INIT tcp servers

	uint32_t node_cnt = join_response.header.node_cnt;

	if (node_cnt > 0){
		join_response.node_config_arr = (Node_Config *) malloc(node_cnt * sizeof(Node_Config));
		// BAD ALLOCATION: FATAL ERROR
		if (join_response.node_config_arr == NULL){
			fprintf(stderr, "Error: malloc failed when allocating node_config_arr, terminating\n");
			close(sockfd);
			*ret_is_join_successful = false;
			return -1;
		}
	}
	else{
		join_response.node_config_arr = NULL;
	}

	// 3.) Receive the node_configs (if node_cnt > 0)

	if (node_cnt > 0){
		byte_cnt = recv(sockfd, join_response.node_config_arr, node_cnt * sizeof(Node_Config), MSG_WAITALL);
		if (byte_cnt != node_cnt * sizeof(Node_Config)) {
			fprintf(stderr, "Error: Couldn't receive the join response node configs. %zd/%zu bytes. Errno String: %s", byte_cnt, node_cnt * sizeof(Node_Config), strerror(errno));
			close(sockfd);
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
		*ret_is_join_successful = false;
		return 0;
	}


	// 5.) Set return values and close connection

	*ret_is_join_successful = true;

	// ret_join_response has already been populated by join_response beign set equal to *ret_join_response

	close(sockfd);

	return 0;
}

Join_Response * join_net(char * self_ip_addr, char * master_ip_addr) {

	int ret;

	// Loop until successfully joined network
	//	- communicates with master join_net server who then returns a response

	int client_sockfd;
	bool is_join_successful = false;

	Join_Response * join_response = (Join_Response *) malloc(sizeof(Join_Response));

	// loop until successfully join network
	while (!is_join_successful){

		client_sockfd = connect_to_master(self_ip_addr, master_ip_addr, JOIN_NET_PORT);
		if (client_sockfd == -1){
			fprintf(stderr, "Error: join_net failed because couldn't connect to master\n");
			return NULL;
		}
	
		// Note: this function will handle closing the socket
		ret = process_join_net_response(client_sockfd, &is_join_successful, join_response);
		if (ret == -1){
			fprintf(stderr, "Error: fatal problem within processing join response\n");
			return NULL;
		}

		// timeout before trying again
		// defined within config.h => default is 10ms
		usleep(JOIN_NET_TIMEOUT_MICROS);
	}

	printf("Successfully joined network! Was assigned id: %u\n", (join_response -> header).node_id);

	return join_response;
}
