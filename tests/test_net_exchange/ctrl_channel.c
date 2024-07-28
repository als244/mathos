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
	uint32_t item_size_bytes = sizeof(Control_Message);
	if ((channel_type == RECV_CTRL_CHANNEL) || (channel_type == SHARED_RECV_CTRL_CHANNEL)){
		item_size_bytes += sizeof(struct ibv_grh);
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

		for (int i = 0; i < max_items; i++){
			ret =  post_recv_ctrl_channel(channel);
			if (ret != 0){
				fprintf(stderr, "Error: failure posting intitial recveive message on control channel\n");
				return NULL;
			}
		}

	}

	return channel;
}


// Send_Dest must have been populated with a 
int post_recv_ctrl_channel(Ctrl_Channel * channel) {

	int ret;

	// Error check if we want
	// assert(channel -> channel_type == RECV_CHANNEL)

	// 1.) Copy item into a registered memory region

	// the lower 32 bits of insert_ind will be used as the wr_id
	// we know that the buffer sizes are limited by hardware ~32k
	// thus insert_ind can be casted down. 32 bits is plently
	uint32_t insert_ind = (uint32_t) produce_fifo(channel -> fifo, NULL);

	// 2.) Now obtain the address of item within registered region
	void * buffer_item = get_buffer_addr(channel -> fifo, insert_ind);

	// 3.) Based on it's insertion index and the channel id, determine an appropriate wr_id

	// now obtain the wr_id
	uint64_t wr_id = encode_ctrl_wr_id(channel, insert_ind);

	// 4.) Get other information needed for posting work requests
	uint32_t lkey = channel -> channel_mr -> lkey;
	uint32_t length = channel -> fifo -> item_size_bytes;

	if (channel -> channel_type == SHARED_RECV_CTRL_CHANNEL){
		ret = post_srq_work_request(channel -> srq, (uint64_t) buffer_item, length, lkey, wr_id);
	}
	else{
		ret = post_recv_work_request(channel -> qp, (uint64_t) buffer_item, length, lkey, wr_id);
	}

	if (ret != 0){
		fprintf(stderr, "Error: post_channel failed\n");
		return -1;
	}

	return 0;
}

// The pd that created AH should match channel -> pd
int post_send_ctrl_channel(Ctrl_Channel * channel, Control_Message * ctrl_message, struct ibv_ah * ah, uint32_t remote_qp_num, uint32_t remote_qkey) {

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
int extract_ctrl_channel(Ctrl_Channel * channel, Control_Message * ret_ctrl_message) {

	// Error Check if we want

	/*
	if (unlikely(channel == NULL)){
		fprintf(stderr, "Error: consume_channel failed because channel is null\n");
		return NULL;
	}
	*/

	// returns a copy of the item in fifo buffer => should free it when done!
	void * fifo_item = consume_fifo(channel -> fifo);
	if (unlikely(fifo_item == NULL)){
		fprintf(stderr, "Error: within extract_ctrl_channel, consume_fifo returned null\n");
		return -1;
	}

	// when we put a message in control channel there is no extra space
	if ((channel -> channel_type == SEND_CTRL_CHANNEL)){
		memcpy((void *) ret_ctrl_message, fifo_item, sizeof(Control_Message));
	}
	// when we get a receive message the first 40 bytes are GRH, so can skip passed this
	// and cast to control message
	if ((channel -> channel_type == RECV_CTRL_CHANNEL) || (channel -> channel_type == SHARED_RECV_CTRL_CHANNEL)){
		memcpy((void *) ret_ctrl_message, (void *) ((uint64_t) fifo_item + sizeof(struct ibv_grh)), sizeof(Control_Message));
	}
	
	if ((channel -> channel_type == RECV_CTRL_CHANNEL) || (channel -> channel_type == SHARED_RECV_CTRL_CHANNEL)){
		// after extracting a control item (must have been in receive queue), replace it
		int ret = post_recv_ctrl_channel(channel);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure posting a receive after trying to replace an extracted item\n");
			return -1;
		}
	}

	// the consume_fifo() returned a copy of the item in the buffer,
	// so we want to free this now that we have copied contents to a ctrl message
	free(fifo_item);

	// NOTE: ensure to free the message returned from extract!

	return 0;
}