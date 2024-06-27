#include "exchange.h"

// floor(log2(index) + 1)
// number of low order 1's bits in table_size bitmask
static const char LogTable512[512] = {
	0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9
};

int exchange_item_cmp(void * exchange_item, void * other_item) {
	uint64_t fingerprint_bytes = (uint64_t) (Exchange_Item *) exchange_item -> fingerprint_bytes;
	unsigned char * item_fingerprint = (Exchange_Item *) exchange_item -> fingerprint;
	unsigned char * other_fingerprint = (Exchange_Item *) other_item -> fingerprint;
	int cmp_res = memcmp(item_fingerprint, other_fingerprint, fingerprint_bytes);
	return cmp_res;
}	


uint64_t exchange_hash_func(void * exchange_item, uint64_t table_size) {
	Exchange_Item * item_casted = (Exchange_Item *) exchange_item;
	unsigned char * fingerprint = item_casted -> fingerprint;
	int fingerprint_bytes = (int) item_casted -> fingerprint_bytes;
	uint64_t least_sig_64bits = fingerprint_to_least_sig64(fingerprint, fingerprint_bytes);
	// bitmask should be the lower log(2) lower bits of table size.
	// i.e. if table_size = 2^12, we should have a bit mask of 12 1's
	uint64_t bit_mask;
	uint64_t hash_ind;

	// optimization for power's of two table sizes, no need for leading-zero/table lookup or modulus
	// but might hurt due to branch prediction...
	// if (__builtin_popcountll(table_size) == 1){
	// 	bit_mask = table_size - 1;
	// 	hash_ind = least_sig_64bits & bit_mask;
	// 	return hash_ind;
	// }

	int leading_zeros = __builtin_clzll(table_size);
	// 64 bits as table_size type
	// taking ceil of power of 2, then subtracing 1 to get low-order 1's bitmask
	int num_bits = (64 - leading_zeros) + 1;
	bit_mask = (1L << num_bits) - 1;
	hash_ind = (least_sig_64bits & bit_mask) % table_size;
	return hash_ind;
}


// Ref: "https://graphics.stanford.edu/~seander/bithacks.html#ModulusDivisionEasy"
uint64_t exchange_hash_func_no_builtin(void * exchange_item, uint64_t table_size) {
	Exchange_Item * item_casted = (Exchange_Item *) exchange_item;
	unsigned char * fingerprint = item_casted -> fingerprint;
	int fingerprint_bytes = (int) item_casted -> fingerprint_bytes;
	uint64_t least_sig_64bits = fingerprint_to_least_sig64(fingerprint, fingerprint_bytes);
	// bitmask should be the lower log(2) lower bits of table size.
	// i.e. if table_size = 2^12, we should have a bit mask of 12 1's
	uint64_t bit_mask;
	uint64_t hash_ind;
	
	// here we set the log table to be floor(log(ind) + 1), which represents number of bits we should have in bitmask before modulus
	uint64_t num_bits;
	register uint64_t temp;
	if (temp = table_size >> 56){
		num_bits = 56 + LogTable512[temp];
	}
	else if (temp = table_size >> 48) {
		num_bits = 48 + LogTable512[temp];
	}
	else if (temp = table_size >> 40){
		num_bits = 40 + LogTable512[temp];
	}
	else if (temp = table_size >> 32){
		num_bits = 32 + LogTable512[temp];
	}
	else if (temp = table_size >> 24){
		num_bits = 24 + LogTable512[temp];
	}
	else if (temp = table_size >> 16){
		num_bits = 18 + LogTable512[temp];
	}
	else if (temp = table_size >> 8){
		num_bits = 8 + LogTable512[temp];
	}
	else{
		num_bits = LogTable512[temp];
	}
	bit_mask = (1 << num_bits) - 1;
	// now after computing bit_mask the hash_ind may be greater than table_size
	hash_ind = (least_sig_64bits & bit_mask) % table_size;
	
	return hash_ind;
}


Exchange * init_exchange(uint64_t id, uint64_t start_val, uint64_t end_val, uint64_t max_bids, uint64_t max_offers) {

	Exchange * exchange = (Exchange *) malloc(sizeof(Exchange));
	if (exchange_table == NULL){
		fprintf(stderr, "Error: malloc failed allocating exchange table\n");
		return NULL;
	}

	exchange -> id = id;
	exchange -> start_val = start_val;
	exchange -> end_val = end_val;
	exchange -> max_bids = max_bids;
	exchange -> max_offers = max_offers;

	// DEFAULT hyperparameters for hash tables
	uint64_t min_items = 1 << 20;
	float load_factor = .5f;
	float shrink_factor = .1f;
	Hash_Func hash_func = &exchange_hash_func;
	Item_Cmp item_cmp = &exchange_item_cmp;

	// init bids and offers table
	Table * bids = init_table(min_size, max_bids, load_factor, shrink_factor, hash_func, item_cmp);
	Table * offers = init_table(min_size, max_offers, load_factor, shrink_factor, hash_func, item_cmp);
	if ((bids == NULL) || (offers == NULL)){
		fprintf(stderr, "Error: could not initialize bids and offer tables\n");
		return NULL;
	}

	exchange -> bids = bids;
	exchange -> offers = offers;

	pthread_mutex_init(&(exchange -> exchange_lock), NULL);

	return exchange;
}

