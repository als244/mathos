#include "init_net.h"

int main(int argc, char * argv[]){

	int ret;
	
	if ((argc != 2) && (argc != 3)){
		fprintf(stderr, "Error: Usage ./testWorker <master_ip_addr> <self_ip_addr>\n");
		return -1;
	}
	
	char * master_ip_addr = argv[1];
	
	char * self_ip_addr = NULL;
	if (argc == 3){
		self_ip_addr = argv[2];
	}	

	Net_World * net_world = init_net(master_ip_addr, self_ip_addr);

	if (net_world == NULL){
		fprintf(stderr, "Error: failed to initialize net\n");
		return -1;
	}

	printf("\n\nSuccessfully initialized network!\n\n\n");

	Ctrl_Message ctrl_message;
	ctrl_message.header.source_node_id = net_world -> self_node_id;

	uint32_t dest_node_id;
	if (net_world -> self_node_id == 1){
		dest_node_id = 2;
		ctrl_message.header.message_type = BID_ORDER;
		strcpy((char *) ctrl_message.contents, "Hello");
		
	}
	if (net_world -> self_node_id == 2){
		dest_node_id = 1;
		ctrl_message.header.message_type = OFFER_ORDER;
		strcpy((char *) ctrl_message.contents, "World");
	}

	printf("\n\n[Node %u] Posting a control message. Sending to node id: %d...\n\n", net_world -> self_node_id, dest_node_id);
	ret = post_send_ctrl_net(net_world, &ctrl_message, dest_node_id);
	if (ret != 0){
		fprintf(stderr, "Error: could not post control message. From id: %u going to node id: %u\n", net_world -> self_node_id, dest_node_id);
		return -1;
	}

	// Should Be Infinitely Blocking 
	// (unless error or shutdown message)
	ret = pthread_join(net_world -> tcp_rdma_init_server_thread, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: pthread_join failed for join server\n");
		return -1;
	}

	return 0;
}