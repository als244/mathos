#include "tcp_connection.h"

// returns serv_sockfd
int start_server_listen(char * server_ip_addr, unsigned short server_port, int backlog) {

	int ret;

	// 1.) create server TCP socket
	int serv_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sockfd == -1){
		fprintf(stderr, "[Master Server] Error: could not create server socket\n");
		return -1;
	}

	// 2.) Init server info
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;

	// set IP address of server
	// INET_ATON return 0 on error!
	ret = inet_aton(server_ip_addr, &serv_addr.sin_addr);
	if (ret == 0){
		fprintf(stderr, "[Master Server] Error: master join server ip address: %s -- invalid\n", server_ip_addr);
		return -1;
	}
	// defined within config.h
	serv_addr.sin_port = htons(server_port);

	// 3.) Set socket options to make development nicely
	//		- when this program terminates from various reasons (could be any thread dying)
	//			 want this addr/port to be available for binding upon restart without having to deal with TIME_WAIT in kernel

	int enable_reuse;
	ret = setsockopt(serv_sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &enable_reuse, sizeof(enable_reuse));
	if (ret != 0){
		fprintf(stderr, "Error: unable to set socket options in join_net server\n");
		return -1;
	}

	// 4.) Bind server to port
	ret = bind(serv_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
	if (ret != 0){
		fprintf(stderr, "[Master Server] Error: could not bind server socket to address: %s, port: %u\n", server_ip_addr, server_port);
		return -1;
	}

	// 5.) Start Listening
	ret = listen(serv_sockfd, backlog);
	if (ret != 0){
		fprintf(stderr, "[Master Server] Error: could not start listening on server socket\n");
		return -1;
	}

	return serv_sockfd;

}

// returns file descriptor of client_sockfd to read/write with
// in case of error, returns -1
// passing in server_port_num in case we want to open more ports to serve different content...
int connect_to_server(char * server_ip_addr, char * self_ip_addr, unsigned short server_port) {

	int ret;

	// 1.) Create client socket
	int client_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_sockfd == -1){
		fprintf(stderr, "Error: could not create client socket\n");
		return -1;
	}

	// 2.) Bind to the appropriate local network interface  (if self_ip_addr != )
	if ((self_ip_addr != NULL) && strcmp(self_ip_addr, server_ip_addr) != 0){
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
	}

	// 3.) Prepare server socket ip and port
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	// port is defined in config.h and master/workers agree
	serv_addr.sin_port = htons(server_port);
	
	ret = inet_aton(server_ip_addr, &serv_addr.sin_addr);
	// INET_ATON return 0 on error!
	if (ret == 0){
		fprintf(stderr, "Error: server ip address: %s -- invalid\n", server_ip_addr);
		close(client_sockfd);
		return -1;
	}

	ret = connect(client_sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	if (ret != 0){
		fprintf(stderr, "Error: could not connect to server (Address: %s, Port: %u)\n", server_ip_addr, server_port);
		close(client_sockfd);
		return -1;
	}

	return client_sockfd;
}