// exchange items are initialized if fingerprint not found, 
// then create item based on whoever called post_bid or post_offer
// exchange items should only exist with >= 1 participants
Exchange_Item * init_exchange_item(unsigned char * fingerprint, uint8_t fingerprint_bytes, uint64_t data_bytes, ExchangeItemType item_type, Participant * participant){

	int ret;

	Exchange_Item * exchange_item = (Exchange_Item *) malloc(sizeof(Exchange_Item));
	if (exchange_item == NULL){
		fprintf(stderr, "Error: malloc failed allocating exchange item\n");
		return NULL;
	}

	exchange_item -> fingerprint = fingerprint;
	exchange_item -> fingerprint_bytes = fingerprint_bytes;
	exchange_item -> data_bytes = data_bytes;
	exchange_item -> item_type = item_type;

	Deque * participants = init_deque();
	if (participants == NULL){
		fprintf(stderr, "Error: could not initialize participants queue\n");
		return NULL;
	}

	ret = enqueue(participants, participant);
	if (ret != 0){
		fprintf(stderr, "Error: could not enqueue intial participant to exchange item\n");
		return NULL;
	}

	exchange_item -> participants = participants;

	pthread_mutex_init(&(exchange_item -> item_lock), NULL);

	return exchange_item;
}


Participant * init_participant(uint64_t location_id, uint64_t addr, uint32_t rkey){

	Participant * participant = (Participant *) malloc(sizeof(Participant));
	if (participant == NULL){
		fprintf(stderr, "Error: malloc failed allocating participant\n");
		return NULL;
	}

	participant -> location_id = location_id;
	participant -> addr = addr;
	participant -> rkey = rkey;

	return participant;
}



// used at the beginning of an offer posting to see if can immediately do RDMA writes
int lookup_bid(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, Exchange_Item ** ret_item){

	Table * bids = exchange -> bids;

	// create temp_exchange item populated with fingerprint and fingerprint_bytes
	Exchange_Item exchange_item;
	exchange_item.fingerprint = fingerprint;
	exchange_item.fingerprint_bytes = fingerprint_bytes;

	Exchange_Item * found_item = (Exchange_Item *) find_item(bids, &exchange_item);

	*ret_item = found_item;

	if (found_item == NULL){
		return -1;
	}

	return 0;
}

// used at the beginning of a bid posting to see if can immediately do an RDMA
int lookup_offer(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, Exchange_Item ** ret_item){

	Table * offers = exchange -> offers;

	// create temp_exchange item populated with fingerprint and fingerprint_bytes
	Exchange_Item exchange_item;
	exchange_item.fingerprint = fingerprint;
	exchange_item.fingerprint_bytes = fingerprint_bytes;

	Exchange_Item * found_item = (Exchange_Item *) find_item(offers, &exchange_item);

	*ret_item = found_item;

	if (found_item == NULL){
		return -1;
	}

	return 0;
}



int remove_bid(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, Exchange_Item ** ret_item){
	Table * bids = exchange -> bids;

	// create temp_exchange item populated with fingerprint and fingerprint_bytes
	Exchange_Item exchange_item;
	exchange_item.fingerprint = fingerprint;
	exchange_item.fingerprint_bytes = fingerprint_bytes;

	Exchange_Item * removed_item = (Exchange_Item *) remove_item(bids, &exchange_item);

	*ret_item = removed_item;

	if (removed_item == NULL){
		return -1;
	}

	return 0;
}

int remove_offer(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, Exchange_Item ** ret_item){
	Table * offers = exchange -> offers;

	// create temp_exchange item populated with fingerprint and fingerprint_bytes
	Exchange_Item exchange_item;
	exchange_item.fingerprint = fingerprint;
	exchange_item.fingerprint_bytes = fingerprint_bytes;

	Exchange_Item * removed_item = (Exchange_Item *) remove_item(offers, &exchange_item);

	*ret_item = removed_item;

	if (removed_item == NULL){
		return -1;
	}

	return 0;
}



