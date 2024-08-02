#include "master_worker.h"



// NOT USING THIS FUNCTION FOR NOW!!!

// PROBABLY WANT TO REORGANIZE MASTER STRUCT AND PASS THAT AS INPUT TO THIS AS A WORKER ARG!


void * run_master_worker(void * _worker_thread_data) {
	

	int ret;

	// cast the type correctly
	Worker_Thread_Data * worker_thread_data = (Worker_Thread_Data *) _worker_thread_data;

	// General worker arguments

	int worker_thread_id = worker_thread_data -> worker_thread_id;

	printf("[Master Worker %d] Started!\n", worker_thread_id);

	// Exchange specific arguments
	Master_Worker_Data * master_worker_data = (Master_Worker_Data *) worker_thread_data -> worker_arg;
	Net_World * net_world = master_worker_data -> net_world;

	Ctrl_Message ctrl_message;


	// The main task queue that contains control messages routed to this worker_class
	//	- the ctrl_handler threads (which are polling the CQ's attached to each Control Endpoint QP)
	//		are responsible for producing tasks on this bounded buffer
	Fifo * tasks = worker_thread_data -> tasks;


	// If a benchmark was set
	Work_Bench * work_bench = *(worker_thread_data -> work_bench);

	while (1){



		// 1.) Receive task from fifo (and ensure it was meant for this thread)

		// generates a copy of the data that was in fifo
		//	- this data will be overwritten when the fifo wraps around
		consume_fifo(tasks, &ctrl_message);

		//printf("[Exchange Worker %d] Consumed a control message!\n", worker_thread_id);

		if (ctrl_message.header.message_class != MASTER_CLASS){
			fprintf(stderr, "[Master Worker %d] Error: a master worker saw a task not with master class, but instead: %d\n", worker_thread_id, ctrl_message.header.message_class);
		}



		// FOR NOW NOT ACTUALLY DOING ANYTHING
		//	- just testing send/recv throughputs

		// 2.) Actually perform the task

	}

	return NULL;
}