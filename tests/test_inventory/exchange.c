#include "exchange.h"

// there are items wihtin a participants deque
// each void * is really a uint32_t *
int participant_item_cmp(void * participant, void * other_participant){
	uint32_t node_id = ((Participant *) participant) -> node_id;
	uint32_t other_node_id = ((Participant *) other_participant) -> node_id;
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
	return least_sig_64bits % table_size;
}

Exchange * init_exchange() {

	Exchange * exchange = (Exchange *) malloc(sizeof(Exchange));
	if (exchange == NULL){
		fprintf(stderr, "Error: malloc failed allocating exchange table\n");
		return NULL;
	}

	exchange -> max_bids = EXCHANGE_MAX_BID_TABLE_ITEMS;
	exchange -> max_offers = EXCHANGE_MAX_OFFER_TABLE_ITEMS;
	exchange -> max_futures = EXCHANGE_MAX_FUTURE_TABLE_ITEMS;

	// these lookups need to be quick
	//	- lower load factor => more memory usage, but faster
	//		- the ratio of filled items before the table grows by 1 / load_factor
	float load_factor = EXCHANGE_TABLES_LOAD_FACTOR;
	float shrink_factor = EXCHANGE_TABLES_SHRINK_FACTOR;


	Hash_Func hash_func = &exchange_hash_func;
	Item_Cmp item_cmp = &exchange_item_cmp;

	// init bids and offers table
	Table * bids = init_table(EXCHANGE_MIN_BID_TABLE_ITEMS, EXCHANGE_MAX_BID_TABLE_ITEMS, load_factor, shrink_factor, hash_func, item_cmp);
	Table * offers = init_table(EXCHANGE_MIN_OFFER_TABLE_ITEMS, EXCHANGE_MAX_OFFER_TABLE_ITEMS, load_factor, shrink_factor, hash_func, item_cmp);
	Table * futures = init_table(EXCHANGE_MIN_FUTURE_TABLE_ITEMS, EXCHANGE_MAX_FUTURE_TABLE_ITEMS, load_factor, shrink_factor, hash_func, item_cmp);
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

	// Originally set participants array to NULL
	// this will be updated after joining the net
	// and calling update_init_exchange_with_net_info
	//	- this call occurs within init_sys in sys.c

	exchange -> participants = NULL;


	return exchange;
}


// This is called after init_net and once we see self_id and max_nodes
//	- There will be a different function to update the exchange info on joiners/leavers
int update_init_exchange_with_net_info(Exchange * exchange, uint32_t self_id, uint32_t max_nodes){

	// Now that we have self id and max nodes we can update the exchange structure

	exchange -> self_id = self_id;
	exchange -> max_nodes = max_nodes;

	// have +1 because master is index 0 an doesn't count towards max_nodes
	Participant * participants = (Participant *) malloc((max_nodes + 1) * sizeof(Participant));
	if (participants == NULL){
		fprintf(stderr, "Error: malloc failed to allocate participants array within update_init_exchange_with_net_info\n");
		return -1;
	}


	for (uint32_t i = 0; i < max_nodes + 1; i++){
		participants[i].node_id = i;
	}

	// Now set to this array instead of null
	exchange -> participants = participants;

	return 0;
}


int insert_participant_to_deque(Exchange * exchange, uint32_t node_id, Deque * deque, DequeEnd insert_end){

	Participant * participants = exchange -> participants;
	if (participants == NULL){
		fprintf(stderr, "Error: exchange's participants array is unitialized\n");
		return -1;
	}

	if (node_id > exchange -> max_nodes + 1){
		fprintf(stderr, "Error: node id is larger than maximum number of participants\n");
		return -1;
	}

	int ret = insert_deque(deque, insert_end, &(participants[node_id]));
	if (ret != 0){
		fprintf(stderr, "Error: could not insert participant with node id: %u to an exchange deque\n", node_id);
		return -1;
	}

	return 0;
}

// exchange items are initialized if fingerprint not found, 
// then create item based on whoever called post_order
// exchange items should only exist with >= 1 participants
// when there are 0 participants they should be deleted
//	- (but maybe have some sort of delay to prevent overhead of creation/deletion
//		if it is a hot-fingerprint)


