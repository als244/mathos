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


int client_item_cmp(void * client_item, void * other_item) {
	uint64_t location_a = ((Client_Connection *) client_item) -> location_id;
	uint64_t location_b = ((Client_Connection *) other_item) -> location_id;
	return location_a - location_b;
}

uint64_t client_hash_func(void * client_item, uint64_t table_size) {
	uint64_t key = ((Client_Connection *) client_item) -> location_id;
	// Taken from "https://github.com/shenwei356/uint64-hash-bench?tab=readme-ov-file"
	// Credit: Thomas Wang
	key = (key << 21) - key - 1;
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);
	return key;
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
	uint64_t least_sig_64bits = fingerprint_to_least_sig64(fingerprint, FINGERPRINT_NUM_BYTES);
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


Exchange * init_exchange(uint64_t id, uint64_t start_val, uint64_t end_val, uint64_t max_bids, uint64_t max_offers, uint64_t max_futures, uint64_t max_clients) {

	Exchange * exchange = (Exchange *) malloc(sizeof(Exchange));
	if (exchange == NULL){
		fprintf(stderr, "Error: malloc failed allocating exchange table\n");
		return NULL;
	}

	exchange -> id = id;
	exchange -> start_val = start_val;
	exchange -> end_val = end_val;
	exchange -> max_bids = max_bids;
	exchange -> max_offers = max_offers;

	// DEFAULT hyperparameters for hash tables
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
		fprintf(stderr, "Error: could not initialize bids and offer tables\n");
		return NULL;
	}

	exchange -> bids = bids;
	exchange -> offers = offers;

	pthread_mutex_init(&(exchange -> exchange_lock), NULL);


	uint64_t client_table_min_size = 1 << 6;
	if (client_table_min_size > max_clients){
		client_table_min_size = max_clients;
	}
	Hash_Func hash_func_client = &client_hash_func;
	Item_Cmp item_cmp_client = &client_item_cmp; 
	Table * clients = init_table(client_table_min_size, max_clients, load_factor, shrink_factor, hash_func_client, item_cmp_client);
	if (clients == NULL){
		fprintf(stderr, "Error: could not initialize clients table\n");
		return NULL;
	}

	exchange -> clients = clients;

	exchange -> exchange_qp = NULL;

	return exchange;
}

// exchange items are initialized if fingerprint not found, 
// then create item based on whoever called post_bid or post_offer
// exchange items should only exist with >= 1 participants

