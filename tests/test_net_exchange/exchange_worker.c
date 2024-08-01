#include "exchange_worker.h"

void * run_exchange_worker(void * _worker_thread_data) {
	

	int ret;

	// cast the type correctly
	Worker_Thread_Data * worker_thread_data = (Worker_Thread_Data *) _worker_thread_data;

	// General worker arguments

	int worker_thread_id = worker_thread_data -> worker_thread_id;

	printf("[Exchange Worker %d] Started!\n", worker_thread_id);

	// Exchange specific arguments
	Exchange_Worker_Data * exchange_worker_data = (Exchange_Worker_Data *) worker_thread_data -> worker_arg;
	Exchange * exchange = exchange_worker_data -> exchange;
	Net_World * net_world = exchange_worker_data -> net_world;

	

	// The main task queue that contains control messages routed to this worker_class
	//	- the ctrl_handler threads (which are polling the CQ's attached to each Control Endpoint QP)
	//		are responsible for producing tasks on this bounded buffer
	Fifo * tasks = worker_thread_data -> tasks;


	// If a benchmark was set

	Work_Bench * work_bench = *(worker_thread_data -> work_bench);


	// TODO! 

	// check the assigned device id and set thread affinity
	//	- ensures the "post_send_ctrl_net" response messages are low-latency
	//		- these are the match notifications that the exchange sends out


	Ctrl_Message * ctrl_messages = (Ctrl_Message *) malloc(tasks -> max_items * sizeof(Ctrl_Message));
	if (ctrl_messages == NULL){
		fprintf(stderr, "Error: malloc failed to allocate ctrl_messages buffer within exchange worker\n");
		return NULL;
	}

	Ctrl_Message ctrl_message;

	uint32_t num_triggered_response_ctrl_messages;
	Ctrl_Message * triggered_response_ctrl_messages;

	uint64_t num_consumed;

	while (1){



		// 1.) Receive task from fifo (and ensure it was meant for this thread)

		// generates a copy of the data that was in fifo
		//	- this data will be overwritten when the fifo wraps around
		num_consumed = consume_all_fifo(tasks, ctrl_messages);


		// Consume as many as possible and store in buffer to reduce lock contention on the fifo

		for (uint64_t i = 0; i < num_consumed; i++){

			ctrl_message = ctrl_messages[i];
			
			//printf("[Exchange Worker %d] Consumed a control message!\n", worker_thread_id);

			if (ctrl_message.header.message_class != EXCHANGE_CLASS){
				fprintf(stderr, "[Exchange Worker %d] Error: an exchange worker saw a task not with exchange class, but instead: %d\n", worker_thread_id, ctrl_message.header.message_class);
			}

			
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

			/*

			// 2.) Actually perform the task
			ret = do_exchange_function(exchange, &ctrl_message, &num_triggered_response_ctrl_messages, &triggered_response_ctrl_messages);
			if (ret != 0){
				fprintf(stderr, "[Exchange Worker %d] Error: do_exchange_function failed\n", worker_thread_id);
			}


			// 3. If there are any control messages that need be send out in response to some trigger, do so
			for (uint32_t i = 0; i < num_triggered_response_ctrl_messages; i++){
				ret = post_send_ctrl_net(net_world, &(triggered_response_ctrl_messages[i]));
				if (ret != 0){
					fprintf(stderr, "[Exchange Worker %d] Error: after do exchange function, was supposed to send %u triggered messages. But posting a send ctrl message for #%u failed\n", 
						worker_thread_id, num_triggered_response_ctrl_messages, i);
				}
			}


			// 4.) Clean up memory if necesary
			// generate_match_ctrl_messages() allocated memory if there were messages that needed to be sent, but now free
			// because the message informatioin got copied to the send control channel
			if (num_triggered_response_ctrl_messages > 0){
				free(triggered_response_ctrl_messages);
			}


			*/

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