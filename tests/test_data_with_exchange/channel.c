#include "channel.h"

char * message_type_to_str(MessageType message_type){
	switch(message_type){
		case DATA_INITIATE:
			return "DATA_INITIATE";
		case DATA_RESPONSE:
			return "DATA_RESPONSE";
		case DATA_PACKET:
			return "DATA_PACKET";
		case BID_ORDER:
			return "BID_ORDER";
		case BID_MATCH:
			return "BID_MATCH";
		case OFFER_ORDER:
			return "OFFER_ORDER";
		case FUTURE_ORDER:
			return "FUTURE_ORDER";
		default:
			fprintf(stderr, "Unsupported MessageType\n");
			return "";
	}
}

uint64_t encode_wr_id(uint32_t sender_id, uint32_t channel_count, MessageType message_type) {
	uint64_t wr_id = ((uint64_t) message_type) << 56;
	wr_id |= (((uint64_t) channel_count) << 32);
	wr_id |= (uint64_t) sender_id;
	return wr_id;
}

MessageType decode_wr_id(uint64_t wr_id, uint32_t * ret_sender_id) {
	// sender_id is 32 bits, so clear out top 32 and move back
	uint32_t sender_id = (wr_id << 32) >> 32;
	*ret_sender_id = sender_id;
	MessageType message_type = wr_id >> 56;
	return message_type;
}

int channel_item_cmp(void * channel_item, void * other_item) {
	uint64_t id_a = ((Channel_Item *) channel_item) -> id;
	uint64_t id_b = ((Channel_Item *) other_item) -> id;
	return id_a - id_b;
}

uint64_t channel_item_hash_func(void * channel_item, uint64_t table_size) {
	uint64_t key = ((Channel_Item *) channel_item) -> id;
	// Taken from "https://github.com/shenwei356/uint64-hash-bench?tab=readme-ov-file"
	// Credit: Thomas Wang
	key = (key << 21) - key - 1;
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);
	return key % table_size;
}



Channel * init_channel(uint32_t self_id, uint32_t peer_id, uint32_t capacity, MessageType message_type, uint32_t message_size, bool to_init_buffer, bool is_inbound, bool to_presubmit_recv, struct ibv_pd * pd, struct ibv_qp * qp, struct ibv_cq_ex * cq) {

	int ret;

	Channel * channel = (Channel *) malloc(sizeof(Channel));
	if (channel == NULL){
		fprintf(stderr, "Error: malloc failed in allocating channel\n");
		return NULL;
	}

	channel -> self_id = self_id;
	channel -> peer_id = peer_id;
	channel -> capacity = capacity;
	channel -> message_type = message_type;
	channel -> message_size = message_size;
	channel -> is_inbound = is_inbound;
	channel -> pd = pd;
	channel -> qp = qp;
	channel -> cq = cq;

	uint64_t min_size = capacity;
	uint64_t max_size = capacity;
	float load_factor = 1.0f;
	float shrink_factor = 0.0f;

	Hash_Func hash_func = &channel_item_hash_func;
	Item_Cmp item_cmp = &channel_item_cmp;
	Table * buffer_table = init_table(min_size, max_size, load_factor, shrink_factor, hash_func, item_cmp);
	if (buffer_table == NULL){
		fprintf(stderr, "Error: could not initialize buffer table\n");
		return NULL;
	}

	channel -> buffer_table = buffer_table;

	void * buffer = NULL;

	if (to_init_buffer){
		buffer = malloc((uint64_t) capacity * message_size);

		// now need to register with ib_verbs to get mr => lkey needed for posting sends/recvs
		ret = register_virt_memory(pd, buffer, (uint64_t) capacity * message_size, &(channel -> mr));
		if (ret != 0){
			fprintf(stderr, "Error: could not register ring buffer items mr\n");
			return NULL;
		}
	}
	channel -> buffer = buffer;

	ret = pthread_mutex_init(&(channel -> cnt_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init cnt lock\n");
		return NULL;
	}

	// now need to post the intial receives for this channel 
	if (is_inbound && to_presubmit_recv){
		for (uint16_t i = 0; i < capacity; i++){
			ret = submit_in_channel_reservation(channel, NULL, NULL);
			if (ret != 0){
				fprintf(stderr, "Error: could not submit initial receives to channel\n");
				return NULL;
			}
		}
	}

	return channel;
}


int submit_in_channel_reservation(Channel * channel, uint64_t * ret_wr_id, uint64_t * ret_addr){
	
	int ret;

	// 1.) get channel count and increment
	pthread_mutex_lock(&(channel -> cnt_lock));
	uint32_t channel_cnt = channel -> cnt;
	channel -> cnt += 1;
	pthread_mutex_unlock(&(channel -> cnt_lock));

	// 2.) get message type
	MessageType message_type = channel -> message_type;

	// 3.) encode wr id
	uint64_t encoded_wr_id = encode_wr_id(channel -> peer_id, channel_cnt, message_type);

	// 4.) create item and insert into table
	Channel_Item * item = malloc(sizeof(Channel_Item));
	item -> id = encoded_wr_id;
	uint64_t item_ind;
	ret = insert_item_get_index_table(channel -> buffer_table, item, &item_ind);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert item into channel\n");
		return -1;
	}

	// 5.) Convert index into location within registered buffer
	uint64_t addr = (uint64_t) (channel -> buffer + channel -> message_size * item_ind);

	// 6.) Post Receive
	ret = post_recv_work_request(channel -> qp, addr, channel -> message_size, channel -> mr -> lkey, encoded_wr_id);
	if (ret != 0){
		fprintf(stderr, "Error: could not post receive work request for channel\n");
		return -1;
	}

	// 7.) Set return arguments if they were asked for
	if (ret_wr_id != NULL){
		*ret_wr_id = encoded_wr_id;
	}
	
	if (ret_addr != NULL){
		*ret_addr = addr;
	}

	return 0;
}

