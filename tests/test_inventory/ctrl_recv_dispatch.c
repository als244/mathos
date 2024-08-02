#include "ctrl_recv_dispatch.h"


void * run_recv_ctrl_dispatcher(void * _ctrl_recv_dispatcher_thread_data) {

	Ctrl_Recv_Dispatcher_Thread_Data * dispatcher_thread_data = (Ctrl_Recv_Dispatcher_Thread_Data *) _ctrl_recv_dispatcher_thread_data;

	int ib_device_id = dispatcher_thread_data -> ib_device_id;
	Fifo * dispatcher_fifo = dispatcher_thread_data -> dispatcher_fifo;
	Work_Pool * work_pool = dispatcher_thread_data -> work_pool;
	Net_World * net_world = dispatcher_thread_data -> net_world;


	// Knowing what to modulus by when placing wokrer
	int num_workers_per_class[work_pool -> max_work_class_ind];
	for (int i = 0; i < work_pool -> max_work_class_ind; i++){
		if ((work_pool -> classes)[i] == NULL){
			num_workers_per_class[i] = 0;
		}
		else{
			num_workers_per_class[i] = (work_pool -> classes)[i] -> num_workers;
		}
	}

	// Deciding which worker fifo to place on
	uint64_t next_worker_id_by_class[work_pool -> max_work_class_ind];
	for (int i = 0; i < work_pool -> max_work_class_ind; i++){
		next_worker_id_by_class[i] = 0;
	}

	Work_Class ** work_classes = work_pool -> classes;


	Recv_Ctrl_Message * recv_ctrl_messages = (Recv_Ctrl_Message *) malloc(dispatcher_fifo -> max_items * sizeof(Recv_Ctrl_Message));
	if (recv_ctrl_messages == NULL){
		fprintf(stderr, "Error: malloc failed to allocate buffer for recv control messages within dispatcher thread\n");
		return NULL;
	}


	Ctrl_Message ctrl_message;

	Ctrl_Message_H ctrl_message_header;
	int control_message_class;

	Fifo ** worker_fifos;
	int next_worker_id;


	uint64_t num_consumed;

	while (1){

		// CONSUME THE FIFO PRODUCED BY RECV_CTRL_HANDLER

		num_consumed = consume_all_fifo(dispatcher_fifo, recv_ctrl_messages);


		// Consume as many as possible and store in buffer to reduce lock contention on the fifo

		for (uint64_t i = 0; i < num_consumed; i++){

			ctrl_message = recv_ctrl_messages[i].ctrl_message;

			// For each message, route it the appropriate work_class and worker within that class

			ctrl_message_header = ctrl_message.header;
						
			// Kinda ugly for printing		
			// printf("\n\n[Node %u] Received control message!\n\tSource Node ID: %u\n\tMessage Class: %s\n\n", 
			// 				net_world -> self_node_id, ctrl_message_header.source_node_id, message_class_to_str(ctrl_message_header.message_class));

			// all fifo buffers at at:
			// work_pool -> classes)[ctrl_message_header.message_class] -> tasks
						
			// Error check a valid work class to place message on proper task fifo
			control_message_class = ctrl_message_header.message_class;
			if (control_message_class > work_pool -> max_work_class_ind){
				fprintf(stderr, "Error: received message specifying message class %d, but declared maximum work class index of %d\n", control_message_class, work_pool -> max_work_class_ind);
				continue;
			}

			// Probably want to ensure there that the class has been added (and thus tasks is non-null)

			worker_fifos = work_classes[control_message_class] -> worker_tasks;

			next_worker_id = next_worker_id_by_class[control_message_class] % num_workers_per_class[control_message_class];
					
			produce_fifo(worker_fifos[next_worker_id], &ctrl_message);
		}
	}
}