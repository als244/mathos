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

// called internally from post_bid and post_offer
int handle_bid_match_notify(Exchange * exchange, uint64_t offer_location_id, uint64_t bid_location_id, uint64_t bid_match_wr_id) {

	int ret;

    // used for looking up client connections needed to get to channel buffer to send to bid participant
    Table * client_conn_table = exchange -> clients;
    Client_Connection target_client_conn;
    target_client_conn.location_id = bid_location_id;

    Client_Connection * client_connection = find_item_table(client_conn_table, &target_client_conn);

    if (client_connection == NULL){
    	fprintf(stderr, "Error: could not find client connection for id: %lu\n", bid_location_id);
    	return -1;
    }

    // message to send to bid participant
    Bid_Match bid_match;
    bid_match.location_id = offer_location_id;

    Channel * out_bid_matches = client_connection -> out_bid_matches;

    printf("[Exchange %lu]. Sending BID_MATCH notification to: %lu...\n", exchange -> id, bid_location_id);

    // specifying the wr_id to use and don't need the addr of bid_match within registered channel buffer
    uint64_t wr_id_to_send = bid_match_wr_id;
	ret = submit_out_channel_message(out_bid_matches, &bid_match, &wr_id_to_send, NULL, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not submit out channel message with bid order\n");
		return -1;
	}

	return 0;
}


// called by the completition handler
int handle_order(Exchange * exchange, Client_Connection * client_connection, uint64_t order_wr_id, ExchangeItemType order_type){

	int ret;

	Channel * in_channel;
	uint64_t channel_item_id = order_wr_id;
	void * client_order;
	switch(order_type){
		case BID_ITEM:
			in_channel = client_connection -> in_bid_orders;
			Bid_Order bid_order;
			client_order = &bid_order;
			break;
		case OFFER_ITEM:
			in_channel = client_connection -> in_offer_orders;
			Offer_Order offer_order;
			client_order = &offer_order;
			break;
		case FUTURE_ITEM:
			in_channel = client_connection -> in_future_orders;
			Future_Order future_order;
			client_order = &future_order;
			break;
	}
		
	// ensure to post a new receive (reservation within in-channel) now that we have consumed one
	// Populate client item
	ret = extract_channel_item(in_channel, channel_item_id, true, client_order);
	if (ret != 0){
		fprintf(stderr, "Error: could not extract order from channel when handling\n");
		return -1;
	}



	// actually send item to exchange
	uint8_t * fingerprint;

	print_hex(fingerprint, FINGERPRINT_NUM_BYTES);
	switch(order_type) {
		case BID_ITEM:
			fingerprint = ((Bid_Order *) client_order) -> fingerprint;
			printf("[Exchange %lu] Posting BID from client: %lu...\n", exchange -> id, ((Bid_Order *) client_order) -> location_id);
			ret = post_bid(exchange, fingerprint, ((Bid_Order *) client_order) -> data_bytes, 
							((Bid_Order *) client_order) -> location_id, ((Bid_Order *) client_order) -> wr_id);
			break;
		case OFFER_ITEM:
			fingerprint = ((Offer_Order *) client_order) -> fingerprint;
			printf("[Exchange %lu] Posting OFFER from client: %lu...\n", exchange -> id, ((Offer_Order *) client_order) -> location_id);
			ret = post_offer(exchange, fingerprint, ((Offer_Order *) client_order) -> data_bytes, 
							((Offer_Order *) client_order) -> location_id);
			break;
		case FUTURE_ITEM:
			fingerprint = ((Future_Order *) client_order) -> fingerprint;
			printf("[Exchange %lu] Posting Future from client: %lu...\n", exchange -> id, ((Future_Order *) client_order) -> location_id);
			ret = post_offer(exchange, fingerprint, ((Future_Order *) client_order) -> data_bytes, 
							((Future_Order *) client_order) -> location_id);
			break;
		default:
			fprintf(stderr, "Error: order type not supported\n");
			return -1;
	}

	if (ret != 0){
		fprintf(stderr, "Error: unsuccessful in posting order\n");
		return -1;
	}

	printf("Successfully posted order for fingerprint: ");
	print_hex(fingerprint, FINGERPRINT_NUM_BYTES);

	return 0;
}

