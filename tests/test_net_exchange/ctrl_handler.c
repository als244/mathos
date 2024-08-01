#include "ctrl_handler.h"

void * run_send_ctrl_handler(void * _cq_thread_data){

	int ret;

	// 1.) determine what cq this thread is working on

	// IN REALITY THIS WILL CONTAIN MORE THAN JUST NET_WORLD
	// IT SHOULD CONNECT TO WORKER POOL
	// (THAT CONNECTS TO EXCHANGE AND SCHED AS WELL!)
	// GOING TO PASS ALL CONTROL MESSAGES TO APPROPRIATE WORKER!
	Cq_Thread_Data * cq_thread_data = (Cq_Thread_Data *) _cq_thread_data;

	// needed to find the CQ this thread is supposed to work on! 
	struct ibv_cq_ex * cq_ex = cq_thread_data -> cq;
	Net_World * net_world = cq_thread_data -> net_world;
	Self_Net * self_net = net_world -> self_net;

	struct ibv_cq * cq = ibv_cq_ex_to_cq(cq_ex);

	enum ibv_wc_status status;
	uint64_t wr_id;

	
		
	// these will get populated upon extracting an item from channel
	

	// For now doing an infnite loop unless error....
	// Burns a lot of cpu....
	//	- but want low latency!!!
	//	- many, many exchange requests per sec


	int num_comp;

	int max_poll_entries = SEND_CTRL_MAX_POLL_ENTRIES;

	struct ibv_wc work_completions[max_poll_entries];

	

	Ctrl_Channel * ctrl_channel;

	Ctrl_Message ctrl_message;
	Ctrl_Message_H ctrl_message_header;


	while (1){

		// wait for an entry
		do {
			num_comp = ibv_poll_cq(cq, max_poll_entries, work_completions);
		} while (num_comp == 0);


		// Iterate over all sends, and see which QP's send control channel needs to be consumed
		
		for (int i = 0; i < num_comp; i++){

			// Consume the completed work request
			wr_id = work_completions[i].wr_id;
			status = work_completions[i].status;
				
			// This message is kinda ugly. Can be helpful with errors
			// printf("Saw completion of wr_id = %lu\n\tStatus: %d\n", wr_id, status);

			if (status != IBV_WC_SUCCESS){
				fprintf(stderr, "Error: work request id %lu had error. Status: %d\n", wr_id, status);
				// DO ERROR HANDLING HERE!
			}


			// 1.) get channel

			// Defined within self_net.c
			ctrl_channel = get_send_ctrl_channel(self_net, wr_id);
			if (unlikely(ctrl_channel == NULL)){
				fprintf(stderr, "Error: control completion handler failed. Couldn't get channel. For wr_id = %lu\n", wr_id);
				return NULL;
			}

			
			// 2.) Extract Item

			// Defined within ctrl_channel.
			//	- will refresh posting a receive if was on recv/shared recv channel
			ret = extract_ctrl_channel(ctrl_channel, &ctrl_message);
			if (unlikely(ret != 0)){
				fprintf(stderr, "Error: control completion handler failed. Couldn't extract channel item. For wr_id = %lu\n", wr_id);
				return NULL;
			}

			ctrl_message_header = ctrl_message.header;

			printf("\n\n[Node %u] Sent message work completion!\n\tSource Node ID: %u\n\tMessage Class: %s\n\t\tContents: %s\n\n", 
						net_world -> self_node_id, ctrl_message_header.source_node_id, message_class_to_str(ctrl_message_header.message_class), ctrl_message.contents);
		}	
	}

	return 0;
}

