#ifndef EXCHANGE_H
#define EXCHANGE_H

#include "common.h"
#include "table.h"
#include "deque.h"
#include "fingerprint.h"
#include "communicate.h"
#include "channel.h"

typedef enum exch_item_type {
	BID_ITEM,
	OFFER_ITEM,
	FUTURE_ITEM
} ExchangeItemType;


typedef struct Bid_Participant {
	uint32_t location_id;
	// for BIDs upon a match
	uint64_t wr_id;
} Bid_Participant;

typedef struct Offer_Participant {
	uint32_t location_id;
} Offer_Participant;

typedef struct Future_Participant {
	uint32_t location_id;
} Future_Participant;


typedef struct exchange_item {
	uint64_t data_bytes;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	Deque * participants;
	ExchangeItemType item_type;
	pthread_mutex_t item_lock;
} Exchange_Item;


typedef struct client_connection {
	uint32_t location_id;
	Connection * connection;
	uint32_t capacity_channels;
	Channel * in_bid_orders;
	Channel * in_offer_orders;
	Channel * in_future_orders;
	Channel * out_bid_matches;
} Client_Connection;


typedef struct exchange {
	// used for sharding objects
	// the least significant 64 bits of hash are used as uint64_t
	// avalance property and uniformity make this possible with SHA256
	// this value is then sharded across number of nodes
	// start_val and end_val represent the range of objects' least sig 64 bits that this table will hold
	// doesn't necessarily have to be uniform across nodes (different system RAM capacities), but needs
	// to be specified at configuration so other nodes know where to look up
	uint32_t id;
	uint64_t start_val;
	uint64_t end_val;
	uint64_t max_bids;
	uint64_t max_offers;
	Table * bids;
	Table * offers;
	Table * futures;
	Table * clients;
	pthread_mutex_t exchange_lock;
	struct ibv_pd * exchange_pd;
	// initialized upon first connection and then reused to connect to other clients
	struct ibv_qp * exchange_qp;
	// initialized upon first connection and then resused with other clients
	struct ibv_cq_ex * exchange_cq;
	// number of completion threads should equal number of CQs, likely equal number of QPs...
	pthread_t * completion_threads;
} Exchange;

typedef struct exchange_completition {
	uint64_t completition_thread_id;
	Exchange * exchange;
} Exchange_Completition;


// One bid/offer per fingerprint!!!


Exchange * init_exchange(uint32_t id, uint64_t start_val, uint64_t end_val, uint64_t max_bids, uint64_t max_offers, uint64_t max_futures, uint32_t max_clients, struct ibv_context * ibv_ctx);

int setup_client_connection(Exchange * exchange, uint32_t exchange_id, char * exchange_ip, uint32_t location_id, char * location_ip, char * server_port, uint32_t capacity_channels);

// NEED TO CONSIDER THE SYNCHONIZATION / ORDERING WHEN CACHING OCCURS AND ADDR+RKEY no longer valid!
// locks on each bid + offer, but that isn't enough
// What about ordering from the "participant" who wants to declare an addr invalid, but there is a delay in sending this information
// 	- Before making an address invalid the owner must first remove item from exchange such that for the entirety of object exiting on exchange it is valid
//	- This means a round-trip from owner to exchange before doing any internal data-transfers / invalidations which may be prohibitive...
//	- Also means bookkeeping outstanding requests because cannot remove item from exchange until there are no outstanding requests for that item
//		- Outstanding requests needs to be atomic

// done after receiving a function request with a fingerprint not found in inventory
//	- triggers a lookup of offers and if a match is found does an RDMA read to specified addr and rkey
// 	- might want to consider not supplying a memory location yet because could be outstanding for a while and do not want to reserve destination...
int post_bid(Exchange * exchange, uint8_t * fingerprint, uint64_t data_bytes, uint32_t location_id, uint64_t wr_id);
// done after receiving a function request with a fingerprint not found in inventory
//	- triggers a lookup of offers and if a match is found does RDMA writes to matching bids and removes them from exchange
int post_offer(Exchange * exchange, uint8_t * fingerprint, uint64_t data_bytes, uint32_t location_id);

int post_future(Exchange * exchange, uint8_t * fingerprint, uint64_t data_bytes, uint32_t location_id);





#endif