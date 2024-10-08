#include "sys.h"
#include "fingerprint.h"
#include "exchange_client.h"
#include "utils.h"
#include "self_net.h"


// Temporary
#include "hsa_memory.h"
#include "memory.h"
#include "rocblas_funcs.h"


#include "fast_table.h"
#include "fast_tree.h"

#include "memory_client.h"

int main(int argc, char * argv[]){


	struct timespec start, stop;
	uint64_t timestamp_start, timestamp_stop, elapsed_ns;

	int ret;
	
	char * master_ip_addr, * self_ip_addr;


	if ((argc != 2) && (argc != 3)){
		fprintf(stderr, "Error: Usage ./testWorker <master_ip_addr> <self_ip_addr>\n");
		return -1;
	}
	
	master_ip_addr = argv[1];
	self_ip_addr = NULL;
	if (argc == 3){
		self_ip_addr = argv[2];
	}


	// 1.) Initialize HSA memory

	printf("Intializing Backend Memory...\n");

	Hsa_Memory * hsa_memory = hsa_init_memory();
	if (hsa_memory == NULL){
		fprintf(stderr, "Error: hsa_init_memory failed\n");
	}

	printf("\nFound %d HSA agents each with a mempool!\n\n", hsa_memory -> n_agents);

	
	// 2 MB Chunk Size
	int device_id = 0;
	uint64_t chunk_size = 1U << 16;
	uint64_t num_chunks = 10000;

	// should return a 2GB region
	ret = hsa_add_device_memory(hsa_memory, device_id, num_chunks, chunk_size);
	if (ret != 0){
		fprintf(stderr, "Error: failed to add device memory\n");
		return -1;
	}


	// 2.) Intiialize common (across accelerator backends) system memory struct

	// currently this function is within hsa_memory.c, but would be nicer to rearrange things...
	Memory * memory =  init_memory(init_backend_memory(hsa_memory), SYS_MEM_NUM_CHUNKS, SYS_MEM_CHUNK_SIZE);
	if (memory == NULL){
		fprintf(stderr, "Error: failed to initialize memory\n");
		return -1;
	}


	printf("\n\nREQUESTING TO JOIN NETWORK & BRING SYSTEM ONLINE...!\n\n");

	System * system = init_system(master_ip_addr, self_ip_addr, memory);

	if (system == NULL){
		fprintf(stderr, "Error: failed to initialize system\n");
		return -1;
	}

	printf("\n\nSuccessfully initialized system! Was assigned Node ID: %u!\n\n", system -> net_world -> self_node_id);


	Net_World * net_world = system -> net_world;


	ExchMessageType exch_message_type;
	if (net_world -> self_node_id == 1){
		exch_message_type = BID_ORDER;
	}
	if (net_world -> self_node_id >= 2){
		exch_message_type = OFFER_ORDER;
	}

	uint64_t num_exchange_messages = 1000;
	
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

	// NOW SEND/RECV MESSAGES!


	// prepare all contorl messages
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	
	// Only send message from node 1

	uint64_t start_message_id = 0;

	uint64_t content_size = SYS_MEM_CHUNK_SIZE;

	for (uint64_t i = start_message_id; i < start_message_id + num_exchange_messages; i++){

		// do_fingerprinting populates an already allocated array
		do_fingerprinting(&i, sizeof(uint64_t), fingerprint, FINGERPRINT_TYPE);

		// submit exchange order copies the fingerprint contents into a control message
		ret = submit_exchange_order(system, fingerprint, exch_message_type, content_size, 0);
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

	Work_Bench * exchange_bench = (system -> work_pool -> classes)[EXCHANGE_CLASS] -> work_bench;

	uint64_t start_timestamp = (exchange_bench -> start).tv_sec * 1e9 +  (exchange_bench -> start).tv_nsec;
	uint64_t stop_timestamp = (exchange_bench -> stop).tv_sec * 1e9 +  (exchange_bench -> stop).tv_nsec;

	uint64_t elapsed_time_ns = stop_timestamp - start_timestamp;
	uint64_t num_tasks = (exchange_bench -> task_cnt_stop_bench) - (exchange_bench -> task_cnt_start_bench);

	uint64_t tasks_per_sec = (num_tasks * 1e9) / elapsed_time_ns; 

	printf("\n\n\nExchange Throughtput Stats:\n\tNumber of Requests: %lu\n\tElapsed Time (ns): %lu\n\tThroughput (requests/sec): %lu\n\n\n",
		num_tasks, elapsed_time_ns, tasks_per_sec);

	// Should Be Infinitely Blocking 
	// (unless error or shutdown message)
	ret = pthread_join(net_world -> tcp_rdma_init_server_thread, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: pthread_join failed for join server\n");
		return -1;
	}

	return 0;
}