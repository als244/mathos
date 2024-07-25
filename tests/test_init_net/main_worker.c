#include "self_net.h"

int main(int argc, char * argv[]){

	
	Self_Net * self_net = default_worker_config_init_self_net(NULL);
	if (self_net == NULL){
		fprintf(stderr, "Error: could not initialize self net\n");
		return -1;
	}

	printf("Success! Total Ports: %d, Total QPs: %d\n", self_net -> self_node -> total_ports, self_net -> self_node -> total_qps);

	/*
	if ((argc != 2) && (argc != 3)){
		fprintf(stderr, "Error: Usage ./testJoinNet <master_ip_addr> <self_ip_addr>\n");
		return -1;
	}
	
	char * master_ip_addr = argv[1];
	
	char * self_ip_addr = NULL;
	if (argc == 3){
		self_ip_addr = argv[2];
	}	

	printf("\n\nAttempting to join_net...\n");

	Join_Response * join_response = join_net(master_ip_addr, self_ip_addr);
	if (join_response == NULL){
		fprintf(stderr, "Error: could not join net, exiting\n");
		return -1;
	}


	printf("\n\nReceived Join Response!\n\n");

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
	*/
}