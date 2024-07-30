#include "init_sys.h"
#include "utils.h"

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



	uint32_t self_node_id = net_world -> self_node_id;
	uint32_t dest_node_id;
	if (net_world -> self_node_id == 1){
		dest_node_id = 2;
	}
	if (net_world -> self_node_id == 2){
		dest_node_id = 1;
	}

	uint64_t num_exchange_messages = 1 << 4;

	// prepare all contorl messages

	Ctrl_Message * ctrl_messages_to_send = (Ctrl_Message *) malloc(num_exchange_messages * sizeof(Ctrl_Message));
	if (ctrl_messages_to_send == NULL){
		fprintf(stderr, "Error: malloc failed to allocate control message buffer for sending\n");
		return -1;
	}

	//char num_buf[20];
	for (uint64_t i = 0; i < num_exchange_messages; i++){
		ctrl_messages_to_send[i].header.source_node_id = self_node_id;
		ctrl_messages_to_send[i].header.dest_node_id = dest_node_id;
		ctrl_messages_to_send[i].header.message_class = EXCHANGE_CLASS;
		//uint64_to_str_with_comma(num_buf, i);
		//sprintf((char *) (ctrl_messages_to_send[i].contents), "I am message #%s!", num_buf);
		sprintf((char *) (ctrl_messages_to_send[i].contents), "I am message #%lu from node %u!", i, self_node_id);
	}


	printf("\n\n[Node %u] Sending %lu exchange messages to node id: %d...\n\n", net_world -> self_node_id, num_exchange_messages, dest_node_id);
	
	for (uint64_t i = 0; i < num_exchange_messages; i++){
		ret = post_send_ctrl_net(net_world, &(ctrl_messages_to_send[i]));
		if (ret != 0){
			fprintf(stderr, "Error: could not post control message #%lu (From id: %u going to node id: %u)\n", i, net_world -> self_node_id, dest_node_id);
			return -1;
		}
	}

	// all messages have been sent
	//	- meaning contents have been copied to the verbs registered buffer 
	//		(within with sending QP's send control channel's fifo -> buffer)
	// so can free these messages now
	free(ctrl_messages_to_send);

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