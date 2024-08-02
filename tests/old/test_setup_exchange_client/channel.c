#include "channel.h"


uint64_t encode_wr_id(uint64_t sender_id, uint64_t channel_count, MessageType message_type) {
	uint64_t wr_id = ((uint64_t) message_type) << 56;
	wr_id |= (channel_count << 40);
	wr_id |= sender_id;
	return wr_id;
}

Channel * init_channel(uint64_t self_id, uint64_t peer_id, uint16_t capacity, MessageType message_type, uint64_t message_size, bool is_recv, struct ibv_pd * pd, struct ibv_qp * qp) {

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
	channel -> is_recv = is_recv;
	channel -> pd = pd;
	channel -> qp = qp;

	Ring_Buffer * ring_buffer = init_ring_buffer(capacity, message_size);
	if (ring_buffer == NULL){
		fprintf(stderr, "Error: could not initialize ring buffer for channel\n");
		return NULL;
	}

	

	// now need to register with ib_verbs to get mr => lkey needed for posting sends/recvs
	int ret = register_virt_memory(pd, ring_buffer -> items, (uint64_t) capacity * message_size, &(channel -> mr));
	if (ret != 0){
		fprintf(stderr, "Error: could not register ring buffer items mr\n");
		return NULL;
	}

	uint64_t count = 0;
	// now need to post the intial receives for this channel 
	if (is_recv){
		uint64_t addr;
		uint64_t encoded_wr_id;
		for (uint16_t i = 0; i < capacity; i++){
			addr = get_write_addr(ring_buffer, i);
			encoded_wr_id = encode_wr_id(peer_id, i, message_type);
			ret = post_recv_work_request(qp, addr, message_size, channel -> mr -> lkey, encoded_wr_id);
			if (ret != 0){
				fprintf(stderr, "Error: could not post intial receive work request for channel\n");
				return NULL;

			}
		}
		count = capacity;
	}

	channel -> count = count;
	channel -> ring_buffer = ring_buffer;

	return channel;
}