Exchange_Item * init_exchange_item(Exchange * exchange, uint8_t * fingerprint, ExchangeItemType item_type, uint32_t node_id){

	int ret;

	Exchange_Item * exchange_item = (Exchange_Item *) malloc(sizeof(Exchange_Item));
	if (unlikely(exchange_item == NULL)){
		fprintf(stderr, "Error: malloc failed allocating exchange item\n");
		return NULL;
	}

	memcpy(exchange_item -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
	exchange_item -> item_type = item_type;

	Deque * participants = init_deque(&participant_item_cmp);
	if (unlikely(participants == NULL)){
		fprintf(stderr, "Error: could not initialize participants queue\n");
		return NULL;
	}

	ret = insert_participant_to_deque(exchange, node_id, participants, BACK_DEQUE);
	if (ret != 0){
		fprintf(stderr, "Error: failure to insert participant with id %u to initial init_exchange_item deque of type %d\n", node_id, item_type);
		return NULL;
	}

	exchange_item -> participants = participants;

	ret = pthread_mutex_init(&(exchange_item -> exch_item_lock), NULL);
	if (unlikely(ret != 0)){
		fprintf(stderr, "Error: could not initialize exchange item lock\n");
		return NULL;
	}

	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);

	uint64_t timestamp = time.tv_sec * 1e9 + time.tv_nsec;

	exchange_item -> lookup_cnt = 0;
	exchange_item -> timestamp_lookup = timestamp;
	exchange_item -> modify_cnt = 0;
	exchange_item -> timestamp_modify = timestamp;

	return exchange_item;
}


// ASSUMES THE CALLING THREAD ALREADY HOLDS LOCK
void destroy_exchange_item(Exchange_Item * exchange_item){

	// 1.) destroy deque
	Deque * participants = exchange_item -> participants;
	destroy_deque(participants, false);

	// 2.) destroy lock
	// unlock before destroying
	pthread_mutex_unlock(&(exchange_item -> exch_item_lock));
	pthread_mutex_destroy(&(exchange_item -> exch_item_lock));

	// 3.) free item
	free(exchange_item);
}

void update_item_lookup(Exchange_Item * exchange_item){
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	uint64_t timestamp = time.tv_sec * 1e9 + time.tv_nsec;

	pthread_mutex_lock(&(exchange_item -> exch_item_lock));
	exchange_item -> lookup_cnt += 1;
	exchange_item -> timestamp_lookup = timestamp;
	pthread_mutex_unlock(&(exchange_item -> exch_item_lock));

	return;
}

void update_item_modify(Exchange_Item * exchange_item) {

	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	uint64_t timestamp = time.tv_sec * 1e9 + time.tv_nsec;

	pthread_mutex_lock(&(exchange_item -> exch_item_lock));
	exchange_item -> modify_cnt += 1;
	exchange_item -> timestamp_modify = timestamp;
	pthread_mutex_unlock(&(exchange_item -> exch_item_lock));

	return;
}


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
	else{
		update_item_lookup(found_item);
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


// a bid is posted after ingesting a function and not having an argument fingerprints in local inventory.
// The fingerprint corresponding to argument(s) is posted
int post_bid(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id, Deque ** ret_matching_offer_participants) {

	int ret;

	// 1.) lookup if offer exists => if so, 
	// 		then set the offer participants return value for the exchange worker to start populating match notifications

	*ret_matching_offer_participants = NULL;
	Exchange_Item * found_offer;
	ret = lookup_exch_item(exchange, fingerprint, OFFER_ITEM, &found_offer);
	if (found_offer){
		*ret_matching_offer_participants = found_offer -> participants;
	}
	
	// - Even if there a match found, still have post this fingerprint + node to the exchange
	//		- there is a chance that the matching location's don't have data or somehow things get messed up
	//	- Wait until proper confirmation that this node has received the object (posting an offer_confirm_match_data order)
	//		before removing from bid table

	// 2.) Lookup bids to see if exchange item exists
	// 		a.) If Yes, append participant to the deque
	//		b.) If no, create an exchange item and insert into table
	Exchange_Item * found_bid;
	ret = lookup_exch_item(exchange, fingerprint, BID_ITEM, &found_bid);
	if (found_bid){
		Deque * bid_participants = found_bid -> participants;
		ret = insert_participant_to_deque(exchange, node_id, bid_participants, BACK_DEQUE);
		if (ret != 0){
			fprintf(stderr, "Error: failure to insert participant to bid deque after posting bid\n");
			return -1;
		}
		// update the modification counter and timestamp
		update_item_modify(found_bid);
	}
	else{
		Exchange_Item * new_bid = init_exchange_item(exchange, fingerprint, BID_ITEM, node_id);
		if (unlikely(new_bid == NULL)){
			fprintf(stderr, "Error: could not initialize new bid exchange item\n");
			return -1;
		}
		ret = insert_item_table(exchange -> bids, new_bid);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: could not insert new bid to exchange table\n");
			return -1;
		}
	}

	return 0;
}