int post_bid(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, uint64_t data_bytes, uint64_t location_id, uint64_t addr, uint32_t rkey) {

	int ret;

	// 1.) lookup if offer exists, then just do RDMA read (choosing "best" of participants) and return
	Exchange_Item * found_offer;
	ret = lookup_offer(exchange, fingerprint, fingerprint_bytes, &found_offer);
	if (found_offer){
		Deque * offer_participants = found_offer -> participants;
		int num_participants = offer_participants -> cnt;

		// FOR NOW: CRUDELY SIMULATING DOING RDMA TRANSFERS
		// BIG TODO!!!
		printf("Found %d participants with offers for fingerprint. Would be reading from any of: ");
		print_hex(fingerprint, fingerprint_bytes);
		Deque_Item * cur_item = offer_participants -> head;
		Participant * offer_participant;
		while (cur_item != NULL){
			offer_participant = (Participant *) cur_item -> item;
			printf("\tLocation ID: %lu\n\tAddr: %lu\n\tRkey: %lu\n\n", offer_participant -> location_id, offer_participant -> addr, offer_participant -> rkey);
			cur_item = cur_item -> next;
		}
		return 0;
	}

	// Otherwise...
	// 2.) Build participant

	Particiapnt * new_participant = init_participant(location_id, addr, rkey);
	if (new_participant == NULL){
		fprintf(stderr, "Error: could not initialize participant\n");
		return -1;
	}
	
	// 3.) Lookup bids to see if exchange item exists
	// 		a.) If Yes, append participant to the deque
	//		b.) If no, create an exchange item and insert into table

	Exchange_Item * found_bid;
	ret = lookup_offer(exchange, fingerprint, fingerprint_bytes, &found_bid);
	if (found_bid){
		ret = enqueue(found_bid -> participants, new_participant);
		if (ret != 0){
			fprintf(stderr, "Error: could not enqueue new participant to exciting participants on bid\n");
			return -1;
		}
	}
	else{
		ExchangeItemType item_type = BID;
		Exchange_Item * new_bid = init_exchange_item(fingerprint, fingerprint_bytes, data_bytes, item_type, new_participant);
		if (new_bid == NULL){
			fprintf(stderr, "Error: could not initialize new bid exchange item\n");
			return -1;
		}
		ret = insert_item(exchange -> bids, new_bid);
		if (ret != 0){
			fprintf(stderr, "Error: could not insert new bid to exchange table\n");
			return -1;
		}
	}

	return 0;
}


int post_offer(Exchange * exchange, unsigned char * fingerprint, uint8_t fingerprint_bytes, uint64_t data_bytes, uint64_t location_id, uint64_t addr, uint32_t rkey) {

	int ret;

	// 1.) Lookup if bid exists. If so, then queue RDMA writes for all (or "available") participants, 
	//	   and remove participants from bid item (if no participants left, remove item). Free memory appropriately
	Exchange_Item * found_bid;
	ret = lookup_bid(exchange, fingerprint, fingerprint_bytes, &found_bid);
	if (found_bid){
		Deque * bid_participants = found_bid -> participants;
		int num_participants = bid_participants -> cnt;

		// FOR NOW: CRUDELY SIMULATING DOING RDMA TRANSFERS
		// BIG TODO!!!
		printf("Found %d participants with bids for fingerprint. Would be writing to all of: ");
		print_hex(fingerprint, fingerprint_bytes);
		Deque_Item * cur_item = bid_participants -> head;
		Participant * bid_participant;
		while (cur_item != NULL){
			bid_participant = (Participant *) cur_item -> item;
			printf("\tLocation ID: %lu\n\tAddr: %lu\n\tRkey: %lu\n\n", bid_participant -> location_id, bid_participant -> addr, bid_participant -> rkey);
			cur_item = cur_item -> next;
		}

		// SHOULD ALSO BE FREEING MEMORY HERE!
	}

	// 2.) Build participant

	Particiapnt * new_participant = init_participant(location_id, addr, rkey);
	if (new_participant == NULL){
		fprintf(stderr, "Error: could not initialize participant\n");
		return -1;
	}
	
	// 3.) Lookup offers to see if exchange item exists
	// 		a.) If Yes, append participant to the deque
	//		b.) If no, create an exchange item and insert into table

	Exchange_Item * found_offer;
	ret = lookup_offer(exchange, fingerprint, fingerprint_bytes, &found_offer);
	if (found_bid){
		ret = enqueue(found_offer -> participants, new_participant);
		if (ret != 0){
			fprintf(stderr, "Error: could not enqueue new participant to exciting participants on offer\n");
			return -1;
		}
	}
	else{
		ExchangeItemType item_type = OFFER;
		Exchange_Item * new_offer = init_exchange_item(fingerprint, fingerprint_bytes, data_bytes, item_type, new_participant);
		if (new_offer == NULL){
			fprintf(stderr, "Error: could not initialize new offer exchange item\n");
			return -1;
		}
		ret = insert_item(exchange -> offers, new_offer);
		if (ret != 0){
			fprintf(stderr, "Error: could not insert new offer to exchange table\n");
			return -1;
		}
	}

	return 0;

}