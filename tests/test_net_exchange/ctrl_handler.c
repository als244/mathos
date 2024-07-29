#include "ctrl_handler.h"


void * run_ctrl_handler(void * _cq_thread_data){

	int ret;


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
	ret = ibv_start_poll(cq, &poll_qp_attr);

	// If Error after start, do not call "end_poll"
	if ((ret != 0) && (ret != ENOENT)){
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

	while (1){

		// return is 0 if a new item was cosumed, otherwise it equals ENOENT
		if (ret == 0){
			seen_new_completition = 1;
		}
		else{
			seen_new_completition = 0;
		}
		
		// Consume the completed work request
		wr_id = cq -> wr_id;
		status = cq -> status;
		// other fields as well...
		if (seen_new_completition){
			/* DO SOMETHING WITH wr_id! */
			
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
			//		- if this was a shared receive / receive channel then this function will automatically replenish

			// Defined within ctrl_channel.
			ret = extract_ctrl_channel(ctrl_channel, &ctrl_message);
			if (unlikely(ret != 0)){
				fprintf(stderr, "Error: control completion handler failed. Couldn't extract channel item. For wr_id = %lu\n", wr_id);
				return NULL;
			}

			// 3.) If it was a shared receive / recv channel then we should act 


			// FOR NOW JUST PRINTING

			if ((ctrl_channel -> channel_type == SHARED_RECV_CTRL_CHANNEL) || (ctrl_channel -> channel_type == RECV_CTRL_CHANNEL)){

				ctrl_message_header = ctrl_message.header;

				// For now just printing
				printf("\n\n[Node %u] Received control message!\n\tSource Node ID: %u\n\tMessage Class: %s\n\t\tContents: %s\n\n", 
							self_node_id, ctrl_message_header.source_node_id, message_class_to_str(ctrl_message_header.message_class), ctrl_message.contents);


				// REALLY SHOULD HAVE A FORMAT LIKE THIS HERE....

				// all fifo buffers at at:
				// work_pool -> classes)[ctrl_message_header.message_class] -> tasks
				
				switch(ctrl_message_header.message_class){
					case EXCHANGE_CLASS:
						fifo_insert_ind = produce_fifo((work_pool -> classes)[ctrl_message_header.message_class] -> tasks, &ctrl_message);
						printf("[Ctrl Handler] Producing on fifo with address %p\n", (void *) (((work_pool -> classes)[ctrl_message_header.message_class]) -> tasks));
						break;
					default:
						fprintf(stderr, "Error: saw an unknown message class of type %d\n", ctrl_message_header.message_class);
						break; 
				}
			}			
		}

		// Check for next completed work request...
		ret = ibv_next_poll(cq);

		if ((ret != 0) && (ret != ENOENT)){
			// If Error after next, call "end_poll"
			ibv_end_poll(cq);
			fprintf(stderr, "Error: could not do next poll for completition queue\n");
			return NULL;
		}
	}

	ibv_end_poll(cq);

	return 0;
}