// For now only care about receive completitions (aka sender != self) ...
void * exchange_completition_handler(void * _thread_data){

	int ret;

	Exchange_Completition * completition_handler_data = (Exchange_Completition *) _thread_data;

	uint64_t completition_thread_id = completition_handler_data -> completition_thread_id;
	Exchange * exchange = completition_handler_data -> exchange;


	// really should look up based on completition_thread_id
	struct ibv_cq_ex * cq = exchange -> exchange_cq;

    struct ibv_poll_cq_attr poll_qp_attr = {};
    ret = ibv_start_poll(cq, &poll_qp_attr);

    // If Error after start, do not call "end_poll"
    if ((ret != 0) && (ret != ENOENT)){
        fprintf(stderr, "Error: could not start poll for completition queue\n");
        return NULL;
    }

    // if ret = 0, then ibv_start_poll already consumed an item
    int seen_new_completition;
    int is_done = 0;
    
    enum ibv_wc_status status;
    uint64_t wr_id;

    MessageType message_type;
    uint64_t sender_id;

    uint64_t self_id = exchange -> id;

    
    Client_Connection * client_connection;

    // used for looking up exchange connections needed to get to channel buffer
    Table * client_conn_table = exchange -> clients;
    Client_Connection target_client_conn;

    int handle_ret;

    // For now, infinite loop
    while (!is_done){
        // return is 0 if a new item was cosumed, otherwise it equals ENOENT
        if (ret == 0){
            seen_new_completition = 1;
        }
        else{
            seen_new_completition = 0;
        }
        
        // Consume the completed work request
        wr_id = cq -> wr_id;
        status = cq -> status;
        // other fields as well...
        if (seen_new_completition){

        	message_type = decode_wr_id(wr_id, &sender_id);

        	/* DO SOMETHING WITH wr_id! */
            printf("Saw completion of wr_id = %ld (Sender_ID = %lu, MessageType = %s)\n\tStatus: %d\n\n", wr_id, sender_id, message_type_to_str(message_type), status);

            if (status != IBV_WC_SUCCESS){
                fprintf(stderr, "Error: work request id %ld had error\n", wr_id);
                // DO ERROR HANDLING HERE!
            }

        	
        	// for now can ignore the send completitions
        	// eventually need to have an ack in place and also
        	// need to remove the send data from channel's buffer table
        	if (sender_id != self_id){

        		// lookup the connection based on sender id

        		target_client_conn.location_id = sender_id;

        		client_connection = find_item_table(client_conn_table, &target_client_conn);
        		if (client_connection != NULL){
	        		// MAY WANT TO HAVE SEPERATE THREADS FOR PROCESSING THE WORK DO BE DONE...
		        	switch(message_type){
		        		case BID_ORDER:
		        			handle_ret = handle_order(exchange, client_connection, wr_id, BID_ITEM);
		        			break;
		        		case OFFER_ORDER:
		        			handle_ret = handle_order(exchange, client_connection, wr_id, OFFER_ITEM);
		        			break;
		        		case FUTURE_ORDER:
		        			handle_ret = handle_order(exchange, client_connection, wr_id, FUTURE_ITEM);
		        			break;
		        		default:
		        			fprintf(stderr, "Error: unsupported exchanges client handler message type of: %d\n", message_type);
		        			break;
		        	}
		        	if (handle_ret != 0){
		        		fprintf(stderr, "Error: exchanges client handler had an error\n");
		        	}
		        }
	        	else{
	        		fprintf(stderr, "Error: within completition handler, could not find exchange connection with id: %lu\n", sender_id);
	        	}
	        }
        }

        // Check for next completed work request...
        ret = ibv_next_poll(cq);

        if ((ret != 0) && (ret != ENOENT)){
            // If Error after next, call "end_poll"
            ibv_end_poll(cq);
            fprintf(stderr, "Error: could not do next poll for completition queue\n");
            return NULL;
        }
    }

    // should never get here...
    ibv_end_poll(cq);

    return NULL;
}


