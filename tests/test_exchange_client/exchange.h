#ifndef EXCHANGE_H
#define EXCHANGE_H

#include "common.h"
#include "table.h"
#include "connection.h"
#include "deque.h"
#include "fingerprint.h"

typedef struct bid_participant {
	uint64_t location_id;
	uint64_t wr_id;
} Bid_Participant;

typedef struct offer_participant {
	uint64_t location_id;
	uint64_t addr;
	uint32_t rkey;
} Offer_Participant;

typedef enum exchange_item_type {
	BID = 0,
	OFFER = 1
} ExchangeItemType;

typedef struct exchange_bid {
	unsigned char * fingerprint;
	// could remove this if everyhing is using SHA256, but more general
	// WASTE OF SPACE HERE
	uint8_t fingerprint_bytes;
	ExchangeItemType item_type;
	Deque * participants;
	pthread_mutex_t item_lock;
	uint64_t data_bytes;
} Exchange_Item;


typedef struct client_connection {
	uint64_t location_id;
	Connection * connection;
} Client_Connection;


typedef struct exchange {
	// used for sharding objects
	// the least significant 64 bits of hash are used as uint64_t
	// avalance property and uniformity make this possible with SHA256
	// this value is then sharded across number of nodes
	// start_val and end_val represent the range of objects' least sig 64 bits that this table will hold
	// doesn't necessarily have to be uniform across nodes (different system RAM capacities), but needs
	// to be specified at configuration so other nodes know where to look up
	uint64_t id;
	uint64_t start_val;
	uint64_t end_val;
	uint64_t max_bids;
	uint64_t max_offers;
	Table * bids;
	Table * offers;
	Table * clients;
	pthread_mutex_t exchange_lock;
	// initialized upon first connection and then reused to connect to other exchanges
	struct ibv_qp * exchange_qp;
} Exchange;


// One bid/offer per fingerprint!!!


Exchange * init_exchange(uint64_t id, uint64_t start_val, uint64_t end_val, uint64_t max_bids, uint64_t max_offers, uint64_t max_clients);

int setup_client_connection(Exchange * exchange, uint64_t exchange_id, char * exchange_ip, char * exchange_port, uint64_t location_id, char * location_ip, char * location_port);


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
int post_bid(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, uint64_t data_bytes, uint64_t location_id, uint64_t wr_id);
// done after receiving a function request with a fingerprint not found in inventory
//	- triggers a lookup of offers and if a match is found does RDMA writes to matching bids and removes them from exchange
int post_offer(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, uint64_t data_bytes, uint64_t location_id, uint64_t addr, uint32_t rkey);





#endif