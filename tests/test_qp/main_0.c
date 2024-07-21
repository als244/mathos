#include "setup_net.h"
#include "fingerprint.h"
#include "verbs_ops.h"

#define NUM_INTS 100

#define THIS_ID 0

int main(int argc, char * argv[]){


	int ret;

	/*
	TEST THIS FUNCTION!:

	Network * init_net(int self_id, int num_qp_types, QueuePairUsageType * qp_usage_types, bool * to_use_srq_by_type, int * num_qps_per_type, 
						int num_cq_types, CompletionQueueUsageType * cq_usage_types);

	*/

	int self_id = THIS_ID;
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

	Self_Net * self_net = net -> self_net;
	Node_Net * self_node = net -> self_net -> self_node;

	int device_ind = 0;
	int port_ind = 0;
	Port * port = (self_node -> ports)[device_ind][port_ind];

	uint8_t port_num = port -> port_num;
	uint16_t lid = port -> lid;

	union ibv_gid gid = port -> gid;
	// 16 bytes
	uint8_t * gid_raw = gid.raw;

	printf("Device %d, Port Index: %d\n\tPort Number: %d\n\tLID: %d\n\tGID: ", device_ind, port_ind, (int) port_num, (int) lid);
	print_hex(gid_raw, GID_NUM_BYTES);
	printf("\n\n");


	// all_gids node_id/device_id/port_num
	/* SAVING LOCAL GID
	FILE * gid_file = fopen("./all_gids/0/0/1.gid", "w");
	if (gid_file == NULL){
		fprintf(stderr, "Error: could not open gid file\n");
		return NULL;
	}
	fwrite(&gid, sizeof(gid), 1, gid_file);
	fclose(gid_file);
	*/


	QP *** queue_pairs = port -> qp_collection -> queue_pairs;

	QP * control_qp = queue_pairs[0][0];
	printf("Control QP (device %d):\n\tPort Num: %d\n\tQP Num: %u\n\tQKey: %u\n\n\n", device_ind, control_qp -> port_num, control_qp -> qp_num, control_qp -> qkey);
	QP * data_qp = queue_pairs[1][0];
	printf("Data QP (device %d):\n\tPort Num: %d\n\tQP Num: %u\n\tQKey: %u\n\t\n\n", device_ind, data_qp -> port_num, data_qp -> qp_num, data_qp -> qkey);

	union ibv_gid dgid;
	// Read the GID from node 1, dev 0, port 1
	FILE * dgid_file = fopen("./all_gids/1/0/1.gid", "r");
	fread(&dgid, sizeof(dgid), 1, dgid_file);
	fclose(dgid_file);

	
	struct ibv_ah_attr ah_attr;
	memset(&ah_attr, 0, sizeof(ah_attr));

	ah_attr.grh.dgid = dgid;
	ah_attr.grh.sgid_index = 0;
	ah_attr.is_global = 1;
	ah_attr.dlid = 0;
	ah_attr.port_num = 1;

	struct ibv_pd * dev_pd = (self_net -> dev_pds)[device_ind];
	struct ibv_ah *ah = ibv_create_ah(dev_pd, &ah_attr);
	if (ah == NULL){
		fprintf(stderr, "Error: could not create address handle\n");
		return -1;
	}


	// Get ready to send message

	struct ibv_qp * ctrl_qp = control_qp -> ibv_qp;
	struct ibv_qp_ex * ctrl_qp_ex = ibv_qp_to_qp_ex(control_qp -> ibv_qp);

	// 1.) register memory region
	struct ibv_mr * mr;
	int * buffer = malloc(NUM_INTS * sizeof(int));
	// populated buffer with NUM_INTS integers and get ready to send
	for (int i = 0; i < NUM_INTS; i++){
		buffer[i] = i;
	}
	ret = register_virt_memory(dev_pd, (void *) buffer, NUM_INTS * sizeof(int), &mr);
	if (ret != 0){
		fprintf(stderr, "Error: could not register memory region\n");
		return -1;
	}

	// 2.) Transition qp into correct stages (do i need to do this for UD queue pairs..??)

	// first go to INIT, then RTS, then to RTS
	struct ibv_qp_attr mod_attr;
	memset(&mod_attr, 0, sizeof(mod_attr));

	// transition from reset to init
	mod_attr.qp_state = IBV_QPS_INIT;
	mod_attr.pkey_index = 0;
	mod_attr.port_num = 1;
	mod_attr.qkey = 0;
	ret = ibv_modify_qp(ctrl_qp, &mod_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY);
	if (ret != 0){
		fprintf(stderr, "Error: could not move QP to Init state\n");
		return -1;
	}

	// now transition to RTR
	mod_attr.qp_state = IBV_QPS_RTR;
	ret = ibv_modify_qp(ctrl_qp, &mod_attr, IBV_QP_STATE);
	if (ret != 0){
		fprintf(stderr, "Error: could not move QP to Ready-to-Receive state\n");
		return -1;
	}

	// now go to RTS state
	mod_attr.qp_state = IBV_QPS_RTS;
	mod_attr.sq_psn = 0;
	ret = ibv_modify_qp(ctrl_qp, &mod_attr, IBV_QP_STATE | IBV_QP_SQ_PSN);
	if (ret != 0){
		fprintf(stderr, "Error: could not move QP to Ready-to-Send state\n");
		return -1;
	}


	// 3.) Send send message

	uint64_t send_wr_id = 3;

	ibv_wr_start(ctrl_qp_ex);

	ctrl_qp_ex -> wr_id = send_wr_id;
	ctrl_qp_ex -> wr_flags = 0;

	// PEFORM Send
    ibv_wr_send(ctrl_qp_ex);

    // Now add details of the send

	// Location of Send Data / Length / MR Key
	uint32_t length = NUM_INTS * sizeof(int);
	ibv_wr_set_sge(ctrl_qp_ex, mr -> lkey, (uint64_t) buffer, length);

	// UD Details (Address Header/Remote QP Num)
	uint32_t remote_qpn = 4449;
	uint32_t remote_qkey = 0;
	ibv_wr_set_ud_addr(ctrl_qp_ex, ah, remote_qpn, remote_qkey);

	// call complete to actually send message
	ret = ibv_wr_complete(ctrl_qp_ex);

	if (ret != 0){
		fprintf(stderr, "Error: issue with ibv_wr_complete: %d\n", ret);
		return -1;
	}

	// 4.) Block to ensure message sent
	CQ_Collection * cq_collection = (self_net -> dev_cq_collections)[device_ind];
	CQ * send_cq = (cq_collection -> send_cqs)[0];
	struct ibv_cq_ex * ibv_send_cq = send_cq -> ibv_cq;
	ret = block_for_wr_comp(ibv_send_cq, send_wr_id);
	if (ret != 0){
		fprintf(stderr, "Error: could not block for wr completion\n");
		return -1;
	}

	printf("Network setup and sending UD message successful!\n");
	
	return 0;
}