Exchange * init_exchange(uint64_t id, uint64_t start_val, uint64_t end_val, uint64_t max_bids, uint64_t max_offers, uint64_t max_futures, uint64_t max_clients, struct ibv_context * ibv_ctx) {

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

	// Setting up based on context of exchange
	// Need to intialize PD, CQ, and QP here

	// 1.) PD based on inputted configuration
	struct ibv_pd * pd = ibv_alloc_pd(ibv_ctx);
	if (pd == NULL) {
		fprintf(stderr, "Error: could not allocate pd for exchanges_client\n");
		return NULL;
	}

	// 2.) CQ based on inputted configuration
	int num_cq_entries = 1U << 15;

	/* "The pointer cq_context will be used to set user context pointer of the cq structure" */
	
	// SHOULD BE THE EXCHANGE_CLIENT COMPLETITION HANDLER 
	void * cq_context = NULL;

	struct ibv_cq_init_attr_ex cq_attr;
	memset(&cq_attr, 0, sizeof(cq_attr));
	cq_attr.cqe = num_cq_entries;
	cq_attr.cq_context = cq_context;
	
	struct ibv_cq_ex * cq = ibv_create_cq_ex(ibv_ctx, &cq_attr);
	if (cq == NULL){
		fprintf(stderr, "Error: could not create cq for exchanges_client\n");
		return NULL;
	}

	// 3.) NOW create QP
	// SHOULD BE OF UD type but for now just saying RC for simplicity

	// really should be RDMA_UD
	RDMAConnectionType connection_type = RDMA_RC;
	enum ibv_qp_type qp_type;
	if (connection_type == RDMA_RC){
		qp_type = IBV_QPT_RC;
	}
	if (connection_type == RDMA_UD){
		qp_type = IBV_QPT_UD;
	}
	
	struct ibv_qp_init_attr_ex qp_attr;
	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.pd = pd; // Setting Protection Domain
	qp_attr.qp_type = qp_type; // Using Reliable-Connection
	qp_attr.sq_sig_all = 1;       // if not set 0, all work requests submitted to SQ will always generate a Work Completion.
	qp_attr.send_cq = ibv_cq_ex_to_cq(cq);         // completion queue can be shared or you can use distinct completion queues.
	qp_attr.recv_cq = ibv_cq_ex_to_cq(cq);         // completion queue can be shared or you can use distinct completion queues.

	// Device cap of 2^15 for each side of QP's outstanding work requests...
	qp_attr.cap.max_send_wr = 1U << 15;  // increase if you want to keep more send work requests in the SQ.
	qp_attr.cap.max_recv_wr = 1U << 15;  // increase if you want to keep more receive work requests in the RQ.
	qp_attr.cap.max_send_sge = 1; // increase if you allow send work requests to have multiple scatter gather entry (SGE).
	qp_attr.cap.max_recv_sge = 1; // increase if you allow receive work requests to have multiple scatter gather entry (SGE).
	//qp_attr.cap.max_inline_data = 1000;
	uint64_t send_ops_flags;
	if (connection_type == RDMA_RC){
		send_ops_flags = IBV_QP_EX_WITH_RDMA_WRITE | IBV_QP_EX_WITH_RDMA_READ | IBV_QP_EX_WITH_SEND |
								IBV_QP_EX_WITH_ATOMIC_CMP_AND_SWP | IBV_QP_EX_WITH_ATOMIC_FETCH_AND_ADD;
	}
	// UD queue pairs can only do Sends, not RDMA or Atomics
	else{
		send_ops_flags = IBV_QP_EX_WITH_SEND;
	}
	qp_attr.send_ops_flags |= send_ops_flags;
	qp_attr.comp_mask |= IBV_QP_INIT_ATTR_SEND_OPS_FLAGS | IBV_QP_INIT_ATTR_PD;

	struct ibv_qp * qp = ibv_create_qp_ex(ibv_ctx, &qp_attr);
	if (qp == NULL){
		fprintf(stderr, "Error: could not create qp for exchanges_client\n");
		return NULL;
	}

	exchange -> exchange_pd = pd;
	exchange -> exchange_cq = cq;
	exchange -> exchange_qp = qp;

	// INITIALIZE COMPLETITION QUEUE HANDLER THREADS

	// num threads should equal number of CQs
	int num_threads = 1;
	Exchange_Completition * handler_thread_data = malloc(num_threads * sizeof(Exchange_Completition));
	if (handler_thread_data == NULL){
		fprintf(stderr, "Error: malloc failed allocating handler thread data\n");
		return NULL;
	}

	pthread_t * completion_threads = (pthread_t *) malloc(num_threads * sizeof(pthread_t));
	if (completion_threads == NULL){
		fprintf(stderr, "Error: malloc failed allocating pthreads for completition handlers\n");
		return NULL;
	}

	exchange -> completion_threads = completion_threads;

	for (int i = 0; i < num_threads; i++){
		handler_thread_data[i].completition_thread_id = i;
		handler_thread_data[i].exchange = exchange;
		// start the completion thread
		pthread_create(&completion_threads[i], NULL, exchange_completition_handler, (void *) &handler_thread_data[i]);
	}

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
		uint64_t offer_location_id;
		// FOR NOW PRINTING ALL PARTICIPANTS AND POSTING SEND WR_ID with details of head of queue 
		// BUT MIGHT WANT TO CHANGE BASED ON TOPOLOGY!
		while (cur_item != NULL){
			offer_participant = (Offer_Participant *) cur_item -> item;
			offer_location_id = ((Offer_Participant *) offer_participant) -> location_id;
			ret = handle_bid_match_notify(exchange, offer_location_id, location_id, wr_id);
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
		uint64_t bid_location_id;
		uint64_t bid_match_wr_id;
		// FOR NOW REMOVING ALL BID PARTICIPANTS BUT MIGHT WANT TO CHANGE BASED ON TOPOLOGY!
		while (!is_deque_empty(bid_participants)){
			ret = dequeue(bid_participants, &bid_participant);
			if (ret != 0){
				fprintf(stderr, "Error: could not dequeue bid participant\n");
				return -1;
			}
			bid_location_id = ((Bid_Participant *) bid_participant) -> location_id;
			bid_match_wr_id =  ((Bid_Participant *) bid_participant) -> wr_id;
			ret = handle_bid_match_notify(exchange, location_id, bid_location_id, bid_match_wr_id);
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
	struct ibv_pd *server_pd, *client_pd;
	if (location_id < exchange_id){
		is_server = 0;
		server_id = location_id;
		server_ip = location_ip;
		server_pd = NULL;
		server_qp = NULL;
		server_cq = NULL;
		client_id = exchange_id;
		client_ip = exchange_ip;
		client_pd = exchange -> exchange_pd;
		client_qp = exchange -> exchange_qp;
		client_cq = exchange -> exchange_cq;
	}
	else{
		is_server = 1;
		server_id = exchange_id;
		server_ip = exchange_ip;
		server_pd = exchange -> exchange_pd;
		server_qp = exchange -> exchange_qp;
		server_cq = exchange -> exchange_cq;
		client_id = location_id;
		client_ip = location_ip;
		client_qp = NULL;
		client_cq = NULL;
	}

	// if exchange_qp is null, then it will be created, otherwise connection will use that qp
	ret = setup_connection(exchange_connection_type, is_server, server_id, server_ip, server_port, server_pd, server_qp, server_cq,
							client_id, client_ip, client_pd, client_qp, client_cq, &connection);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup exchange connection\n");
		return -1;
	}

	// set exchange_qp if null
	if (exchange -> exchange_qp == NULL){
		exchange -> exchange_qp = connection -> cm_id -> qp;
	}
	if (exchange -> exchange_cq == NULL){
		exchange -> exchange_cq = connection -> cq;
	}
	if (exchange -> exchange_pd == NULL){
		exchange -> exchange_pd = connection -> pd;
	}


	client_connection -> connection = connection;

	
	// now we need to allocate and register ring buffers to receive incoming orders
	client_connection -> capacity_channels = capacity_channels;

	client_connection -> in_bid_orders = init_channel(exchange_id, location_id, capacity_channels, BID_ORDER, sizeof(Bid_Order), true, true, exchange -> exchange_pd, exchange -> exchange_qp, exchange -> exchange_cq);
	client_connection -> in_offer_orders = init_channel(exchange_id, location_id, capacity_channels, OFFER_ORDER, sizeof(Offer_Order), true, true, exchange -> exchange_pd, exchange -> exchange_qp, exchange -> exchange_cq);
	client_connection -> in_future_orders = init_channel(exchange_id, location_id, capacity_channels, FUTURE_ORDER, sizeof(Future_Order), true, true, exchange -> exchange_pd, exchange -> exchange_qp, exchange -> exchange_cq);
	// setting is_recv to false, because we will be posting sends from this channel
	client_connection -> out_bid_matches = init_channel(exchange_id, location_id, capacity_channels, BID_MATCH, sizeof(Bid_Match), false, false, exchange -> exchange_pd, exchange -> exchange_qp, exchange -> exchange_cq);

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