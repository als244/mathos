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
	Control_Message * ctrl_message;

	Control_Message_H ctrl_message_header;

	// For now doing an infnite loop unless error....
	// Burns a lot of cpu....
	//	- but want low latency!!!
	//	- many, many exchange requests per sec

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
			printf("Saw completion of wr_id = %lu\n\tStatus: %d\n", wr_id, status);

			if (status != IBV_WC_SUCCESS){
				fprintf(stderr, "Error: work request id %lu had error\n", wr_id);
				// DO ERROR HANDLING HERE!
			}

			// 1.) get channel

			// Defined within self_net.c
			ctrl_channel = get_ctrl_channel(self_net, wr_id);
			if (ctrl_channel == NULL){
				fprintf(stderr, "Error: control completion handler failed. Couldn't get channel. For wr_id = %lu\n", wr_id);
				return NULL;
			}

			// 2.) Extract Item
			//		- if this was a shared receive / receive channel then this function will automatically replenish

			// Defined within ctrl_channel.
			ctrl_message = extract_ctrl_channel(ctrl_channel);
			if (ctrl_message == NULL){
				fprintf(stderr, "Error: control completion handler failed. Couldn't extract channel item. For wr_id = %lu\n", wr_id);
				return NULL;
			}

			// 3.) If it was a shared receive / recv channel then we should act 


			// FOR NOW JUST PRINTING

			if ((ctrl_channel -> channel_type == SHARED_RECV_CTRL_CHANNEL) || (ctrl_channel -> channel_type == RECV_CTRL_CHANNEL)){

				ctrl_message_header = ctrl_message -> header;

				// For now just printing
				printf("\n\nReceived control message!\n\tSource Node ID: %u\n\tMessage Type: %d\n\t\tContents: %s\n\n", 
							ctrl_message_header.source_node_id, ctrl_message_header.message_type, ctrl_message -> contents);


				// REALLY SHOULD HAVE A FORMAT LIKE THIS HERE....

				/*
				switch(ctrl_message_header.message_type){
					case BID_ORDER:
						// send to exchange worker
					case SCHED_REQUEST:
						// send to sched worker

				}
				*/
			}

			// 4.) we can free the message now
			//		- this was copied from (a copy) of the ctrl_channel fifo buffer
			//			- the intermediate copy (returned from consume_fifo()) was freed within extract()
			//			- purpose of the copies is to help with weird race conditions for very small buffers
			//		- the original contents are still there, just freeing the copy
			//		- those original contents will get overwritten when the producer/consumer buffer loops around

			free(ctrl_message);

			
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

