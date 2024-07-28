#include "exchange.h"

// there are items wihtin a participants deque
// each void * is really a uint32_t *
int participant_item_cmp(void * participant, void * other_participant){
	uint32_t node_id = *((uint32_t *) participant);
	uint32_t other_node_id = *((uint32_t *) other_participant);
	return node_id - other_node_id;
}


int exchange_item_cmp(void * exchange_item, void * other_item) {
	uint8_t * item_fingerprint = ((Exchange_Item *) exchange_item) -> fingerprint;
	uint8_t * other_fingerprint = ((Exchange_Item *) other_item) -> fingerprint;
	int cmp_res = memcmp(item_fingerprint, other_fingerprint, FINGERPRINT_NUM_BYTES);
	return cmp_res;
}	


uint64_t exchange_hash_func(void * exchange_item, uint64_t table_size) {
	Exchange_Item * item_casted = (Exchange_Item *) exchange_item;
	unsigned char * fingerprint = item_casted -> fingerprint;
	uint64_t least_sig_64bits = fingerprint_to_least_sig64(fingerprint, FINGERPRINT_NUM_BYTES);
	// bitmask should be the lower log(2) lower bits of table size.
	// i.e. if table_size = 2^12, we should have a bit mask of 12 1's
	uint64_t bit_mask;
	uint64_t hash_ind;

	int leading_zeros = __builtin_clzll(table_size);
	// 64 bits as table_size type
	// taking ceil of power of 2, then subtracing 1 to get low-order 1's bitmask
	int num_bits = (64 - leading_zeros) + 1;
	bit_mask = (1L << num_bits) - 1;
	hash_ind = (least_sig_64bits & bit_mask) % table_size;
	return hash_ind;
}