// participant is either of type (Bid_Participant or Offer_Participant)
// but doesn't matter for enqueing to deque which assumes void *
Exchange_Item * init_exchange_item(uint8_t * fingerprint, uint64_t data_bytes, ExchangeItemType item_type, void * participant){

	int ret;

	Exchange_Item * exchange_item = (Exchange_Item *) malloc(sizeof(Exchange_Item));
	if (exchange_item == NULL){
		fprintf(stderr, "Error: malloc failed allocating exchange item\n");
		return NULL;
	}

	memcpy(exchange_item -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
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


Bid_Participant * init_bid_participant(uint64_t location_id, uint64_t wr_id){

	Bid_Participant * participant = (Bid_Participant *) malloc(sizeof(Bid_Participant));
	if (participant == NULL){
		fprintf(stderr, "Error: malloc failed allocating participant\n");
		return NULL;
	}

	participant -> location_id = location_id;
	participant -> wr_id = wr_id;

	return participant;
}

Offer_Participant * init_offer_participant(uint64_t location_id){

	Offer_Participant * participant = (Offer_Participant *) malloc(sizeof(Offer_Participant));
	if (participant == NULL){
		fprintf(stderr, "Error: malloc failed allocating participant\n");
		return NULL;
	}

	participant -> location_id = location_id;

	return participant;
}

Future_Participant * init_future_participant(uint64_t location_id){

	Future_Participant * participant = (Future_Participant *) malloc(sizeof(Future_Participant));
	if (participant == NULL){
		fprintf(stderr, "Error: malloc failed allocating participant\n");
		return NULL;
	}

	participant -> location_id = location_id;

	return participant;
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



int post_bid(Exchange * exchange, uint8_t * fingerprint, uint64_t data_bytes, uint64_t location_id, uint64_t wr_id) {

	int ret;

	// 1.) lookup if offer exists, then just do RDMA read (choosing "best" of participants) and return
	Exchange_Item * found_offer;
	ret = lookup_exch_item(exchange, fingerprint, OFFER_ITEM, &found_offer);
	if (found_offer){
		Deque * offer_participants = found_offer -> participants;
		int num_participants = offer_participants -> cnt;

		// FOR NOW: CRUDELY SIMULATING DOING RDMA TRANSFERS
		// BIG TODO!!!
		printf("Found %d participants with offers for fingerprint: ", num_participants);
		print_hex(fingerprint, FINGERPRINT_NUM_BYTES);
		printf("Would be posting sends (& reading from) any of:\n");
		Deque_Item * cur_item = offer_participants -> head;
		Offer_Participant * offer_participant;

		// FOR NOW PRINTING ALL PARTICIPANTS AND POSTING SEND WR_ID with details of tail of queue 
		// BUT MIGHT WANT TO CHANGE BASED ON TOPOLOGY!
		while (cur_item != NULL){
			offer_participant = (Offer_Participant *) cur_item -> item;
			printf("\tLocation ID: %lu\n\tData Bytes: %lu\n\n", offer_participant -> location_id, found_offer -> data_bytes);
			
			// SHOULD BE POSTING SEND HERE WITH data = (Location ID + Addr + Rkey) of offer_participant and sending to the function args (location_id, wr_id)

			cur_item = cur_item -> next;
		}
		return 0;
	}

	// Otherwise...
	// 2.) Build participant

	Bid_Participant * new_participant = init_bid_participant(location_id, wr_id);
	if (new_participant == NULL){
		fprintf(stderr, "Error: could not initialize participant\n");
		return -1;
	}
	
	// 3.) Lookup bids to see if exchange item exists
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
		Exchange_Item * new_bid = init_exchange_item(fingerprint, data_bytes, BID_ITEM, new_participant);
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


int post_offer(Exchange * exchange, uint8_t * fingerprint, uint64_t data_bytes, uint64_t location_id) {

	int ret;

	// 1.) Build participant

	Offer_Participant * new_participant = init_offer_participant(location_id);
	if (new_participant == NULL){
		fprintf(stderr, "Error: could not initialize participant\n");
		return -1;
	}
	
	// 2.) Lookup offers to see if exchange item exists
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
		Exchange_Item * new_offer = init_exchange_item(fingerprint, data_bytes, OFFER_ITEM, new_participant);
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



	// 3.) Lookup if bid exists. If so, then queue post_sends to for all (or "available") bid participants informing them of location of objects, 
	//	   and remove participants from bid item (if no participants left, remove item). Free memory appropriately
	Exchange_Item * found_bid;
	ret = lookup_exch_item(exchange, fingerprint, BID_ITEM, &found_bid);
	if (found_bid){
		Deque * bid_participants = found_bid -> participants;
		int num_participants = bid_participants -> cnt;

		printf("Found %d participants with bids for fingerprint: ", num_participants);
		print_hex(fingerprint, FINGERPRINT_NUM_BYTES);
		printf("Would be posting sends to all of:\n"); 
		void * bid_participant;

		// FOR NOW REMOVING ALL BID PARTICIPANTS BUT MIGHT WANT TO CHANGE BASED ON TOPOLOGY!
		while (!is_deque_empty(bid_participants)){
			ret = dequeue(bid_participants, &bid_participant);
			if (ret != 0){
				fprintf(stderr, "Error: could not dequeue bid participant\n");
				return -1;
			}

			
			printf("\tLocation ID: %lu\n\tWork Request ID: %lu\n\tData Bytes: %lu\n\n", ((Bid_Participant *) bid_participant) -> location_id, ((Bid_Participant *) bid_participant) -> wr_id, found_bid -> data_bytes);
			
			// SHOULD BE POSTING SEND HERE WITH data = (Location ID + Addr + Rkey) of new offer (function args) and sending to (location_id, wr_id)

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


int post_future(Exchange * exchange, uint8_t * fingerprint, uint64_t data_bytes, uint64_t location_id) {
	
	int ret;

	// 1.) Build participant
	Future_Participant * new_participant = init_future_participant(location_id);
	if (new_participant == NULL){
		fprintf(stderr, "Error: could not initialize participant\n");
		return -1;
	}
	
	// 2.) Lookup offers to see if exchange item exists
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
		Exchange_Item * new_future = init_exchange_item(fingerprint, data_bytes, FUTURE_ITEM, new_participant);
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


// The corresponding function to "setup_exchange_connection" from exchange_client.c
int setup_client_connection(Exchange * exchange, uint64_t exchange_id, char * exchange_ip, uint64_t location_id, char * location_ip, char * server_port, uint16_t capacity_channels) {

	int ret;

	Client_Connection * client_connection = (Client_Connection *) malloc(sizeof(Client_Connection));
	if (client_connection == NULL){
		fprintf(stderr, "Error: malloc failed when allocating client connection\n");
		return -1;
	}

	client_connection -> location_id = location_id;

	Connection * connection;

	//RDMAConnectionType exchange_connection_type = RDMA_UD;
	// FOR NOW MAKING IT RDMA_RC FOR EASIER ESTABLISHMENT BUT WILL BE RDMA_UD
	RDMAConnectionType exchange_connection_type = RDMA_RC;

	int is_server;
	uint64_t server_id, client_id;
	char *server_ip, *client_ip;
	struct ibv_qp *server_qp, *client_qp;
	struct ibv_cq_ex *server_cq, *client_cq;
	if (location_id < exchange_id){
		is_server = 0;
		server_id = location_id;
		server_ip = location_ip;
		server_qp = NULL;
		server_cq = NULL;
		client_id = exchange_id;
		client_ip = exchange_ip;
		client_qp = exchange -> exchange_qp;
		client_cq = exchange -> exchange_cq;
	}
	else{
		is_server = 1;
		server_id = exchange_id;
		server_ip = exchange_ip;
		server_qp = exchange -> exchange_qp;
		server_cq = exchange -> exchange_cq;
		client_id = location_id;
		client_ip = location_ip;
		client_qp = NULL;
		client_cq = NULL;
	}

	// if exchange_qp is null, then it will be created, otherwise connection will use that qp
	ret = setup_connection(exchange_connection_type, is_server, server_id, server_ip, server_port, server_qp, server_cq,
							client_id, client_ip, client_qp, client_cq, &connection);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup exchange connection\n");
		return -1;
	}

	// set exchange_qp if null
	if (exchange -> exchange_qp == NULL){
		exchange -> exchange_qp = connection -> cm_id -> qp;
	}
	if (exchange -> exchange_cq = NULL){
		exchange -> exchange_cq = connection -> cq;
	}


	client_connection -> connection = connection;

	
	// now we need to allocate and register ring buffers to receive incoming orders
	client_connection -> capacity_channels = capacity_channels;

	client_connection -> in_bid_orders = init_channel(exchange_id, location_id, capacity_channels, BID_ORDER, sizeof(Bid_Order), true, true, connection -> pd, exchange -> exchange_qp, exchange -> exchange_cq);
	client_connection -> in_offer_orders = init_channel(exchange_id, location_id, capacity_channels, OFFER_ORDER, sizeof(Offer_Order), true, true, connection -> pd, exchange -> exchange_qp, exchange -> exchange_cq);
	client_connection -> in_future_orders = init_channel(exchange_id, location_id, capacity_channels, FUTURE_ORDER, sizeof(Future_Order), true, true, connection -> pd, exchange -> exchange_qp, exchange -> exchange_cq);
	// setting is_recv to false, because we will be posting sends from this channel
	client_connection -> out_bid_matches = init_channel(exchange_id, location_id, capacity_channels, BID_MATCH, sizeof(Bid_Match), false, false, connection -> pd, exchange -> exchange_qp, exchange -> exchange_cq);

	if ((client_connection -> in_bid_orders == NULL) || (client_connection -> in_offer_orders == NULL) || 
			(client_connection -> in_future_orders == NULL) || (client_connection -> out_bid_matches == NULL)){
		fprintf(stderr, "Error: was unable to initialize channels\n");
		return -1;
	}

	// now add the connection to table so we can lookup the connection by exchange_id (aka destination metadata-shard) when we need to query object locations
	ret = insert_item_table(exchange -> clients, client_connection);
	if (ret != 0){
		fprintf(stderr, "Error: could not add client connection to clients table\n");
		return -1;
	}


	return 0;

}