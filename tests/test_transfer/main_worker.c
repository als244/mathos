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


	printf("Attempting to post a control message...\n");

	Control_Message ctrl_message;
	ctrl_message.header.source_node_id = net_world -> self_node_id;
	ctrl_message.header.message_type = BID_ORDER;

	for (int i = 0; i < CONTROL_MESSAGE_CONTENTS_MAX_SIZE_BYTES; i++){
		ctrl_message.contents[i] = i;
	}

	uint32_t dest_node_id;
	if (net_world -> self_node_id == 1){
		dest_node_id = 2;
		
	}
	if (net_world -> self_node_id == 2){
		dest_node_id = 1;
	}

	ret = post_send_ctrl_net(net_world, &ctrl_message, 0, dest_node_id, 0);
	if (ret != 0){
		fprintf(stderr, "Error: could not post control message. From id: %u going to node id: %u\n", net_world -> self_node_id, dest_node_id);
		return -1;
	}

	// POLLING CONTROL RECV CQ ON DEVICE 0

	struct ibv_cq_ex *** cq_collection = net_world -> self_net -> cq_collection;

	int device_id = 0;
	int endpoint_type = CONTROL_ENDPOINT;
	struct ibv_cq_ex * control_cq = cq_collection[device_id][endpoint_type];

	uint64_t poll_duration_ns = 10 * 1e9;
	ret = poll_cq(control_cq, poll_duration_ns); 
	if (ret != 0){
		fprintf(stderr, "Error: failure polling device 0 control cq\n");
		return -1;
	}

	printf("Letting the rdma_init server stay running in case of more joins...\n");

	// Should Be Infinitely Blocking 
	// (unless error or shutdown message)
	ret = pthread_join(net_world -> tcp_rdma_init_server_thread, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: pthread_join failed for join server\n");
		return -1;
	}

	return 0;
}