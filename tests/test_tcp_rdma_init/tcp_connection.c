#include "tcp_connection.h"


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