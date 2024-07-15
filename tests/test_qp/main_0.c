#include "setup_net.h"

int main(int argc, char * argv[]){


	int ret;

	/*
	TEST THIS FUNCTION!:

	Network * init_net(int self_id, int num_qp_types, QueuePairUsageType * qp_usage_types, bool * to_use_srq_by_type, int * num_qps_per_type, 
						int num_cq_types, CompletionQueueUsageType * cq_usage_types);

	*/

	int self_id = 0;
	int num_qp_types = 2;
	QueuePairUsageType qp_usage_types[2] = {CONTROL_QP, DATA_QP};
	bool to_use_srq_by_type[2] = {true, false};
	int num_qps_per_type[2] = {1, 2};

	int num_cq_types = 2;
	CompletionQueueUsageType cq_usage_types[2] = {CONTROL_CQ, DATA_CQ};


	Network * net = init_net(self_id, num_qp_types, qp_usage_types, to_use_srq_by_type, num_qps_per_type, num_cq_types, cq_usage_types);

	if (net == NULL){
		fprintf(stderr, "Error: could not initialize network\n");
		return -1;
	}

	Node_Net * self_node = net -> self_net -> self_node;

	int device_ind = 0;
	int port_ind = 0;
	Port * port = (self_node -> ports)[device_ind][port_ind];

	uint8_t port_num = port -> port_num;
	uint16_t lid = port -> lid;

	printf("Device %d, Port Index: %d\n\tPort Number: %d\n\tLID: %d\n\n", device_ind, port_ind, (int) port_num, (int) lid);

	QP *** queue_pairs = port -> qp_collection -> queue_pairs;

	QP * control_qp = queue_pairs[0][0];
	printf("Control QP (device %d):\n\tPort Num: %d\n\tQP Num: %u\n\tQKey: %u\n\n\n", device_ind, control_qp -> port_num, control_qp -> qp_num, control_qp -> qkey);
	QP * data_qp = queue_pairs[1][0];
	printf("Data QP (device %d):\n\tPort Num: %d\n\tQP Num: %u\n\tQKey: %u\n\t\n\n", device_ind, data_qp -> port_num, data_qp -> qp_num, data_qp -> qkey);

	printf("Network setup successful!\n");

	return 0;
}
