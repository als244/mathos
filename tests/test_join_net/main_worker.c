#include "join_net.h"

int main(int argc, char * argv[]){

	if (argc != 3){
		fprintf(stderr, "Error: Usage ./testJoinNet <self_ip_addr> <master_ip_addr>\n");
		return -1;
	}

	char * self_ip_addr = argv[1];
	char * master_ip_addr = argv[2];

	Join_Response * join_response = join_net(self_ip_addr, master_ip_addr);
	if (join_response == NULL){
		fprintf(stderr, "Error: could not join net, exiting\n");
		return -1;
	}


	printf("\n\n\nReceived Join Response!\n\n");

	printf("Join Response Header:\n");
	
	Join_Response_H join_response_h = join_response -> header;
	
	printf("\
			Node ID: %u\n \
			Max Nodes: %u\n \
			Min Init Nodes: %u\n \
			Node Count: %u\n\n", 
			join_response_h.node_id, 
			join_response_h.max_nodes, 
			join_response_h.min_init_nodes, 
			join_response_h.node_cnt);

	return 0;
}