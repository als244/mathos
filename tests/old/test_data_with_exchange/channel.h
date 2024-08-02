#ifndef CHANNEL_H
#define CHANNEL_H

#include "common.h"
#include "table.h"
#include "communicate.h"

#define MESSAGE_TYPE_BITS 8
#define SENDER_ID_BITS 32

typedef enum message_type {
	DATA_REQUEST,
	DATA_RESPONSE,
	DATA_PACKET,
	BID_ORDER,
	BID_MATCH,
	BID_CANCEL,
	BID_Q,
	BID_Q_RESPONSE,
	OFFER_ORDER,
	OFFER_CANCEL,
	OFFER_Q,
	OFFER_Q_RESPONSE,
	FUTURE_ORDER,
	FUTURE_CANCEL,
	FUTURE_Q,
	FUTURE_Q_RESPONSE,
	SCHED_REQUEST,
	SCHED_RESPONSE,
	SCHED_CANCEL,
	HEARTBEAT,
	PEER_JOIN,
	PEER_DELETE,
	PAUSE,
	SHUTDOWN
} MessageType;


typedef struct bid_order {
	uint32_t location_id;
	// for BIDs upon a match
	uint64_t wr_id;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
} Bid_Order;

typedef struct offer_order {
	uint32_t location_id;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
} Offer_Order;

typedef struct future_order {
	uint32_t location_id;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
} Future_Order;

// the item we will be posting with wr_id == corresponding bid_id, 
// and we can locate the fingerprint based on outstanding bids table within client
// where location_id is chosen offer order to relay
typedef struct Bid_Match{
	uint32_t location_id;
} Bid_Match;


typedef struct Channel_Item {
	uint64_t id;
} Channel_Item;


typedef struct channel {
	uint32_t self_id;
	uint32_t peer_id;
	uint32_t message_size;
	MessageType message_type;
	uint32_t capacity: 24;
	uint32_t cnt: 24;
	// will be of size capacity * message_size
	void * buffer;
	// will store wr_id's and will call "find_item_index_table(wr_id)"
	// this is a safe operation because we know that the table won't grow/shrink 
	// to get the location within buffer
	// (either of contents if outbound, or reservation area if inbound)
	Table * buffer_table;
	// needed to register the memory region
	struct ibv_pd * pd;
	// registered "buffer", and contains lkey needed
	struct ibv_mr * mr;
	// needed for posting requests!
	struct ibv_qp * qp;
	// needed for handling events
	// this cq is almost certainly shared among many channels
	struct ibv_cq_ex * cq;
	bool is_inbound;
	pthread_mutex_t cnt_lock;
} Channel;


// ALL WR_IDS WILL HAVE FORMAT (where index 0 is least significant bit)

// Because using UD and shared QPs from various senders (shared wr_ids) across various protocols, 
// need to ensure that senders only consume recvs meant for them, and the receivers know how to post sends

// 0 - 32: sender id (so as to ensure senders don't step on each other toes consuming the same recv wr_ids)
// 32 - 56: channel count
//				- Simply for internal use on receiver end to maintain recv buffer mappings between wr_id and ring_buffer memory addr.
//				- Sender maintains their own channel count and increments by 1 for each message
//					- the receiver's corresponding recv request will be already be waiting due to protocol
//				- Wraps around to 0 after 2^16
//				- After consuming a ring_buffer item, re-populate a recveive work request with new location and incremented buffer count
// 56 - 64: message_type (so as to digsinguish the wr_ids and keep seperate counts)

char * message_type_to_str(MessageType message_type);

uint64_t encode_wr_id(uint32_t sender_id, uint32_t channel_count, MessageType message_type);

MessageType decode_wr_id(uint64_t wr_id, uint32_t * ret_sender_id);

Channel * init_channel(uint32_t self_id, uint32_t peer_id, uint32_t capacity, MessageType message_type, uint32_t message_size, bool is_inbound, bool to_presubmit_recv, struct ibv_pd * pd, struct ibv_qp * qp, struct ibv_cq_ex * cq);

// For in channels
int submit_in_channel_reservation(Channel * channel, uint64_t * ret_wr_id, uint64_t * ret_addr);

// For out channels (need to pass in values)
int submit_out_channel_message(Channel * channel, void * message, uint64_t * send_wr_id, uint64_t * ret_wr_id, uint64_t * ret_addr);

int extract_channel_item(Channel * channel, uint64_t id, bool to_replace_reservation, void * ret_item);

#endif