#include "sys.h"
#include "fingerprint.h"
#include "exchange_client.h"
#include "utils.h"
#include "hsa_memory.h"
#include "self_net.h"

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
	uint64_t chunk_size = 1U << 21;
	uint64_t num_chunks = 1000;

	// should return a 2GB region
	ret = hsa_add_device_memory(hsa_memory, device_id, num_chunks, chunk_size);
	if (ret != 0){
		fprintf(stderr, "Error: failed to add device memory\n");
		return -1;
	}

	Hsa_User_Page_Table * user_page_table = hsa_memory -> user_page_table;
	printf("\nSuccessfully added device memory!\n\tDmabuf_fd for first chunk: %d\n\tDmabuf_offset for first chunk: %lu\n\n", 
				user_page_table -> dmabuf_fds[device_id][0], user_page_table -> dmabuf_offsets[device_id][0]);



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


	int num_ints = 100;


	struct ibv_pd * pd = (system -> net_world -> self_net -> dev_pds)[0];
	struct ibv_qp * qp;
	struct ibv_mr * mr;


	// Post receive request in GPU memory

	
	printf("Attempting to register DMA buf with verbs...\n");

	int mr_access = IBV_ACCESS_LOCAL_WRITE; 

	uint64_t iova = (uint64_t) user_page_table -> virt_memories[device_id];

	mr = ibv_reg_dmabuf_mr(pd, user_page_table -> dmabuf_offsets[device_id][0], chunk_size, iova, user_page_table -> dmabuf_fds[device_id][0], mr_access);
	if (mr == NULL){
		fprintf(stderr, "Error: ibv_reg_dmabuf_mr failed\n");
	}


	printf("Succeeded! Details:\n\tMR address: %p\n\tMR Lkey: %u\n\tDevice Virt Address: %p\n\n", 
				mr -> addr, mr -> lkey, user_page_table -> virt_memories[device_id]);


	
	printf("Attempting to post receive request for GPU memory...\n");

	qp = (net_world -> self_net -> self_node -> endpoints)[1].ibv_qp;

	ret = post_recv_work_request(qp, iova, chunk_size, mr -> lkey, 0);
	if (ret != 0){
		fprintf(stderr, "Error: unable to post recv request to the registered dma buf region\n");
		return -1;
	}

	printf("Successfully posted receive request waiting at GPU memory!\n");

	
	printf("Waiting to read the contents of sender from GPU memory...\n");

	sleep(5);

	int * int_buffer;

	ret = hsa_copy_to_host_memory(hsa_memory, device_id, (void *) (iova + sizeof(struct ibv_grh)), num_ints * sizeof(int), (void **) &int_buffer);
	if (ret != 0){
		fprintf(stderr, "Error failed to copy contents from device to host\n");
	}

	for (int i = 0; i < num_ints; i++){
		printf("%d\n", int_buffer[i]);
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