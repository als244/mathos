#include "ctrl_channel.h"


// COULD MAKE THESE ENCODE/DECODE MACROS IF THEY AER PERF HITS

// Lower 32 bits are the channel count
// Upper 32 bits are teh channel_id

// The channel count has no meaningful interpretation, besides posting unique wr_id's to avoid error
// The channel id will be used when polling completition queues to determine what channel to consume from
uint64_t encode_ctrl_wr_id(Ctrl_Channel * channel, uint32_t buffer_insert_ind){

	// lower 32 bits
	uint64_t wr_id = buffer_insert_ind;

	uint64_t channel_id = (uint64_t) channel -> channel_id;
	// setting upper 32 bits
	wr_id |= (channel_id << CHANNEL_ID_BITS);
	return wr_id;
}

void decode_ctrl_wr_id(uint64_t wr_id, CtrlChannelType * ret_channel_type, uint8_t * ret_ib_device_id, uint32_t * ret_endpoint_id) {

	// only care about the upper 32 bits
	uint32_t channel_id = wr_id >> 32;

	// endpoint id is the lower 32 bits
	*ret_channel_type = (CtrlChannelType) (channel_id >> (CHANNEL_ID_BITS - CTRL_CHANNEL_TYPE_BITS));
	
	// first shift channel to clear out control channel bits
	// now shift back to clear out the endpoint bits
	*ret_ib_device_id = (uint8_t) (channel_id << CTRL_CHANNEL_TYPE_BITS) >> (CTRL_CHANNEL_TYPE_BITS + ENDPOINT_ID_BITS);
	
	// shift up to clear out, then shift back
	*ret_endpoint_id = (channel_id << (CTRL_CHANNEL_TYPE_BITS + IB_DEVICE_ID_BITS)) >> (CTRL_CHANNEL_TYPE_BITS + IB_DEVICE_ID_BITS);

	return;
}


// | channel_type_val (2 bits) | ib_device_id (8 bits) | endpoint_id (20 bits) |

// reserve 2 bits for channel type (send/recv/shared_recv)
// reserve 8 bits for ib_device_id
// reserve 20 bits for endpoint ind:
//	- (for shared receive channels this value will be 0) => the reason for having ib_device_id has part of the encoding
//	- want to identify the correct shared_recv_queue channel to extract from!
uint32_t encode_ctrl_channel_id(CtrlChannelType channel_type, uint8_t ib_device_id, uint32_t endpoint_id) {

	uint32_t channel_id = 0;

	// 1.) encode channel type 
	channel_id |= (((uint32_t) channel_type) << (CHANNEL_ID_BITS - CTRL_CHANNEL_TYPE_BITS));

	// 2.) encode ib device id
	channel_id |= ((uint32_t) ib_device_id << (CHANNEL_ID_BITS - CTRL_CHANNEL_TYPE_BITS - IB_DEVICE_ID_BITS));

	// 3.) encode endpoint ind
	channel_id |= endpoint_id;
	return channel_id;
}



// IF to_produce is true then produce_ind is ignored

