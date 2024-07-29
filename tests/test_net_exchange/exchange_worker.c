#include "exchange_worker.h"

void * run_exchange_worker(void * _worker_thread_data) {
	

	int ret;

	// cast the type correctly
	Worker_Thread_Data * worker_thread_data = (Worker_Thread_Data *) _worker_thread_data;

	// General worker arguments

	int worker_thread_id = worker_thread_data -> worker_thread_id;

	printf("[Exchange Worker %d] Started!\n", worker_thread_id);

	Fifo * tasks = worker_thread_data -> tasks;

	// Exchange specific arguments
	Exchange_Worker_Data * exchange_worker_data = (Exchange_Worker_Data *) worker_thread_data -> worker_arg;
	Exchange * exchange = exchange_worker_data -> exchange;
	Net_World * net_world = exchange_worker_data -> net_world;

	Ctrl_Message * ctrl_message;

	uint32_t num_triggered_response_ctrl_messages;
	Ctrl_Message * triggered_response_ctrl_messages;

	while (1){



		// 1.) Receive task from fifo (and ensure it was meant for this thread)

		// generates a copy of the data that was in fifo
		//	- this data will be overwritten when the fifo wraps around
		ctrl_message = (Ctrl_Message *) consume_fifo(tasks);

		printf("[Exchange Worker %d] Consumed a control message!\n", worker_thread_id);

		if (ctrl_message -> header.message_class != EXCHANGE_CLASS){
			fprintf(stderr, "[Exchange Worker %d] Error: an exchange worker saw a task not with exchange class, but instead: %d\n", worker_thread_id, ctrl_message -> header.message_class);
		}

		// 2.) Actually perform the task
		ret = do_exchange_function(exchange, ctrl_message, &num_triggered_response_ctrl_messages, &triggered_response_ctrl_messages);
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


		// 4.) Clean up memory
		free(ctrl_message);
		// generate_match_ctrl_messages() allocated memory if there were messages that needed to be sent, but now free
		// because the message informatioin got copied to the send control channel
		if (num_triggered_response_ctrl_messages > 0){
			free(triggered_response_ctrl_messages);
		}
	}

	return NULL;
}