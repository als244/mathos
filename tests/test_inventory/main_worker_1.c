#include "sys.h"
#include "fingerprint.h"
#include "exchange_client.h"
#include "utils.h"
#include "hsa_memory.h"
#include "self_net.h"


// Temporary
#include "rocblas_funcs.h"

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


	/* QUICK AND DIRTY SPOT TO TEST HSA STUFF! */

	

	printf("\n\nREQUESTING TO JOIN NETWORK & BRING SYSTEM ONLINE...!\n\n");

	System * system = init_system(master_ip_addr, self_ip_addr);

	if (system == NULL){
		fprintf(stderr, "Error: failed to initialize system\n");
		return -1;
	}

	printf("\n\nSuccessfully initialized system! Was assigned Node ID: %u!\n\n", system -> net_world -> self_node_id);


	Net_World * net_world = system -> net_world;


	ExchMessageType exch_message_type;
	if (net_world -> self_node_id == 1){
		exch_message_type = OFFER_ORDER;
	}
	if (net_world -> self_node_id >= 2){
		exch_message_type = BID_ORDER;
	}

	uint64_t num_exchange_messages = 10;
	
	// Starting benchmark at count 0 means it will set the start timestamp upon first message
	ret = add_message_class_benchmark(system, EXCHANGE_CLASS, 0, num_exchange_messages);
	if (ret != 0){
		fprintf(stderr, "Error: failed to add benchmark to track work class throughput\n");
		return -1;
	}



	// Actually start all threads!
	//	- this call blocks until min_init_nodes (set within master) have been added to the net_world table
	printf("\n\nSpawning all worker threads & waiting until the minimum number of nodes (%u) have joined the net...\n\n", net_world -> min_init_nodes);

	ret = start_system(system);
	if (ret != 0){
		fprintf(stderr, "Error: failed to start system\n");
		return -1;
	}



	printf("\n\nSuccessfully started system. Everything online and ready to process...!\n\n");


	// 1.) Initialize HSA memory

	printf("Intializing HSA memory container...\n");

	Hsa_Memory * hsa_memory = hsa_init_memory();
	if (hsa_memory == NULL){
		fprintf(stderr, "Error: hsa_init_memory failed\n");
	}

	printf("Found %d HSA agents each with a mempool!\n\n\n", hsa_memory -> n_agents);


	printf("Adding Device Memory and Preparing it for Verbs region...\n");


	// 2 MB Chunk Size
	int device_id = 0;
	uint64_t chunk_size = 1U << 16;
	uint64_t num_chunks = 1000;

	// should return a 2GB region
	ret = hsa_add_device_memory(hsa_memory, device_id, num_chunks, chunk_size);
	if (ret != 0){
		fprintf(stderr, "Error: failed to add device memory\n");
		return -1;
	}

	Hsa_User_Page_Table * user_page_table = hsa_memory -> user_page_table;

	unsigned int cu_count = 82;
	hipStream_t stream;
	ret = initialize_stream(device_id, cu_count, &stream);
	if (ret != 0){
		fprintf(stderr, "Error: failure to initialize hip stream\n");
		return -1;
	}


	int num_floats = 100;
	
	struct ibv_pd * pd = (system -> net_world -> self_net -> dev_pds)[0];
	struct ibv_qp * qp;
	struct ibv_mr * mr;

	int mr_access = IBV_ACCESS_LOCAL_WRITE;


	// TEMPORARY SOLUTION OF SPECIFIYING CHUNK_ID (for testing)
	void * dptr = hsa_reserve_memory(hsa_memory, device_id, 0);
	if (dptr == NULL){
		fprintf(stderr, "Error: failed to reserve memory on device %d of size %lu\n", device_id, chunk_size);
	}


	mr = ibv_reg_mr(pd, dptr, chunk_size, mr_access);
	if (mr == NULL){
		fprintf(stderr, "Error: ibv_reg_mr failed\n");
		return -1;
	}


	printf("Succeeded! Details:\n\tMR address: %p\n\tMR Lkey: %u\n\tDevice GEM Address: %p\n\n", 
				mr -> addr, mr -> lkey, (void *)(uintptr_t) dptr);


	
	printf("Attempting to post receive request for GPU memory...\n");

	qp = (net_world -> self_net -> self_node -> endpoints)[1].ibv_qp;

	ret = post_recv_work_request(qp, (uint64_t) dptr, chunk_size, mr -> lkey, 0);
	if (ret != 0){
		fprintf(stderr, "Error: unable to post recv request to the registered dma buf region\n");
		return -1;
	}

	printf("Successfully posted receive request waiting at GPU memory!\n");

	
	printf("Waiting to read the contents of sender from GPU memory. Blocking until work completion..\n");

	ret = block_for_wr_comp((net_world -> self_net -> cq_recv_collection)[0][1], 0);
	if (ret != 0){
		fprintf(stderr, "Error: unable to block for wr completion\n");
	}


	printf("Attempting to copy contents from GPU to CPU...\n");
	
	
	void * dptr_real = (void *) ((uint64_t) dptr + sizeof(struct ibv_grh));
	void * buffer;
	ret = hsa_copy_to_host_memory(hsa_memory, device_id, dptr_real, num_floats * sizeof(float), (void **) &buffer);
	if (ret != 0){
		fprintf(stderr, "Error failed to copy contents from device to host\n");
	}
	

	float * float_buffer = (float *) buffer;

	for (int i = 0; i < num_floats; i++){
		printf("%f\n", float_buffer[i]);
	}

	printf("\n\nDOING GPU MATMUL ON RECEIVED DATA!\n\n");


	// TEMPORARY SOLUTION OF SPECIFIYING CHUNK_ID (for testing)
	void * out_dptr = hsa_reserve_memory(hsa_memory, device_id, 1);
	if (dptr == NULL){
		fprintf(stderr, "Error: failed to reserve memory on device %d of size %lu\n", device_id, chunk_size);
	}



	uint64_t elapsed_ns;


	int m = 10;
	int k = 10;
	int n = 10;

	ret = do_rocblas_matmul(stream, m, k, n, dptr_real, dptr_real, out_dptr, &elapsed_ns);
	if (ret != 0){
		fprintf(stderr, "Error: doing matmul failed\n");
		return -1;
	}

	void * out_buffer;
	ret = hsa_copy_to_host_memory(hsa_memory, device_id, out_dptr, m * n * sizeof(float), (void **) &out_buffer);
	if (ret != 0){
		fprintf(stderr, "Error failed to copy contents from device to host\n");
	}


	float * out_ptr_casted = (float *) out_buffer;
	for (int i = 0; i < 10; i++){
		for (int j = 0; j < 10; j++){
			printf("%f ", out_ptr_casted[i]);
		}
		printf("\n");
	}


	exit(0);









	// NOW SEND/RECV MESSAGES!


	// prepare all contorl messages
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	
	// Only send message from node 1

	uint64_t start_message_id = 0;
	for (uint64_t i = start_message_id; i < start_message_id + num_exchange_messages; i++){

		// do_fingerprinting populates an already allocated array
		do_fingerprinting(&i, sizeof(uint64_t), fingerprint, FINGERPRINT_TYPE);

		// submit exchange order copies the fingerprint contents into a control message
		ret = submit_exchange_order(system, fingerprint, exch_message_type);
		if (ret != 0){
			fprintf(stderr, "Error: failure to submit exchange order\n");
			return -1;
		}
	}
	

	// Wait for benchmark to finish before recording


	// THIS BENCHMARK IS FOR RECEIVING 1,000,000 exchange class messages

	Deque * are_benchmarks_ready = system -> are_benchmarks_ready;

	sem_t * cur_bench_ready;
	ret = take_deque(are_benchmarks_ready, FRONT_DEQUE, (void **) &cur_bench_ready);
	while (cur_bench_ready != NULL){
		sem_wait(cur_bench_ready);
		ret = take_deque(are_benchmarks_ready, FRONT_DEQUE, (void **) &cur_bench_ready);
	}


	// Now all benchmarks have finished so we can read the values
	// for now just reading the exchange class values

	/*
	Work_Bench * exchange_bench = (system -> work_pool -> classes)[EXCHANGE_CLASS] -> work_bench;

	uint64_t start_timestamp = (exchange_bench -> start).tv_sec * 1e9 +  (exchange_bench -> start).tv_nsec;
	uint64_t stop_timestamp = (exchange_bench -> stop).tv_sec * 1e9 +  (exchange_bench -> stop).tv_nsec;

	uint64_t elapsed_time_ns = stop_timestamp - start_timestamp;
	uint64_t num_tasks = (exchange_bench -> task_cnt_stop_bench) - (exchange_bench -> task_cnt_start_bench);

	uint64_t tasks_per_sec = (num_tasks * 1e9) / elapsed_time_ns; 

	printf("\n\n\nExchange Throughtput Stats:\n\tNumber of Requests: %lu\n\tElapsed Time (ns): %lu\n\tThroughput (requests/sec): %lu\n\n\n",
		num_tasks, elapsed_time_ns, tasks_per_sec);
	*/

	// Should Be Infinitely Blocking 
	// (unless error or shutdown message)
	ret = pthread_join(net_world -> tcp_rdma_init_server_thread, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: pthread_join failed for join server\n");
		return -1;
	}

	return 0;
}