#ifndef EXCHANGE_H
#define EXCHANGE_H


#include "common.h"
#include "config.h"
#include "table.h"
#include "deque.h"
#include "fingerprint.h"

typedef enum exch_item_type {
	BID_ITEM,
	OFFER_ITEM,
	FUTURE_ITEM
} ExchangeItemType;


typedef struct exchange_item {
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	// Deque of uint32_t's representing node id's
	Deque * participants;
	ExchangeItemType item_type;
	// used for caching purposes
	// every time this item was looked up increment the count
	uint64_t ref_count;
	pthread_mutex_t exch_item_lock;
} Exchange_Item;


typedef struct exchange {
	// used for sharding objects
	// the least significant 64 bits of hash are used as uint64_t
	// avalance property and uniformity make this possible with SHA256
	// this value is then sharded across number of nodes
	// start_val and end_val represent the range of objects' least sig 64 bits that this table will hold
	// doesn't necessarily have to be uniform across nodes (different system RAM capacities), but needs
	// to be specified at configuration so other nodes know where to look up
	
	// this value get's populated after a successful join to the network
	// however, this structure get's intialized for that => 
	// it gets filled in later when value is known
	uint32_t self_id;
	// upon a new addition this value get's updated and this exchange
	// may have to send a portion of it's exchange data to other exchanges 
	// for rebalancing purposes to maintain proper lookups

	// this value get's populated after a successful join to the network
	// however, this structure get's intialized for that => 
	// it gets filled in later when value is known
	uint32_t global_worker_node_cnt;
	// FOR NOW: not using for start_val/end_val...
	// BUT coulnt have more sophisticated policy 
	// rather than just modulus over # of global nodes
	//	(i.e. some exchanges are tied to nodes with more system memory
	//			or are dedicated exchange nodes => they should take larger share)
	// During a rebalancing period things will have race conditions, so want these
	// to get updated in order to send a reject if the incoming fingerprint is not
	// within this range. The other side might try again 
	// (if they thought they were correct and that this node was slower to update) or 
	// they may receive an update on their end indicating to try a different exchange
	uint64_t start_val;
	uint64_t end_val;


	// setting limits on table values
	uint64_t max_bids;
	uint64_t max_offers;
	uint64_t max_futures;
	Table * bids;
	Table * offers;
	Table * futures;
	// might want to build a different
	// structure that makes it easier for a single query across all tables...?
	//	- A fingerprint can be in multiple tables
	//	- A specific fingerprint + node_id can be exclusively in each table
	pthread_mutex_t exchange_lock;
} Exchange;




// One bid/offer per fingerprint!!!


Exchange * init_exchange(uint64_t max_bids, uint64_t max_offers, uint64_t max_futures);


// The generic function called by exchange workers who then call the appropriate
// order type.

// Some exchange functions will generate a return message that should be sent out the exchange will populated a return control message that the calling worker then can then send
// appropriately

// Many functions will have no return type
int do_exchange_function(Exchange * exchange, Ctrl_Message * ctrl_message, uint32_t num_ret_messages, Ctrl_Message ** ret_ctrl_messages);



// THE FUNCTIONS BELOW SHOULD NOT BE EXPOSED...


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
int post_bid(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id);
// done after receiving a function request with a fingerprint not found in inventory
//	- triggers a lookup of offers and if a match is found does RDMA writes to matching bids and removes them from exchange
int post_offer(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id);

int post_future(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id);


#endif