// an offer is posted after computing a new result. The fingerprint corresponding to the encoded function is posted
//	- this fingerprint should already be in the future's table and should be moved to offer table, then this will trigger match
int post_offer(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id, Deque ** ret_matching_bid_participants) {

	int ret;
	*ret_matching_bid_participants = NULL;
	
	// 1.) Lookup offers to see if exchange item exists
	// 		a.) If Yes, append participant to the deque
	//		b.) If no, create an exchange item and insert into table

	Exchange_Item * found_offer;
	Deque * offer_participants;
	ret = lookup_exch_item(exchange, fingerprint, OFFER_ITEM, &found_offer);
	if (found_offer){
		offer_participants = found_offer -> participants;
		ret = insert_participant_to_deque(exchange, node_id, offer_participants, BACK_DEQUE);
		if (ret != 0){
			fprintf(stderr, "Error: failure to insert participant to offer deque after posting offer\n");
			return -1;
		}
		// update the modification counter and timestamp
		update_item_modify(found_offer);
	}
	else{
		Exchange_Item * new_offer = init_exchange_item(exchange, fingerprint, OFFER_ITEM, node_id);
		if (unlikely(new_offer == NULL)){
			fprintf(stderr, "Error: could not initialize new offer exchange item\n");
			return -1;
		}
		ret = insert_item_table(exchange -> offers, new_offer);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: could not insert new offer to exchange table\n");
			return -1;
		}
		offer_participants = new_offer -> participants;
	}


	// 2.) Lookup if bid exists. If so, set the bids 
	Exchange_Item * found_bid;
	ret = lookup_exch_item(exchange, fingerprint, BID_ITEM, &found_bid);
	if (found_bid){
		*ret_matching_bid_participants = found_bid -> participants;
	}

	// 3.) Remove from futures table
	//		- it should already exist, but for flexibility not reporting error if so
	Exchange_Item * found_future;
	ret = lookup_exch_item(exchange, fingerprint, FUTURE_ITEM, &found_future);
	uint64_t num_copies_removed;

	Participant participant;
	participant.node_id = node_id;
	if (found_future){
		Deque * future_participants = found_future -> participants;
		// could use remove_if_eq_accel here and set max_removed = 1 to speed things up
		num_copies_removed = remove_if_eq_deque(future_participants, &participant, false);
		// not returning an error here, just printing a message
		if (unlikely(num_copies_removed != 1)){
			fprintf(stderr, "Error: was expecting to remove one copy of fingerprint from futures table after an offer from node %u, but removed: %lu",
								node_id, num_copies_removed);
		}
		// update the modification counter and timestamp
		update_item_modify(found_future);

		// when removing, first acquire lock
		// if there are no more participants remove the item
		pthread_mutex_lock(&(found_future -> exch_item_lock));
		uint64_t participant_cnt = get_count_deque(future_participants);
		// there are no more participants, so we can remove this item from table and free its memory
		if (participant_cnt == 0){
			Exchange_Item * removed_item = (Exchange_Item *) remove_item_table(exchange -> futures, found_future);
			// assert(removed_item == found_bid)
			if (unlikely(removed_item != found_future)){
				fprintf(stderr, "Error: issue removing exchange bid item after a offer_match confirmation from node: %u\n", node_id);
				return -1;
			}

			destroy_exchange_item(found_future);
		}
		else{
			// there were more participants, so we aren't destroying but need to release lock
			pthread_mutex_unlock(&(found_future -> exch_item_lock));
		}
	}

	return 0;

}


// this order type is used after a bid was placed and that node received a match notification
// after the match notfiication when the node whose bid it was actually receives object
// they will post this order

// it doesn't do any triggering of new notification, but it adds this node to the offer participants
// for fingerprint and removes this node from that exchange item's bid table

