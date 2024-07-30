#include "init_sys.h"

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

	System * system = init_system(master_ip_addr, self_ip_addr);

	if (system == NULL){
		fprintf(stderr, "Error: failed to initialize system\n");
		return -1;
	}

	printf("\n\nSuccessfully initialized system!\n\n\n");


	Net_World * net_world = system -> net_world;

	Ctrl_Message ctrl_message;
	ctrl_message.header.source_node_id = net_world -> self_node_id;

	uint32_t dest_node_id;
	if (net_world -> self_node_id == 1){
		dest_node_id = 2;
		ctrl_message.header.message_class = EXCHANGE_CLASS;
		strcpy((char *) ctrl_message.contents, "Hello");
		
	}
	if (net_world -> self_node_id == 2){
		dest_node_id = 1;
		ctrl_message.header.message_class = EXCHANGE_CLASS;
		strcpy((char *) ctrl_message.contents, "World");
	}

	ctrl_message.header.dest_node_id = dest_node_id;


	uint64_t num_exchange_messages = 1000000;
	printf("\n\n[Node %u] Sending %lu exchange messages to node id: %d...\n\n", net_world -> self_node_id, num_exchange_messages, dest_node_id);
	
	
	for (uint64_t i = 0; i < num_exchange_messages; i++){
		ret = post_send_ctrl_net(net_world, &ctrl_message);
		if (ret != 0){
			fprintf(stderr, "Error: could not post control message. From id: %u going to node id: %u\n", net_world -> self_node_id, dest_node_id);
			return -1;
		}
	}

	// Wait for benchmark to finish before recording

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