int post_recv_batch_ctrl_channel(Ctrl_Channel * channel, uint32_t num_recvs, bool to_produce, uint32_t produce_ind) {

	int ret;

	// Error check if we want
	// assert(channel -> channel_type == RECV_CHANNEL)

	// 1.) Copy item into a registered memory region

	uint32_t max_items = channel -> fifo -> max_items;

	// We are not actually producing items because the NIC will do that
	uint32_t start_insert_ind;
	if (to_produce){
		start_insert_ind = (uint32_t) produce_batch_fifo(channel -> fifo, num_recvs, NULL);
	}
	else {
		start_insert_ind = produce_ind;
	}

	uint32_t items_til_end = num_recvs;

	// check for loop around
	if ((start_insert_ind + num_recvs) > max_items){
		items_til_end = max_items - start_insert_ind;
	}
	uint32_t remain_recvs = num_recvs - items_til_end;

	

	// 2.) Now obtain the address of item within registered region
	void * start_buffer_addr = get_buffer_addr(channel -> fifo, start_insert_ind);

	
	// 3.) Based on it's insertion index and the channel id, determine an appropriate wr_id

	// now obtain the wr_id
	uint64_t wr_id_start = encode_ctrl_wr_id(channel, start_insert_ind);
	uint64_t wr_id_remain = encode_ctrl_wr_id(channel, 0);

	// 4.) Get other information needed for posting work requests
	uint32_t lkey = channel -> channel_mr -> lkey;
	uint32_t item_length = channel -> fifo -> item_size_bytes;


	// 5.) Actually post the work requests
	if (channel -> channel_type == SHARED_RECV_CTRL_CHANNEL){
		if (items_til_end == 0){
			printf("Posting to srq batch with items_til_end == 0\n");
		}
		ret = post_srq_batch_work_requests(channel -> srq, items_til_end, (uint64_t) start_buffer_addr, item_length, lkey, wr_id_start);

	}
	else{
		ret = post_recv_batch_work_requests(channel -> qp, items_til_end, (uint64_t) start_buffer_addr, item_length, lkey, wr_id_start);
	}

	// 6.) Post the requests that loop around from beginning
	if (remain_recvs > 0){

		if (channel -> channel_type == SHARED_RECV_CTRL_CHANNEL){
			if (items_til_end == 0){
				printf("Posting to srq batch with remain_recvs == 0\n");
			}
			ret = post_srq_batch_work_requests(channel -> srq, remain_recvs, (uint64_t) channel -> fifo -> buffer, item_length, lkey, wr_id_remain);
		}
		else{
			ret = post_recv_batch_work_requests(channel -> qp, remain_recvs, (uint64_t) channel -> fifo -> buffer, item_length, lkey, wr_id_remain);
		}
	}


	if (ret != 0){
		fprintf(stderr, "Error: post_channel failed\n");
		return -1;
	}

	return 0;
}


int post_recv_ctrl_channel(Ctrl_Channel * channel) {

	int ret = post_recv_batch_ctrl_channel(channel, 1, true, 0);

	if (ret != 0){
		fprintf(stderr, "Error: post_channel failed\n");
		return -1;
	}

	return 0;
}



// The pd that created AH should match channel -> pd
int post_send_ctrl_channel(Ctrl_Channel * channel, Ctrl_Message * ctrl_message, struct ibv_ah * ah, uint32_t remote_qp_num, uint32_t remote_qkey) {

	int ret;

	// Error check if we want

	// assert(channel -> channel_type == SEND_CHANNEL)

	// 1.) Copy item into a registered memory region

	// the lower 32 bits of insert_ind will be used as the wr_id
	// we know that the buffer sizes are limited by hardware ~32k
	// thus insert_ind can be casted down. 32 bits is plently
	uint32_t insert_ind = (uint32_t) produce_fifo(channel -> fifo, (void *) ctrl_message);

	// 2.) Now obtain the address of item within registered region
	void * buffer_item = get_buffer_addr(channel -> fifo, insert_ind);

	// 3.) Based on it's insertion index and the channel id, determine an appropriate wr_id

	// now obtain the wr_id
	uint64_t wr_id = encode_ctrl_wr_id(channel, insert_ind);

	// 4.) Get other information needed for posting work requests
	uint32_t lkey = channel -> channel_mr -> lkey;
	uint32_t length = channel -> fifo -> item_size_bytes;

	ret = post_send_work_request(channel -> qp, (uint64_t) buffer_item, length, lkey, wr_id, ah, remote_qp_num, remote_qkey);

	if (ret != 0){
		fprintf(stderr, "Error: post_channel failed\n");
		return -1;
	}

	return 0;
}


// Completition Queue Handler's will be calling consume_channel
//	- they will choose the correct channel based upon channel ID!


// When this is called upon RECV or SHARED_RECV channels, we just consumed an item so we want to replace!
// 	- When we extract something we sent no need to replace, because sends have content

// ASSUME ret_control_message has memory allocated somehow!!


int extract_ctrl_channel(Ctrl_Channel * channel, Ctrl_Message * ret_ctrl_message) {

	// Error Check if we want

	/*
	if (unlikely(channel == NULL)){
		fprintf(stderr, "Error: consume_channel failed because channel is null\n");
		return NULL;
	}
	*/

	// returns a copy of the item in fifo buffer => should free it when done!

	// when we put a message in control channel there is no extra space
	if ((channel -> channel_type == SEND_CTRL_CHANNEL)){
		Ctrl_Message send_ctrl_message;
		consume_fifo(channel -> fifo, &send_ctrl_message);
		memcpy((void *) ret_ctrl_message, &send_ctrl_message, sizeof(Ctrl_Message));
	}
	// when we get a receive message the first 40 bytes are GRH, so can skip passed this
	// and cast to control message
	// HERE COULD BE AND ELSE BUT BEING EXPLICITY FOR READABILITY
	if ((channel -> channel_type == RECV_CTRL_CHANNEL) || (channel -> channel_type == SHARED_RECV_CTRL_CHANNEL)){
		Recv_Ctrl_Message recv_ctrl_message;
		consume_fifo(channel -> fifo, &recv_ctrl_message);
		memcpy((void *) ret_ctrl_message, &(recv_ctrl_message.ctrl_message), sizeof(Ctrl_Message));
		int ret = post_recv_ctrl_channel(channel);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure posting a receive after trying to replace an extracted item\n");
			return -1;
		}
	}

	return 0;
}



