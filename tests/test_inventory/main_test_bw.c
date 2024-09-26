#include "sys.h"
#include "fingerprint.h"
#include "exchange_client.h"
#include "utils.h"
#include "self_net.h"

#define MAX_DATA_PER_MSG 1000

int main(int argc, char * argv[]){

	int ret;
	
	if ((argc != 3) && (argc != 4)){
		fprintf(stderr, "Error: Usage ./testWorker <master_ip_addr> <n_bytes> [<self_ip_addr>] \n");
		return -1;
	}
	
	char * master_ip_addr = argv[1];
	size_t n_bytes = atol(argv[2]);

	char * self_ip_addr = NULL;
	if (argc == 4){
		self_ip_addr = argv[3];
	}


	printf("\n\nREQUESTING TO JOIN NETWORK & BRING SYSTEM ONLINE...!\n\n");

	System * system = init_system(master_ip_addr, self_ip_addr, NULL);

	if (system == NULL){
		fprintf(stderr, "Error: failed to initialize system\n");
		return -1;
	}

	printf("\n\nSuccessfully initialized system! Was assigned Node ID: %u!\n\n", system -> net_world -> self_node_id);


	Net_World * net_world = system -> net_world;


	// Actually start all threads!
	//	- this call blocks until min_init_nodes (set within master) have been added to the net_world table
	printf("\n\nSpawning all worker threads & waiting until the minimum number of nodes (%u) have joined the net...\n\n", net_world -> min_init_nodes);

	ret = start_system(system);
	if (ret != 0){
		fprintf(stderr, "Error: failed to start system\n");
		return -1;
	}


	printf("\n\nSuccessfully started system. Everything online and ready to process...!\n\n");


	struct ibv_pd * pd = (system -> net_world -> self_net -> dev_pds)[0];
	// use the "data" qp
	struct ibv_qp * qp = (system -> net_world -> self_net -> self_node -> endpoints)[1].ibv_qp;
	struct ibv_mr * mr;
	struct ibv_mr * complete_mr;

	// use receive queue on 0th port for "data" (which is tied to recv not srq)
	struct ibv_cq_ex * cq = (net_world -> self_net -> cq_recv_collection)[0][1];

	



	void * complete = malloc(1 + sizeof(struct ibv_grh));
	ret = register_virt_memory(pd, (void *) complete, 1 + sizeof(struct ibv_grh), &complete_mr);
	if (ret != 0){
		fprintf(stderr, "Error: failed to register int buffer region in system mem on sender side\n");
		return -1;
	}

	uint32_t complete_lkey = complete_mr -> lkey;

	uint64_t num_items = MY_CEIL(n_bytes, MAX_DATA_PER_MSG);
	uint64_t item_length = MAX_DATA_PER_MSG;
	uint64_t wr_id_start = 1;


	uint8_t * data;
	uint32_t lkey;
	uint32_t remote_node_id;
	uint32_t remote_qp_num;
	uint32_t remote_qkey;
	uint32_t remote_node_port_ind;
	struct ibv_ah * ah;

	Net_Node target_node;
	Net_Node * remote_node;

	struct timespec start, stop;

	// sender
	if (net_world -> self_node_id == 1){

		printf("Sleeping to let other side post recvs...\n");
		// Wait for other node to prepare memory and post recv
		sleep(2);

		data = malloc(num_items * item_length);
		ret = register_virt_memory(pd, (void *) data, num_items * item_length, &mr);
		if (ret != 0){
			fprintf(stderr, "Error: failed to register int buffer region in system mem on sender side\n");
			return -1;
		}

		lkey = mr -> lkey;

		for (uint64_t i = 0; i < n_bytes; i++){
			data[i] = 1;
		}

		remote_node_id = 2;
		target_node.node_id = remote_node_id;

		remote_node = find_item_table(net_world -> nodes, &target_node);
		if (remote_node == NULL){
			fprintf(stderr, "Error: could not find node 1 in table\n");
			return -1;
		}

		remote_qp_num = (remote_node -> endpoints)[1].remote_qp_num;
		remote_qkey = (remote_node -> endpoints)[1].remote_qkey;

		remote_node_port_ind = (remote_node -> endpoints)[1].remote_node_port_ind;

		ah = (remote_node -> ports)[remote_node_port_ind].address_handles[0];

		ret = post_recv_work_request(qp, (uint64_t) complete, 1 + sizeof(struct ibv_grh), complete_lkey, 0);
		if (ret != 0){
			fprintf(stderr, "Error: failure to post wr request for hearing back when complete\n");
			return -1;
		}


		printf("Sending %lu bytes...\n\n\tTo remote_node: %u\n\tRemote Qp Num: %u\n\tRemote Qkey: %u\n\tRemote Port Ind: %u\n\n", 
					n_bytes, remote_node_id, remote_qp_num, remote_qkey, remote_node_port_ind);
		

		struct timespec post_send; 
		clock_gettime(CLOCK_REALTIME, &start);
		ret = post_send_batch_work_request(qp, num_items, (uint64_t) data, item_length, lkey, wr_id_start, ah, remote_qp_num, remote_qkey);
		if (ret != 0){
			fprintf(stderr, "Error: failure to send batch of data\n");
			return -1;
		}
		clock_gettime(CLOCK_REALTIME, &post_send);

		printf("Posted sends! Now blocking for confirmation...\n");

		ret = block_for_wr_comp(cq, 0);
		clock_gettime(CLOCK_REALTIME, &stop);

		if (ret != 0){
			fprintf(stderr, "Error: failure to block for completed wr id\n");
			return -1;
		}

		uint64_t timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
		uint64_t timestamp_post_send = post_send.tv_sec * 1e9 + post_send.tv_nsec;
		uint64_t timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;

		uint64_t elapsed_send_ns = timestamp_post_send - timestamp_start;
		uint64_t elapsed_ns = timestamp_stop - timestamp_start;

		double throughput_gb_sec = (double) (8 * n_bytes) / (double) elapsed_ns;

		printf("\nStats:\n\tData Size: %lu\n\tTimestamp Start: %lu\n\tTimestamp Post Send: %lu\n\tTimestamp Confirm: %lu\n\tElapsed Time Send (ns): %lu\n\tElapsed Time Confirm (ns): %lu\n\tThroughput Gb/s: %.3f\n\n", n_bytes, timestamp_start, timestamp_post_send, timestamp_stop, elapsed_send_ns, elapsed_ns, throughput_gb_sec);
	}

	// receiver
	if (net_world -> self_node_id == 2){

		item_length += sizeof(struct ibv_grh);
		data = malloc(num_items * item_length);
		ret = register_virt_memory(pd, (void *) data, num_items * item_length, &mr);
		if (ret != 0){
			fprintf(stderr, "Error: failed to register int buffer region in system mem on sender side\n");
			return -1;
		}

		remote_node_id = 1;
		target_node.node_id = remote_node_id;

		remote_node = find_item_table(net_world -> nodes, &target_node);
		if (remote_node == NULL){
			fprintf(stderr, "Error: could not find node 1 in table\n");
			return -1;
		}

		remote_qp_num = (remote_node -> endpoints)[1].remote_qp_num;
		remote_qkey = (remote_node -> endpoints)[1].remote_qkey;

		remote_node_port_ind = (remote_node -> endpoints)[1].remote_node_port_ind;

		ah = (remote_node -> ports)[remote_node_port_ind].address_handles[0];


		lkey = mr -> lkey;

		printf("Receiving %lu bytes...\n", n_bytes);

		ret = post_recv_batch_work_requests(qp, num_items, (uint64_t) data, item_length, lkey, 1);
		if (ret != 0){
			fprintf(stderr, "Error: unable to post recv batch request\n");
			return -1;
		}

		printf("Blocking for %lu completitions...\n", num_items);

		uint64_t seen_completitions = block_for_batch_wr_comp(cq, num_items);
		if (seen_completitions != num_items){
			fprintf(stderr, "Error: unable to find correct number of completitions (%lu != %lu)\n", seen_completitions, num_items);
			return -1;
		}
		clock_gettime(CLOCK_REALTIME, &stop);

		uint64_t timestamp_recv = stop.tv_sec * 1e9 + stop.tv_nsec;
		printf("Finished receiving data at time: %lu\n\n", timestamp_recv);

		printf("Sending confirmation to original sender:\n\tTo remote_node: %u\n\tRemote Qp Num: %u\n\tRemote Qkey: %u\n\tRemote Port Ind: %u\n\n", 
					remote_node_id, remote_qp_num, remote_qkey, remote_node_port_ind);

		// doesn't matter the contents, but we are sending 1 byte to trigger the recv completition
		ret = post_send_work_request(qp, (uint64_t) complete, 1, complete_lkey, 0, ah, remote_qp_num, remote_qkey);
		if (ret != 0){
			fprintf(stderr, "Error: failure to post send wr request for confirmation\n");
			return -1;
		}
	}

	return 0;
}