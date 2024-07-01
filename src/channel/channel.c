#include "channel.h"


uint64_t encode_wr_id(uint64_t sender_id, uint64_t channel_count, MessageType message_type) {
	uint64_t wr_id = ((uint64_t) message_type) << 56;
	wr_id |= (channel_count << 40);
	wr_id |= sender_id;
	return wr_id;
}

int channel_item_cmp(void * channel_item, void * other_item) {
	uint64_t id_a = ((Channel_Item *) channel_item) -> wr_id;
	uint64_t id_b = ((Channel_Item *) other_item) -> wr_id;
	return id_a - id_b;
}

uint64_t channel_item_hash_func(void * channel_item, uint64_t table_size) {
	uint64_t key = ((Channel_Item *) channel_item) -> wr_id;
	// Taken from "https://github.com/shenwei356/uint64-hash-bench?tab=readme-ov-file"
	// Credit: Thomas Wang
	key = (key << 21) - key - 1;
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);
	return key;
}



Channel * init_channel(uint64_t self_id, uint64_t peer_id, uint16_t capacity, MessageType message_type, uint64_t message_size, bool is_inbound, bool to_presubmit_recv, struct ibv_pd * pd, struct ibv_qp * qp) {

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

	void * buffer = malloc((uint64_t) capacity * message_size);

	// now need to register with ib_verbs to get mr => lkey needed for posting sends/recvs
	int ret = register_virt_memory(pd, buffer, (uint64_t) capacity * message_size, &(channel -> mr));
	if (ret != 0){
		fprintf(stderr, "Error: could not register ring buffer items mr\n");
		return NULL;
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
	uint16_t channel_cnt = channel -> cnt;
	channel -> cnt += 1;
	pthread_mutex_unlock(&(channel -> cnt_lock));

	// 2.) get message type
	MessageType message_type = channel -> message_type;

	// 3.) encode wr id
	uint64_t encoded_wr_id = encode_wr_id(channel -> peer_id, channel_cnt, message_type);

	// 4.) create item and insert into table
	Channel_Item * item = malloc(sizeof(Channel_Item));
	item -> wr_id = encoded_wr_id;
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

int submit_out_channel_message(Channel * channel, void * message, uint64_t * ret_wr_id, uint64_t * ret_addr) {

	int ret;

	// 1.) get channel count and increment
	pthread_mutex_lock(&(channel -> cnt_lock));
	uint16_t channel_cnt = channel -> cnt;
	channel -> cnt += 1;
	pthread_mutex_unlock(&(channel -> cnt_lock));

	// 2.) get message type
	MessageType message_type = channel -> message_type;

	// 3.) encode wr id
	uint64_t encoded_wr_id = encode_wr_id(channel -> self_id, channel_cnt, message_type);

	// 4.) create item and insert into table
	Channel_Item * item = malloc(sizeof(Channel_Item));
	item -> wr_id = encoded_wr_id;
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