// either send with a specified wr_id, or generate one based on protocol. 
// optionally returned the wr_id used for sending and the addr of item within channel buffer

// For non-data channels
int submit_out_channel_message(Channel * channel, void * message, uint64_t * send_wr_id, uint64_t * ret_wr_id, uint64_t * ret_addr) {

	int ret;

	// 1.) get channel count and increment
	pthread_mutex_lock(&(channel -> cnt_lock));
	uint32_t channel_cnt = channel -> cnt;
	channel -> cnt += 1;
	pthread_mutex_unlock(&(channel -> cnt_lock));

	// 2.) get message type
	MessageType message_type = channel -> message_type;

	// 3.) encode wr id, if not passed in. (for initial out messages like orders, do this. for responding out messages use passed in wr_id)
	
	uint64_t encoded_wr_id;
	if (send_wr_id == NULL){
		encoded_wr_id = encode_wr_id(channel -> self_id, channel_cnt, message_type);
	}
	else{
		encoded_wr_id = *send_wr_id;
	}

	// 4.) create item and insert into table
	Channel_Item * item = malloc(sizeof(Channel_Item));
	item -> id = encoded_wr_id;
	uint64_t item_ind;
	ret = insert_item_get_index_table(channel -> buffer_table, item, &item_ind);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert item into channel\n");
		return -1;
	}

	// 5.) Convert index into location within registered buffer
	uint64_t addr = (uint64_t) (channel -> buffer + channel -> message_size * item_ind);

	// 6.) Memcpy the message to this location
	memcpy((void *) addr, message, channel -> message_size);

	// 7.) Post send
	ret = post_send_work_request(channel -> qp, addr, channel -> message_size, channel -> mr -> lkey, encoded_wr_id);
	if (ret != 0){
		fprintf(stderr, "Error: could not post receive work request for channel\n");
		return -1;
	}

	// 8.) Set return arguments if they were asked for
	if (ret_wr_id != NULL){
		*ret_wr_id = encoded_wr_id;
	}
	
	if (ret_addr != NULL){
		*ret_addr = addr;
	}

	return 0;
}

// assume ret_item has memory allocated with channel -> message_bytes # of bytes
int extract_channel_item(Channel * channel, uint64_t id, bool to_replace_reservation, void * ret_item) {

	int ret;

	Table * buffer_table = channel -> buffer_table;

	Channel_Item channel_item;
	channel_item.id = id;

	uint64_t index;

	// might just want to remove item here
	ret = find_item_index_table(buffer_table, &channel_item, &index);
	if (ret != 0){
		fprintf(stderr, "Error: could not find item in channel with id: %lu\n", id);
		return -1;
	}

	void * channel_buffer = channel -> buffer;
	uint64_t message_size = channel -> message_size;

	void * addr = channel_buffer + index * message_size;
	// copy the contents into return location
	memcpy(ret_item, addr, message_size);

	// now 
	ret = remove_item_at_index_table(buffer_table, &channel_item, index);
	if (ret != 0){
		fprintf(stderr, "Error: could not remove item from channel's buffer table\n");
		return -1;
	}

	// reset buffer location
	memset(addr, 0, message_size);

	// to_replace for initially in-bound messages (which don't care about return addr or wr_id)
	if (to_replace_reservation) {
		ret = submit_in_channel_reservation(channel, NULL, NULL);
		if (ret != 0){
			fprintf(stderr, "Error: could not replace channel reservation within extract item\n");
			return -1;
		}
	}

	return 0;
}

