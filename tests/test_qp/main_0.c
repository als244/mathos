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

	printf("Network setup successful!\n");

	return 0;
}