Exchange * init_exchange(uint64_t max_bids, uint64_t max_offers, uint64_t max_futures) {

	Exchange * exchange = (Exchange *) malloc(sizeof(Exchange));
	if (exchange == NULL){
		fprintf(stderr, "Error: malloc failed allocating exchange table\n");
		return NULL;
	}

	exchange -> max_bids = max_bids;
	exchange -> max_offers = max_offers;
	exchange -> max_futures = max_futures;


	// DEFAULT hyperparameters for hash tables

	// SHOULD HAVE BETTER CONFIGURATION HERE!
	uint64_t min_bid_size = 1UL << 20;
	if (min_bid_size > max_bids){
		min_bid_size = max_bids;
	}
	uint64_t min_offer_size = 1UL << 20;
	if (min_offer_size > max_offers){
		min_offer_size = max_offers;
	}
	uint64_t min_future_size = 1UL << 20;
	if (min_future_size > max_futures){
		min_future_size = max_futures;
	}
	float load_factor = .5f;
	float shrink_factor = .1f;
	Hash_Func hash_func = &exchange_hash_func;
	Item_Cmp item_cmp = &exchange_item_cmp;

	// init bids and offers table
	Table * bids = init_table(min_bid_size, max_bids, load_factor, shrink_factor, hash_func, item_cmp);
	Table * offers = init_table(min_offer_size, max_offers, load_factor, shrink_factor, hash_func, item_cmp);
	Table * futures = init_table(min_future_size, max_futures, load_factor, shrink_factor, hash_func, item_cmp);
	if ((bids == NULL) || (offers == NULL) || (futures == NULL)){
		fprintf(stderr, "Error: could not initialize exchange tables\n");
		return NULL;
	}

	exchange -> bids = bids;
	exchange -> offers = offers;
	exchange -> futures = futures;

	int ret = pthread_mutex_init(&(exchange -> exchange_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not initialize exchange lock\n");
		return NULL;
	}


	return exchange;
}

// exchange items are initialized if fingerprint not found, 
// then create item based on whoever called post_order
// exchange items should only exist with >= 1 participants
// when there are 0 participants they should be deleted
//	- (but maybe have some sort of delay to prevent overhead of creation/deletion
//		if it is a hot-fingerprint)


Exchange_Item * init_exchange_item(uint8_t * fingerprint, ExchangeItemType item_type, uint32_t node_id){

	int ret;

	Exchange_Item * exchange_item = (Exchange_Item *) malloc(sizeof(Exchange_Item));
	if (exchange_item == NULL){
		fprintf(stderr, "Error: malloc failed allocating exchange item\n");
		return NULL;
	}

	memcpy(exchange_item -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
	exchange_item -> item_type = item_type;

	Deque * participants = init_deque();
	if (participants == NULL){
		fprintf(stderr, "Error: could not initialize participants queue\n");
		return NULL;
	}

	ret = insert_deque(participants, FRONT_DEQUE, &node_id);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert intial participant with node id %u, to exchange item of type: %d\n", node_id, item_type);
		return NULL;
	}

	exchange_item -> participants = participants;

	ret = pthread_mutex_init(&(exchange_item -> exch_item_lock), NULL);
	if (ret != )


	return exchange_item;
}


// used at the beginning of an offer posting to see if can immediately do RDMA writes
int lookup_exch_item(Exchange * exchange, uint8_t * fingerprint, ExchangeItemType item_type, Exchange_Item ** ret_item){

	Table * table;

	switch (item_type){
		case BID_ITEM:
			table = exchange -> bids;
			break;
		case OFFER_ITEM:
			table = exchange -> offers;
			break;
		case FUTURE_ITEM:
			table = exchange -> futures;
			break;
		default:
			fprintf(stderr, "Error: unknown exchange item type\n");
			*ret_item = NULL;
			return -1;
	}


	// create temp_exchange item populated with fingerprint and fingerprint_bytes
	Exchange_Item exchange_item;
	memcpy(exchange_item.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);

	// set to null if doesn't exist
	Exchange_Item * found_item = (Exchange_Item *) find_item_table(table, &exchange_item);

	*ret_item = found_item;

	if (found_item == NULL){
		return -1;
	}

	return 0;
}


int remove_exch_item(Exchange * exchange, uint8_t * fingerprint, ExchangeItemType item_type, Exchange_Item ** ret_item){
	
	Table * table;

	switch (item_type){
		case BID_ITEM:
			table = exchange -> bids;
			break;
		case OFFER_ITEM:
			table = exchange -> offers;
			break;
		case FUTURE_ITEM:
			table = exchange -> futures;
			break;
		default:
			fprintf(stderr, "Error: unknown exchange item type\n");
			*ret_item = NULL;
			return -1;
	}

	// create temp_exchange item populated with fingerprint and fingerprint_bytes
	Exchange_Item exchange_item;
	memcpy(exchange_item.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);

	// set to null if doesn't exist
	Exchange_Item * removed_item = (Exchange_Item *) remove_item_table(table, &exchange_item);

	*ret_item = removed_item;

	if (removed_item == NULL){
		return -1;
	}

	return 0;
}



int post_bid(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id) {

	int ret;

	// 1.) lookup if offer exists, then just do RDMA read (choosing "best" of participants) and return
	Exchange_Item * found_offer;
	ret = lookup_exch_item(exchange, fingerprint, OFFER_ITEM, &found_offer);
	if (found_offer){
		Deque * offer_participants = found_offer -> participants;
		int num_participants = offer_participants -> cnt;
		Deque_Item * cur_item = offer_participants -> head;
		Offer_Participant * offer_participant;
		uint64_t offer_node_id;
		// FOR NOW PRINTING ALL PARTICIPANTS AND POSTING SEND WR_ID with details of head of queue 
		// BUT MIGHT WANT TO CHANGE BASED ON TOPOLOGY!
		while (cur_item != NULL){
			offer_participant = (Offer_Participant *) cur_item -> item;
			offer_node_id = ((Offer_Participant *) offer_participant) -> node_id;
			ret = handle_bid_match_notify(exchange, offer_node_id, node_id, wr_id);
			if (ret != 0){
				fprintf(stderr, "Error: could not handle submitting a bid match notification\n");
				return -1;
			}
			cur_item = cur_item -> next;

			// for now just using the head of the queue to send bid match notif once
			break;
		}
		return 0;
	}
	
	// 2.) Lookup bids to see if exchange item exists
	// 		a.) If Yes, append participant to the deque
	//		b.) If no, create an exchange item and insert into table

	Exchange_Item * found_bid;
	ret = lookup_exch_item(exchange, fingerprint, BID_ITEM, &found_bid);
	if (found_bid){
		ret = enqueue(found_bid -> participants, new_participant);
		if (ret != 0){
			fprintf(stderr, "Error: could not enqueue new participant to exciting participants on bid\n");
			return -1;
		}
	}
	else{
		Exchange_Item * new_bid = init_exchange_item(fingerprint, BID_ITEM, node_id);
		if (new_bid == NULL){
			fprintf(stderr, "Error: could not initialize new bid exchange item\n");
			return -1;
		}
		ret = insert_item_table(exchange -> bids, new_bid);
		if (ret != 0){
			fprintf(stderr, "Error: could not insert new bid to exchange table\n");
			return -1;
		}
	}

	return 0;
}


int post_offer(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id) {

	int ret;
	
	// 1.) Lookup offers to see if exchange item exists
	// 		a.) If Yes, append participant to the deque
	//		b.) If no, create an exchange item and insert into table

	Exchange_Item * found_offer;
	Deque * offer_participants;
	ret = lookup_exch_item(exchange, fingerprint, OFFER_ITEM, &found_offer);
	if (found_offer){
		offer_participants = found_offer -> participants;
		ret = enqueue(offer_participants, new_participant);
		if (ret != 0){
			fprintf(stderr, "Error: could not enqueue new participant to existing participants on offer\n");
			return -1;
		}
	}
	else{
		Exchange_Item * new_offer = init_exchange_item(fingerprint, OFFER_ITEM, new_participant);
		if (new_offer == NULL){
			fprintf(stderr, "Error: could not initialize new offer exchange item\n");
			return -1;
		}
		ret = insert_item_table(exchange -> offers, new_offer);
		if (ret != 0){
			fprintf(stderr, "Error: could not insert new offer to exchange table\n");
			return -1;
		}
		offer_participants = new_offer -> participants;
	}


	// 2.) Lookup if this fingerprint + node_id combo was in the futures table
	//		- if so, remove it now that we know it was in offer table



	// 3.) Lookup if bid exists. If so, then queue post_sends to for all (or "available") bid participants informing them of location of objects, 
	//	   and remove participants from bid item (if no participants left, remove item). Free memory appropriately
	Exchange_Item * found_bid;
	ret = lookup_exch_item(exchange, fingerprint, BID_ITEM, &found_bid);
	if (found_bid){
		Deque * bid_participants = found_bid -> participants;
		int num_participants = bid_participants -> cnt;
		void * bid_participant;
		uint64_t bid_node_id;
		uint64_t bid_match_wr_id;
		// FOR NOW REMOVING ALL BID PARTICIPANTS BUT MIGHT WANT TO CHANGE BASED ON TOPOLOGY!
		while (!is_deque_empty(bid_participants)){
			ret = dequeue(bid_participants, &bid_participant);
			if (ret != 0){
				fprintf(stderr, "Error: could not dequeue bid participant\n");
				return -1;
			}
			bid_node_id = ((Bid_Participant *) bid_participant) -> node_id;
			bid_match_wr_id =  ((Bid_Participant *) bid_participant) -> wr_id;
			ret = handle_bid_match_notify(exchange, node_id, bid_node_id, bid_match_wr_id);
			if (ret != 0){
				fprintf(stderr, "Error: could not handle submitting a bid match notification\n");
				return -1;
			}
			// because we already sent info, we can free the bid participant
			free(bid_participant);
		}

		// Now bid is empty, so remove it
		Exchange_Item * removed_bid = remove_item_table(exchange -> bids, found_bid);
		
		// assert(removed_bid == found_bid)

		// destroy deque
		// should be the same pointer as bid_participants above
		destroy_deque(removed_bid -> participants);

		// now can free the exchange item
		free(removed_bid);
	}

	return 0;

}


int post_future(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id) {
	
	int ret;

	
	// 1.) Lookup futures to see if exchange item exists
	// 		a.) If Yes, append participant to the deque
	//		b.) If no, create an exchange item and insert into table
	Exchange_Item * found_future;
	ret = lookup_exch_item(exchange, fingerprint, FUTURE_ITEM, &found_future);
	if (found_future){
		Deque * future_participants = found_future -> participants;
		ret = enqueue(future_participants, new_participant);
		if (ret != 0){
			fprintf(stderr, "Error: could not enqueue new participant to existing participants on future\n");
			return -1;
		}
	}
	else{
		Exchange_Item * new_future = init_exchange_item(fingerprint, FUTURE_ITEM, new_participant);
		if (new_future == NULL){
			fprintf(stderr, "Error: could not initialize new future exchange item\n");
			return -1;
		}
		ret = insert_item_table(exchange -> futures, new_future);
		if (ret != 0){
			fprintf(stderr, "Error: could not insert new future to exchange table\n");
			return -1;
		}
	}

	return 0;
}