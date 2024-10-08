#include "inventory_worker.h"


void * run_inventory_worker(void * _worker_thread_data) {

	int ret;

	// cast the type correctly
	Worker_Thread_Data * worker_thread_data = (Worker_Thread_Data *) _worker_thread_data;

	// General worker arguments

	int worker_thread_id = worker_thread_data -> worker_thread_id;

	

	// Exchange specific arguments
	Inventory_Worker_Data * inventory_worker_data = (Inventory_Worker_Data *) worker_thread_data -> worker_arg;
	Inventory * inventory = inventory_worker_data -> inventory;
	Net_World * net_world = inventory_worker_data -> net_world;

	printf("[Node %u: Inventory Worker -- %d] Started!\n", net_world -> self_node_id, worker_thread_id);
	

	// The main task queue that contains control messages routed to this worker_class
	//	- the ctrl_handler threads (which are polling the CQ's attached to each Control Endpoint QP)
	//		are responsible for producing tasks on this bounded buffer
	Fifo * tasks = worker_thread_data -> tasks;


	// If a benchmark was set

	Work_Bench * work_bench = *(worker_thread_data -> work_bench);


	uint64_t size = tasks -> max_items * sizeof(Ctrl_Message);
	printf("Inventory worker allocating size: %lu\n", size);

	Ctrl_Message * ctrl_messages = (Ctrl_Message *) malloc(tasks -> max_items * sizeof(Ctrl_Message));
	if (ctrl_messages == NULL){
		fprintf(stderr, "Error: malloc failed to allocate ctrl_messages buffer within inventory worker\n");
		return NULL;
	}

	Ctrl_Message ctrl_message;
	Ctrl_Message_H ctrl_message_header;

	uint32_t num_triggered_response_ctrl_messages;
	Ctrl_Message * triggered_response_ctrl_messages;

	uint64_t num_consumed;

	Inventory_Message * inventory_message;

	uint64_t total_consumed = 0;

	while (1){



		// 1.) Receive task from fifo (and ensure it was meant for this thread)

		// generates a copy of the data that was in fifo
		//	- this data will be overwritten when the fifo wraps around
		num_consumed = consume_all_fifo(tasks, ctrl_messages);

		total_consumed += num_consumed;

		printf("Inventory worker: CONSUMED %lu messages\n", total_consumed);


		// Consume as many as possible and store in buffer to reduce lock contention on the fifo

		for (uint64_t i = 0; i < num_consumed; i++){

			ctrl_message = ctrl_messages[i];

			Ctrl_Message_H ctrl_message_header = ctrl_message.header;
			
			//printf("[Inventory Worker %d] Consumed a control message!\n", worker_thread_id);

			if (ctrl_message_header.message_class != INVENTORY_CLASS){
				fprintf(stderr, "[Inventory Worker %d] Error: an inventory worker saw a task not with inventory class, but instead: %d\n", worker_thread_id, ctrl_message_header.message_class);
			}

			// within inventory.c
			//print_inventory_message(net_world -> self_node_id, INVENTORY_WORKER, worker_thread_id, &ctrl_message);

			// 1b.) Possibly need to start recording for benchmark
			if ((work_bench != NULL) && (!work_bench -> started)){
				pthread_mutex_lock(&(work_bench -> task_cnt_lock));
				// still need ensure that a different thread didn't mark as started before we acuired lock
				if ((!work_bench -> started) && (work_bench -> task_cnt == work_bench -> task_cnt_start_bench)){
					clock_gettime(CLOCK_MONOTONIC, &(work_bench -> start));
					work_bench -> started = true;
				}
				pthread_mutex_unlock(&(work_bench -> task_cnt_lock));
			}


			// FOR NOW NOT ACTUALLY DOING ANYTHING
			//	- just testing send/recv throughputs


			// 2.) Actually perform the task
			ret = do_inventory_function(inventory, INVENTORY_WORKER, worker_thread_id, &ctrl_message, &num_triggered_response_ctrl_messages, &triggered_response_ctrl_messages);
			if (ret != 0){
				fprintf(stderr, "[Inventory Worker %d] Error: do_inventory_function failed\n", worker_thread_id);
			}


			/*
			// 3. If there are any control messages that need be send out in response to some trigger, do so
			for (uint32_t i = 0; i < num_triggered_response_ctrl_messages; i++){
				// Ensure to not post to self and instead directly pass to proper function
				if (triggered_response_ctrl_messages[i].header.dest_node_id != net_world -> self_node_id){
					ret = post_send_ctrl_net(net_world, &(triggered_response_ctrl_messages[i]));
					if (ret != 0){
						fprintf(stderr, "[Inventory Worker %d] Error: after do inventory function, was supposed to send %u triggered messages. But posting a send ctrl message for #%u failed\n", 
						worker_thread_id, num_triggered_response_ctrl_messages, i);
					}
				}
				else{

					// TODO: actually call function to process this self-directed message
					if (triggered_response_ctrl_messages[i].header.message_class == INVENTORY_CLASS){
						print_inventory_message(net_world -> self_node_id, INVENTORY_WORKER, worker_thread_id, &(triggered_response_ctrl_messages[i]));
					}
				}
			}
			*/

			// 4.) Clean up memory if necesary
			// generate_match_ctrl_messages() allocated memory if there were messages that needed to be sent, but now free
			// because the message informatioin got copied to the send control channel
			if (num_triggered_response_ctrl_messages > 0){
				free(triggered_response_ctrl_messages);
			}


			// 5.) If we have work_bench set, do bookeeping
			//			- Indicate completed task
			
			if ((work_bench != NULL) && (!work_bench -> stopped)){
				pthread_mutex_lock(&(work_bench -> task_cnt_lock));
				// increment count
				work_bench -> task_cnt += 1;
				// because we are changing count while holding lock, don't need to recheck the stopped flag
				//	(only 1 thread will be exactly equal if all threads are changing the value)
				if (work_bench -> task_cnt == work_bench -> task_cnt_stop_bench){
					clock_gettime(CLOCK_MONOTONIC, &(work_bench -> stop));
					work_bench -> stopped = true;
					sem_post(&(work_bench -> is_bench_ready));
				}
				pthread_mutex_unlock(&(work_bench -> task_cnt_lock));
			}


			// COULD ALSO USE GCC ATOMICS
			// Ref: https://gcc.gnu.org/onlinedocs/gcc-4.8.2/gcc/_005f_005fatomic-Builtins.html
			//	- __atomic_fetch_add(shared_task_cnt, 1, __ATOMIC_SEQ_CST);

		}


	}

	return NULL;




}