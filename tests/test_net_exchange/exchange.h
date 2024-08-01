#ifndef EXCHANGE_H
#define EXCHANGE_H

#include "common.h"
#include "config.h"
#include "table.h"
#include "deque.h"
#include "fingerprint.h"
#include "inventory_messages.h"

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
	uint64_t lookup_cnt;
	// updated upon every time this item is looked up
	uint64_t timestamp_lookup;
	// updated upon every time this item is modified
	uint64_t modify_cnt;
	uint64_t timestamp_modify;
	// used when updating ref_cnt or timestamps
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
	// for now using self_id as the assinged node id
	// but we could seperate node id and exchange ids if we wanted
	//	- (having dedicated exchange nodes would be a good idea if custom building data-center => more cost effective)
	uint32_t self_id;
	// upon a new addition this value get's updated and this exchange
	// may have to send a portion of it's exchange data to other exchanges 
	// for rebalancing purposes to maintain proper lookups

	// this value get's populated after a successful join to the network
	// however, this structure get's intialized for that => 
	// it gets filled in later when value is known
	// this is the node count for number of items in the net_world -> nodes table
	// it equals the number of participating nodes within network
	// 	- (this is same as table count because table count also has master, but doesn't include self)
	uint32_t node_cnt;
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


Exchange * init_exchange();


// The generic function called by exchange workers who then call the appropriate
// order type.

// Some exchange functions will generate a return message that should be sent out the exchange will populated a return control message that the calling worker then can then send
// appropriately

// Many functions will have no return type
int do_exchange_function(Exchange * exchange, Ctrl_Message * ctrl_message, uint32_t * ret_num_ctrl_messages, Ctrl_Message ** ret_ctrl_messages);



void exch_message_type_to_str(char * buf, ExchMessageType exch_message_type);


#endif