void * run_recv_ctrl_handler(void * _cq_thread_data) {

	int ret;

	Cq_Thread_Data * cq_thread_data = (Cq_Thread_Data *) _cq_thread_data;


	// 1.) Start a dispatcher thread if needed

	Fifo * recv_dispatcher_fifo = cq_thread_data -> recv_dispatcher_fifo;


	Ctrl_Recv_Dispatcher_Thread_Data recv_dispatcher_thread_data;
	recv_dispatcher_thread_data.ib_device_id = cq_thread_data -> ib_device_id;
	recv_dispatcher_thread_data.dispatcher_fifo = recv_dispatcher_fifo;
	recv_dispatcher_thread_data.net_world = cq_thread_data -> net_world;
	recv_dispatcher_thread_data.work_pool = cq_thread_data -> work_pool;


	// if we are supposed to dispatch
	if (recv_dispatcher_fifo != NULL){
		ret = pthread_create(&(cq_thread_data -> dispatcher_thread), NULL, run_recv_ctrl_dispatcher, &recv_dispatcher_thread_data);
		if (ret != 0){
			fprintf(stderr, "Error: failed to start ctrl receive dispatcher thread within ib device id: %d\n", cq_thread_data -> ib_device_id);
			return NULL;
		}
	}


	// 2.) determine what cq this thread is working on

	// needed to find the CQ this thread is supposed to work on! 
	struct ibv_cq_ex * cq_ex = cq_thread_data -> cq;
	Net_World * net_world = cq_thread_data -> net_world;

	int ib_device_id = cq_thread_data -> ib_device_id;

	struct ibv_cq * cq = ibv_cq_ex_to_cq(cq_ex);






	enum ibv_wc_status status;
	uint64_t wr_id;

	
		
	// these will get populated upon extracting an item from channel
	

	// For now doing an infnite loop unless error....
	// Burns a lot of cpu....
	//	- but want low latency!!!
	//	- many, many exchange requests per sec

	int num_comp;

	int max_poll_entries = RECV_CTRL_MAX_POLL_ENTRIES;

	struct ibv_wc work_completions[max_poll_entries];
	

	Recv_Ctrl_Message recv_ctrl_messages[max_poll_entries];
	Ctrl_Channel * recv_ctrl_channel = get_recv_ctrl_channel(net_world -> self_net, ib_device_id);


	while (1){


		// 1.) consume completed receive work requests


		// wait for an entry
		do {
			num_comp = ibv_poll_cq(cq, max_poll_entries, work_completions);
		} while (num_comp == 0);


		// 2.) Obtain the control messages that were sent to this node
		//		- copy the messages from the verbs-registered buffer, and then replenish the
		//			received work requests in the SRQ corresponding to this thread's ib device


		// This copies the data from teh control channel to recv_ctrl_messages
		// along with marking them as consumed and re-producing the same quantity
		//		- it "replenishes" the recv work requests

		// The purpose of the batching and handing off is so ensure
		// that there are always receive requests available for senders
		// and that the sending messages won't be silently dropped
		// thus want minimal overhead between consuming a receive and reproducing
		ret = extract_batch_recv_ctrl_channel(recv_ctrl_channel, num_comp, recv_ctrl_messages);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: there was an error replenishing receive work requests after consuming %d\n", num_comp);
			return NULL;
		}

		// 3.) error handle for bad statuses
		for (int i = 0; i < num_comp; i++){

			// Consume the completed work request
			wr_id = work_completions[i].wr_id;
			status = work_completions[i].status;

				
			// This message is kinda ugly. Can be helpful with errors
			// printf("Saw completion of wr_id = %lu\n\tStatus: %d\n", wr_id, status);

			if (status != IBV_WC_SUCCESS){
				fprintf(stderr, "Error: work request id %lu had error. Status: %d\n", wr_id, status);
				// DO ERROR HANDLING HERE!
			}
		}


		// 4.) Add these receive messages to the sorting fifo 
		//		which will be responsible for reading the header 
		//		and placing it on the appropriate worker's task queue


		produce_batch_fifo(recv_dispatcher_fifo, num_comp, recv_ctrl_messages);
	}

	return 0;
}




// NOT SURE THE RIGHT WAY TO MAKE THIS WORK!!!