// if this node was not in the bid table for corresponding fignerprint there was an error
int post_offer_confirm_match_data(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id) {

	int ret;
	
	// 1.) Lookup offers to see if exchange item exists
	// 		a.) If Yes, append participant to the deque
	//		b.) If no, create an exchange item and insert into table
	Exchange_Item * found_offer;
	Deque * offer_participants;
	ret = lookup_exch_item(exchange, fingerprint, OFFER_ITEM, &found_offer);
	if (found_offer){
		offer_participants = found_offer -> participants;
		ret = insert_participant_to_deque(exchange, node_id, offer_participants, BACK_DEQUE);
		if (ret != 0){
			fprintf(stderr, "Error: failure to insert participant to offer deque after posting offer confirm match data\n");
			return -1;
		}
		// update the modification counter and timestamp
		update_item_modify(found_offer);
	}
	else{
		Exchange_Item * new_offer = init_exchange_item(exchange, fingerprint, OFFER_ITEM, node_id);
		if (unlikely(new_offer == NULL)){
			fprintf(stderr, "Error: could not initialize new offer exchange item\n");
			return -1;
		}
		ret = insert_item_table(exchange -> offers, new_offer);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: could not insert new offer to exchange table\n");
			return -1;
		}
		offer_participants = new_offer -> participants;
	}


	// 2.) Now remove bid from table
	//		- should be an error if the bid from this fingerprint + node_id is not there
	//			- only would occur if this item (bid) was cached out of the exchange table before confirming data

	Exchange_Item * found_bid;
	ret = lookup_exch_item(exchange, fingerprint, BID_ITEM, &found_bid);
	uint64_t num_copies_removed;
	Participant participant;
	participant.node_id = node_id;
	if (found_bid){
		Deque * bid_participants = found_bid -> participants;
		num_copies_removed = remove_if_eq_deque(bid_participants, &participant, false);
		// not returning an error here, just printing a message
		if (unlikely(num_copies_removed != 1)){
			fprintf(stderr, "Error: was expecting to remove one copy of fingerprint from bid table after an offer_confirm_match_data from node %u, but removed: %lu",
								node_id, num_copies_removed);
		}
		// update the modification counter and timestamp
		update_item_modify(found_bid);

		// when removing, first acquire lock
		// if there are no more participants remove the item
		pthread_mutex_lock(&(found_bid -> exch_item_lock));
		uint64_t participant_cnt = get_count_deque(bid_participants);
		// there are no more participants, so we can remove this item from table and free its memory
		if (participant_cnt == 0){
			Exchange_Item * removed_item = (Exchange_Item *) remove_item_table(exchange -> bids, found_bid);
			// assert(removed_item == found_bid)
			if (unlikely(removed_item != found_bid)){
				fprintf(stderr, "Error: issue removing exchange bid item after a offer_match confirmation from node: %u\n", node_id);
				return -1;
			}

			destroy_exchange_item(found_bid);
		}
		else{
			// there were more participants, so we aren't destroying but need to release lock
			pthread_mutex_unlock(&(found_bid -> exch_item_lock));
		}
	}
	else{
		// for now not returning error, but printing out
		fprintf(stderr, "Error: was expecting the bid to existi after posting an offer_confirm_match_data with node id: %u. But now bid found for fingerprint\n", node_id);
		return 0;
	}

	return 0;
}


