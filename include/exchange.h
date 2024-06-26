#ifndef EXCHANGE_H
#define EXCHANGE_H

#include "common.h"
#include "table.h"
#include "deque.h"
#include "lru.h"
#include "fingerprint.h"

typedef struct participant {
	uint64_t location_id;
	uint64_t addr;
	uint32_t rkey;
} Participant;

typedef enum exchange_item_type {
	BID = 0,
	OFFER 1
} ExchangeItemType;

typedef struct exchange_item {
	unsigned char * fingerprint;
	// could remove this if everyhing is using SHA256, but more general
	uint8_t fingerprint_bytes;
	uint64_t data_bytes;
	ExchangeItemType item_type;
	Deque * participants;
	pthread_mutex_t item_lock;
} Exchange_Item;


typedef struct exchange {
	// used for sharding objects
	// the least significant 64 bits of hash are used as uint64_t
	// avalance property and uniformity make this possible with SHA256
	// this value is then sharded across number of nodes
	// start_val and end_val represent the range of objects' least sig 64 bits that this table will hold
	// doesn't necessarily have to be uniform across nodes (different system RAM capacities), but needs
	// to be specified at configuration so other nodes know where to look up
	uint64_t start_val;
	uint64_t end_val;
	uint64_t max_bids;
	uint64_t max_offers;
	Table * bids;
	Table * offers;
	pthread_mutex_t exchange_lock;
} Exchange;





// One bid/offer per fingerprint!!!

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
int post_bid(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, uint64_t data_bytes, uint64_t location_id, uint64_t addr, uint32_t rkey);
// done after receiving a function request with a fingerprint not found in inventory
//	- triggers a lookup of offers and if a match is found does RDMA writes to matching bids and removes them from exchange
int post_offer(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, uint64_t data_bytes, uint64_t location_id, uint64_t addr, uint32_t rkey);


Exchange * init_exchange(uint64_t start_val, uint64_t end_val, uint64_t max_bids, uint64_t max_offers);



#endif