/*

void * run_ctrl_handler_ex(void * _cq_thread_data){

	int ret;
	int cq_ret;


	// 1.) determine what cq this thread is working on

	// IN REALITY THIS WILL CONTAIN MORE THAN JUST NET_WORLD
	// IT SHOULD CONNECT TO WORKER POOL
	// (THAT CONNECTS TO EXCHANGE AND SCHED AS WELL!)
	// GOING TO PASS ALL CONTROL MESSAGES TO APPROPRIATE WORKER!
	Cq_Thread_Data * cq_thread_data = (Cq_Thread_Data *) _cq_thread_data;

	// needed to find the CQ this thread is supposed to work on! 
	struct ibv_cq_ex * cq = cq_thread_data -> cq;
	Net_World * net_world = cq_thread_data -> net_world;
	Self_Net * self_net = net_world -> self_net;

	Work_Pool * work_pool = cq_thread_data -> work_pool;




	uint32_t self_node_id = net_world -> self_node_id;

	struct ibv_poll_cq_attr poll_qp_attr = {};
	cq_ret = ibv_start_poll(cq, &poll_qp_attr);

	// If Error after start, do not call "end_poll"
	if ((cq_ret != 0) && (cq_ret != ENOENT)){
		fprintf(stderr, "Error: could not start poll for completition queue\n");
		return NULL;
	}

	// if ret = 0, then ibv_start_poll already consumed an item
	int seen_new_completition;
	
	enum ibv_wc_status status;
	uint64_t wr_id;

	Ctrl_Channel * ctrl_channel;
		
	// these will get populated upon extracting an item from channel
	Ctrl_Message ctrl_message;
	Ctrl_Message_H ctrl_message_header;

	// For now doing an infnite loop unless error....
	// Burns a lot of cpu....
	//	- but want low latency!!!
	//	- many, many exchange requests per sec

	uint64_t fifo_insert_ind;

	bool to_refresh_recv = false;

	while (1){

		// return is 0 if a new item was cosumed, otherwise it equals ENOENT
		if (cq_ret == 0){
			seen_new_completition = 1;
		}
		else{
			seen_new_completition = 0;
		}
		
		// Consume the completed work request
		wr_id = cq -> wr_id;
		status = cq -> status;


		// NOTE: need to advance cq before extracting to prevent overrun
		//			- extract_ctrl_channel consumes a slot in fifo, which allows a different thread to produce
		//			- however that need means there are now more outstanding work request than cq entries which leads to overrun
		// Check for next completed work request...
		cq_ret = ibv_next_poll(cq);

		if ((cq_ret != 0) && (cq_ret != ENOENT)){
			// If Error after next, call "end_poll"
			ibv_end_poll(cq);
			fprintf(stderr, "Error: could not do next poll for completition queue\n");
			return NULL;
		}


		if (to_refresh_recv){
			ret = post_recv_ctrl_channel(ctrl_channel);
			if (unlikely(ret != 0)){
				fprintf(stderr, "Error: failure posting a receive after trying to replace an extracted item\n");
				return NULL;
			}
		}
		


		// IF there is work that needs to be done
		if (seen_new_completition){
			
			// This message is kinda ugly. Can be helpful with errors
			// printf("Saw completion of wr_id = %lu\n\tStatus: %d\n", wr_id, status);

			if (status != IBV_WC_SUCCESS){
				fprintf(stderr, "Error: work request id %lu had error. Status: %d\n", wr_id, status);
				// DO ERROR HANDLING HERE!
			}

			// 1.) get channel

			// Defined within self_net.c
			ctrl_channel = get_ctrl_channel(self_net, wr_id);
			if (unlikely(ctrl_channel == NULL)){
				fprintf(stderr, "Error: control completion handler failed. Couldn't get channel. For wr_id = %lu\n", wr_id);
				return NULL;
			}

			// 2.) Extract Item

			// Defined within ctrl_channel.
			ret = extract_ctrl_channel(ctrl_channel, &ctrl_message);
			if (unlikely(ret != 0)){
				fprintf(stderr, "Error: control completion handler failed. Couldn't extract channel item. For wr_id = %lu\n", wr_id);
				return NULL;
			}

			// 3.) If it was a shared receive / recv channel then we should act 

			if ((ctrl_channel -> channel_type == SHARED_RECV_CTRL_CHANNEL) || (ctrl_channel -> channel_type == RECV_CTRL_CHANNEL)){

				ctrl_message_header = ctrl_message.header;

				// For now just printing
				
				
				printf("\n\n[Node %u] Received control message!\n\tSource Node ID: %u\n\tMessage Class: %s\n\t\tContents: %s\n\n", 
							self_node_id, ctrl_message_header.source_node_id, message_class_to_str(ctrl_message_header.message_class), ctrl_message.contents);

				// REALLY SHOULD HAVE A FORMAT LIKE THIS HERE....

				// all fifo buffers at at:
				// work_pool -> classes)[ctrl_message_header.message_class] -> tasks
				
				// Error check a valid work class to place message on proper task fifo
				int control_message_class = ctrl_message_header.message_class;
				if (control_message_class > work_pool -> max_work_class_ind){
					fprintf(stderr, "Error: received message specifying message class %d, but declared maximum work class index of %d\n", control_message_class, work_pool -> max_work_class_ind);
					continue;
				}

				// Probably want to ensure there that the class has been added (and thus tasks is non-null)
				produce_fifo((work_pool -> classes)[ctrl_message_header.message_class] -> tasks, &ctrl_message);

				// NOT USING SWITCH BECAUSE UNNECESSARY COMPARISONS
				// switch(ctrl_message_header.message_class){
				// 	case EXCHANGE_CLASS:
				// 		printf("\n[Ctrl Handler] Producing on EXCHANGE tasks fifo\n");
				// 		fifo_insert_ind = produce_fifo((work_pool -> classes)[ctrl_message_header.message_class] -> tasks, &ctrl_message);
				// 		break;
				// 	case INVENTORY_CLASS:

				// 	default:
				// 		fprintf(stderr, "Error: saw an unknown message class of type %d\n", ctrl_message_header.message_class);
				// 		break; 
				// }
	
				to_refresh_recv = true;
			}
			else{
				to_refresh_recv = false;
				printf("\n\n[Node %u] Sent message work completion!\n\tSource Node ID: %u\n\tMessage Class: %s\n\t\tContents: %s\n\n", 
							self_node_id, ctrl_message_header.source_node_id, message_class_to_str(ctrl_message_header.message_class), ctrl_message.contents);
			}	
		}
		else{
			to_refresh_recv = false;
		}
	}

	ibv_end_poll(cq);

	return 0;
}


*/