// a future order is posted after ingesting a function. The fingerprint corresponding to encoded function is posted
int post_future(Exchange * exchange, uint8_t * fingerprint, uint32_t node_id) {
	
	int ret;

	
	// 1.) Lookup futures to see if exchange item exists
	// 		a.) If Yes, append participant to the deque
	//		b.) If no, create an exchange item and insert into table
	Exchange_Item * found_future;
	ret = lookup_exch_item(exchange, fingerprint, FUTURE_ITEM, &found_future);
	if (found_future){
		Deque * future_participants = found_future -> participants;
		ret = insert_participant_to_deque(exchange, node_id, future_participants, BACK_DEQUE);
		if (ret != 0){
			fprintf(stderr, "Error: failure to insert participant to future deque after posting future order\n");
			return -1;
		}
		update_item_modify(found_future);
	}
	else{
		Exchange_Item * new_future = init_exchange_item(exchange, fingerprint, FUTURE_ITEM, node_id);
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


// If offer is the trigger, then matching participants will be the set of bids that the exchange needs to send with the trigger_node_id as data location
// If bid is the trigger, then matching participants will be the set of offer locations that the exchannge needs to send back to the trigger node
int generate_match_ctrl_messages(uint32_t self_id, uint32_t trigger_node_id, bool is_offer_trigger, uint8_t * fingerprint, Deque * matching_particpants, uint32_t * ret_num_ctrl_messages, Ctrl_Message ** ret_ctrl_messages){



	*ret_num_ctrl_messages = 0;
	*ret_ctrl_messages = NULL;

	// ensure that we only generate matching participants if non-null
	if (matching_particpants == NULL){
		return 0;
	}


	// Now need to iterate over all the participants and create a message for each one
	pthread_mutex_lock(&(matching_particpants -> list_lock));

	uint32_t matching_particpants_cnt = (uint32_t) matching_particpants -> cnt;
	if (matching_particpants_cnt == 0){
		pthread_mutex_unlock(&(matching_particpants -> list_lock));
		return 0;
	}

	uint32_t num_response_messages;
	if (is_offer_trigger){
		num_response_messages = matching_particpants_cnt;
	}
	else{
		// the ceil of number of response messages
		num_response_messages = matching_particpants_cnt / MAX_FINGERPRINT_MATCH_LOCATIONS + ((matching_particpants_cnt % MAX_FINGERPRINT_MATCH_LOCATIONS) != 0);
	}


	Ctrl_Message * match_messages = (Ctrl_Message *) malloc(num_response_messages * sizeof(Ctrl_Message));
	if (match_messages == NULL){
		fprintf(stderr, "Error: malloc failed to allocate memory for generating match ctrl messages\n");
		pthread_mutex_unlock(&(matching_particpants -> list_lock));
		return -1;
	}


	Inventory_Message * inventory_message;
	Fingerprint_Match * fingerprint_match;
	Deque_Item * cur_deque_item = matching_particpants -> head;

	// If the offer was the trigger, we need to send a set of different control messages indicating this offer node's location to each of the bid participants
	if (is_offer_trigger){

		uint32_t i = 0;
		uint32_t bid_node_id;
		while (cur_deque_item != NULL){
			bid_node_id = ((Participant *) cur_deque_item -> item) -> node_id;

			match_messages[i].header.source_node_id = self_id;
			match_messages[i].header.dest_node_id = bid_node_id;
			match_messages[i].header.message_class = INVENTORY_CLASS;

			inventory_message = (Inventory_Message *) (&(match_messages[i].contents));
			inventory_message -> message_type = FINGERPRINT_MATCH;

			fingerprint_match = (Fingerprint_Match *) inventory_message -> message;
			memcpy(fingerprint_match -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
			fingerprint_match -> num_nodes = 1;
			(fingerprint_match -> node_ids)[0] = trigger_node_id; 

			cur_deque_item = cur_deque_item -> next;
			i++;
		}
	}
	// If the bid was the trigger, we need to send matching_particpants_cnt / MAX_FINGERPRINT_MATCH_LOCATIONS messages to the node with all the locations of the offers
	else{
		
		uint32_t offer_node_id;
		uint32_t match_node_id_ind;
		for (int message_ind = 0; message_ind < num_response_messages; message_ind++){
			
			match_messages[message_ind].header.source_node_id = self_id;
			match_messages[message_ind].header.dest_node_id = trigger_node_id;
			match_messages[message_ind].header.message_class = INVENTORY_CLASS;

			inventory_message = (Inventory_Message *) (&(match_messages[message_ind].contents));
			inventory_message -> message_type = FINGERPRINT_MATCH;

			fingerprint_match = (Fingerprint_Match *) inventory_message -> message;
			memcpy(fingerprint_match -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
			fingerprint_match -> num_nodes = 0;

			match_node_id_ind = 0;
			while ((cur_deque_item != NULL) && (match_node_id_ind < MAX_FINGERPRINT_MATCH_LOCATIONS)){
				offer_node_id = ((Participant *) cur_deque_item -> item) -> node_id;
				fingerprint_match -> num_nodes += 1;
				(fingerprint_match -> node_ids)[match_node_id_ind] = offer_node_id; 
				cur_deque_item = cur_deque_item -> next;
				match_node_id_ind++;
			}
		}

	}

	pthread_mutex_unlock(&(matching_particpants -> list_lock));

	*ret_num_ctrl_messages = num_response_messages;
	*ret_ctrl_messages = match_messages;

	return 0;
}



int do_exchange_function(Exchange * exchange, Ctrl_Message * ctrl_message, uint32_t * ret_num_ctrl_messages, Ctrl_Message ** ret_ctrl_messages) {

	int ret;

	uint32_t node_id = ctrl_message -> header.source_node_id;

	Exch_Message * exch_message = (Exch_Message *) ctrl_message -> contents;

	ExchMessageType exch_message_type = exch_message -> message_type;
	uint8_t * fingerprint = exch_message -> fingerprint;
	
	Deque * matching_particpants;

	// Default return values
	// generate_match_ctrl_messages may override these...
	*ret_num_ctrl_messages = 0;
	*ret_ctrl_messages = NULL;

	switch(exch_message_type){			
			case BID_ORDER:
				ret = post_bid(exchange, fingerprint, node_id, &matching_particpants);
				if (unlikely(ret != 0)){
					fprintf(stderr, "Error: could not post bid from node_id %u\n", node_id);
				}
				ret = generate_match_ctrl_messages(exchange -> self_id, node_id, false, fingerprint, matching_particpants, ret_num_ctrl_messages, ret_ctrl_messages);
				if (unlikely(ret != 0)){
					fprintf(stderr, "Error: could not generate match notification message after posting bid from node_id %u\n", node_id);
				}
				break;
			case OFFER_ORDER:
				ret = post_offer(exchange, fingerprint, node_id, &matching_particpants);
				if (unlikely(ret != 0)){
					fprintf(stderr, "Error: could not post bid from node_id %u\n", node_id);
				}
				ret = generate_match_ctrl_messages(exchange -> self_id, node_id, true, fingerprint, matching_particpants, ret_num_ctrl_messages, ret_ctrl_messages);
				if (unlikely(ret != 0)){
					fprintf(stderr, "Error: could not generate match notification message after posting bid from node_id %u\n", node_id);
				}
				break;
			case OFFER_CONFIRM_MATCH_DATA_ORDER:
				ret = post_offer_confirm_match_data(exchange, fingerprint, node_id);
				if (unlikely(ret != 0)){
					fprintf(stderr, "Error: could not post offer_confirm_match_data from node_id %u\n", node_id);
				}
				break;
			case FUTURE_ORDER:
				ret = post_future(exchange, fingerprint, node_id);
				if (unlikely(ret != 0)){
					fprintf(stderr, "Error: could not post offer_confirm_match_data from node_id %u\n", node_id);
				}
				break;
			default:
				fprintf(stderr, "Exchange worker saw unknown exchange messsage type of %d\n", exch_message_type);
				break;
	}

	return ret;

}


void exch_message_type_to_str(char * buf, ExchMessageType exch_message_type){

	switch(exch_message_type){
			case BID_ORDER:
				strcpy(buf, "BID_ORDER");
				return;
			case BID_CANCEL_ORDER:
				strcpy(buf, "BID_CANCEL_ORDER");
				return;
			case BID_Q:
				strcpy(buf, "BID_Q");
				return;
			case BID_Q_RESPONSE:
				strcpy(buf, "BID_Q_RESPONSE");
				return;
			case OFFER_ORDER:
				strcpy(buf, "OFFER_ORDER");
				return;
			case OFFER_CONFIRM_MATCH_DATA_ORDER:
				strcpy(buf, "OFFER_CONFIRM_MATCH_DATA_ORDER");
				return;
			case OFFER_CANCEL_ORDER:
				strcpy(buf, "OFFER_CANCEL_ORDER");
				return;
			case OFFER_Q:
				strcpy(buf, "OFFER_Q");
				return;
			case OFFER_Q_RESPONSE:
				strcpy(buf, "OFFER_Q_RESPONSE");
				return;
			case FUTURE_ORDER:
				strcpy(buf, "FUTURE_ORDER");
				return;
			case FUTURE_CANCEL_ORDER:
				strcpy(buf, "FUTURE_CANCEL_ORDER");
				return;
			case FUTURE_Q:
				strcpy(buf, "FUTURE_Q");
				return;
			case FUTURE_Q_RESPONSE:
				strcpy(buf, "FUTURE_Q_RESPONSE");
				return;
			default:
				strcpy(buf, "UNKNOWN_EXCH_MESSAGE_TYPE");
				return;
	}
}