int extract_batch_recv_ctrl_channel(Ctrl_Channel * channel, uint32_t num_messages, Recv_Ctrl_Message * ret_recv_ctrl_messages) {

	// Error Check if we want

	/*
	if (unlikely(channel == NULL)){
		fprintf(stderr, "Error: consume_channel failed because channel is null\n");
		return NULL;
	}
	*/

	// returns a copy of the item in fifo buffer => should free it when done!

	// assert(channel -> channel_type == RECV_CTRL_CHANNEL) || (channel -> channel_type == SHARED_RECV_CTRL_CHANNEL)

	// Retrieve num_messages from the fifo and copy them to ret_recv_ctrl_messages
	// Also make room for num_messages to replace those messages and return the starting location
	uint64_t produce_ind = consume_and_reproduce_batch_fifo(channel -> fifo, num_messages, ret_recv_ctrl_messages, NULL);

	// Now post a batch of receives to replenish the consumed received work requests
	int ret = post_recv_batch_ctrl_channel(channel, num_messages, false, produce_ind);
	if (ret != 0){
		fprintf(stderr, "Error: could not replenish consumed received work requests\n");
		return -1;
	}


	return 0;
}


Ctrl_Channel * init_ctrl_channel(CtrlChannelType channel_type, uint32_t max_items, uint8_t ib_device_id, struct ibv_pd * pd, 
									uint32_t endpoint_id, struct ibv_qp * qp, struct ibv_srq * srq, bool to_populate_recvs) {

	int ret;

	Ctrl_Channel * channel = (Ctrl_Channel *) malloc(sizeof(Ctrl_Channel));
	if (channel == NULL){
		fprintf(stderr, "Error: malloc failed to allocate channel\n");
		return NULL;
	}

	// 1.) assign channel values
	channel -> channel_type = channel_type;
	channel -> ib_device_id = ib_device_id;
	channel -> endpoint_id = endpoint_id;
	channel -> pd = pd;
	channel -> qp = qp;
	channel -> srq = srq;
	channel -> channel_id = encode_ctrl_channel_id(channel_type, ib_device_id, endpoint_id);

	// 1b.) Get Item Size bytes => all control messages have the same size.
	//		- However we need to add room for Global Routing Header if this is a receive/shared receive channel (40 bytes)
	uint32_t item_size_bytes;
	if ((channel_type == RECV_CTRL_CHANNEL) || (channel_type == SHARED_RECV_CTRL_CHANNEL)){
		item_size_bytes = sizeof(Recv_Ctrl_Message);
	}
	else{
		item_size_bytes = sizeof(Ctrl_Message);
	}
	
	// 2.) Create fifo to actually hold / manage channel items
	Fifo * fifo = init_fifo(max_items, item_size_bytes);
	if (fifo == NULL){
		fprintf(stderr, "Error: init_channel failed because couldn't create fifo\n");
		return NULL;
	}

	channel -> fifo = fifo;

	// 3.) Register fifo buffer region so we can post ib verbs send/recvs
	uint64_t buffer_size = (uint64_t) max_items * (uint64_t) item_size_bytes;

	struct ibv_mr * channel_mr;
	ret = register_virt_memory(pd, channel -> fifo -> buffer, buffer_size, &channel_mr);
	if (ret != 0){
		fprintf(stderr, "Error: could not register channel memory region\n");
		return NULL;
	}

	channel -> channel_mr = channel_mr;

	if (((channel_type == RECV_CTRL_CHANNEL) || (channel_type == SHARED_RECV_CTRL_CHANNEL)) && to_populate_recvs){

		ret = post_recv_batch_ctrl_channel(channel, max_items, true, 0);
		if (ret != 0){
			fprintf(stderr, "Error: failed to post intial receives\n");
		}

	}

	return channel;
}
