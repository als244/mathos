#include "exchange_client.h"

int exch_connection_item_cmp(void * connection_item, void * other_item) {
	uint32_t id_a = ((Exchange_Connection *) connection_item) -> exchange_id;
	uint32_t id_b = ((Exchange_Connection *) other_item) -> exchange_id;
	return id_a - id_b;
}

/*
uint64_t exch_connection_hash_func(void * connection_item, uint64_t table_size) {
	uint64_t key = ((Exchange_Connection *) connection_item) -> exchange_id;
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
*/

// Take from "https://gist.github.com/badboy/6267743"
// Credit: Robert Jenkins
uint64_t exch_connection_hash_func(void * connection_item, uint64_t table_size) {
	uint32_t key = ((Exchange_Connection *) connection_item) -> exchange_id;
	key = (key+0x7ed55d16) + (key<<12);
   	key = (key^0xc761c23c) ^ (key>>19);
   	key = (key+0x165667b1) + (key<<5);
   	key = (key+0xd3a2646c) ^ (key<<9);
   	key = (key+0xfd7046c5) + (key<<3);
   	key = (key^0xb55a4f09) ^ (key>>16);
   	return (uint64_t) key % table_size;
}



int bid_item_cmp(void * bid_item, void * other_item) {
	uint64_t wr_id_a = ((Outstanding_Bid *) bid_item) -> bid_match_wr_id;
	uint64_t wr_id_b = ((Outstanding_Bid *) other_item) -> bid_match_wr_id;
	return wr_id_a - wr_id_b;
}

uint64_t bid_hash_func(void * bid_item, uint64_t table_size) {
	uint64_t key = ((Outstanding_Bid *) bid_item) -> bid_match_wr_id;
	// Taken from "https://github.com/shenwei356/uint64-hash-bench?tab=readme-ov-file"
	// Credit: Thomas Wang
	key = (key << 21) - key - 1;
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);
	return key % table_size;
}


// ASSUME EQUALLY PARTITIONED ID SPACE ACROSS 64-bits (which is lower 64-bits of SHA256 fingerprint hash by deafult)
uint64_t get_start_val_from_exch_id(uint32_t num_exchanges, uint32_t exchange_id){
	uint64_t partition_size = UINT64_MAX / num_exchanges;
	return exchange_id * partition_size;
}

uint64_t get_end_val_from_exch_id(uint32_t num_exchanges, uint32_t exchange_id){
	uint64_t partition_size = UINT64_MAX / num_exchanges;
	return ((exchange_id + 1) * partition_size) - 1;
}



Exchange_Connection * init_self_connection(Exchange * exchange, Exchanges_Client * exchanges_client, int self_recv_capacity) {

	Exchange_Connection * self_exchange_connection = (Exchange_Connection *) calloc(1, sizeof(Exchange_Connection));
	if (self_exchange_connection == NULL){
		fprintf(stderr, "Error: malloc failed in allocating self exchange connection\n");
		return NULL;
	}

	uint64_t exchange_id = exchange -> id;
	self_exchange_connection -> exchange_id = exchange_id;
	self_exchange_connection -> start_val = exchange -> start_val;
	self_exchange_connection -> end_val = exchange -> end_val;
	

	// now we need to allocate and register ring buffers to receive incoming orders
	self_exchange_connection -> capacity_channels = self_recv_capacity;

	// DO NOT NEED TO INITIALIZE / SETUP a "Connection *" because communicating with this process directly

	// Only need to setup in_channels (to be able to post receives)
	Channel * in_bid_matches = init_channel(exchange_id, exchange_id, self_recv_capacity, BID_MATCH, sizeof(Bid_Match), true, true, false, exchanges_client -> exchange_client_pd, exchanges_client -> exchange_client_qp, exchanges_client -> exchange_client_cq);
	if (in_bid_matches == NULL){
		fprintf(stderr, "Error: could not initialize self-receive channel\n");
		return NULL;
	}

	self_exchange_connection -> in_bid_matches = in_bid_matches;

	return self_exchange_connection;

	// The out_channel functions will take care of functions directly 
	// (sends are equivalently to already having the information locally)
	

}

int handle_bid_match_recv(Exchanges_Client * exchanges_client, Exchange_Connection * exchange_connection, uint64_t bid_match_wr_id){

	int ret;

	Channel * in_bid_matches = exchange_connection -> in_bid_matches;

	// remove outsanding bid from table to get fingerprint
	Table * outstanding_bids_table = exchanges_client -> outstanding_bids;

	Outstanding_Bid to_remove;
	to_remove.bid_match_wr_id = bid_match_wr_id;

	Outstanding_Bid * outstanding_bid = remove_item_table(outstanding_bids_table, &to_remove);

	uint8_t * fingerprint = outstanding_bid -> fingerprint;

	// lookup data corresponding to bid match
	Bid_Match bid_match;

	ret = extract_channel_item(in_bid_matches, bid_match_wr_id, false, &bid_match);
	if (ret != 0){
		fprintf(stderr, "Error: could not extract bid match\n");
		return -1;
	}

	printf("[Client %u]. Recived a MATCH\n\tLocation: %u\n\tFor fingerprint: ", exchanges_client -> self_exchange_id, bid_match.location_id);
	print_hex(fingerprint, FINGERPRINT_NUM_BYTES);

	// now can free this because was dynamically allocated when inserted
	free(outstanding_bid);

	return 0;
}


// For now only care about receive completitions (aka sender != self) ...
void * exchange_client_completition_handler(void * _thread_data){

	int ret;

	Exchanges_Client_Completition * completition_handler_data = (Exchanges_Client_Completition *) _thread_data;

	uint64_t completition_thread_id = completition_handler_data -> completition_thread_id;
	Exchanges_Client * exchanges_client = completition_handler_data -> exchanges_client;


	// really should look up based on completition_thread_id
	struct ibv_cq_ex * cq = exchanges_client -> exchange_client_cq;

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
    uint32_t sender_id;

    uint32_t self_id = exchanges_client -> self_exchange_id;

    
    Exchange_Connection * exchange_connection;

    // used for looking up exchange connections needed to get to channel buffer
    Table * exchange_conn_table = exchanges_client -> exchanges;
    Exchange_Connection target_exch_conn;

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
            printf("[Client %u]. Saw completion of wr_id = %ld (Sender_ID = %u, MessageType = %s)\n\tStatus: %d\n\n", self_id, wr_id, sender_id, message_type_to_str(message_type), status);

            if (status != IBV_WC_SUCCESS){
                fprintf(stderr, "Error: work request id %ld had error\n", wr_id);
                // DO ERROR HANDLING HERE!
            }

        	
        	// for now can ignore the send completitions
        	// eventually need to have an ack in place and also
        	// need to remove the send data from channel's buffer table
        	if (sender_id != self_id){

        		// lookup the connection based on sender id

        		target_exch_conn.exchange_id = sender_id;

        		exchange_connection = find_item_table(exchange_conn_table, &target_exch_conn);
        		if (exchange_connection != NULL){
	        		// MAY WANT TO HAVE SEPERATE THREADS FOR PROCESSING THE WORK DO BE DONE...
		        	switch(message_type){
		        		case BID_MATCH:
		        			handle_ret = handle_bid_match_recv(exchanges_client, exchange_connection, wr_id);
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
	        		fprintf(stderr, "Error: within completition handler, could not find exchange connection with id: %u\n", sender_id);
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




Exchanges_Client * init_exchanges_client(uint32_t num_exchanges, uint32_t max_exchanges, uint64_t max_outstanding_bids, Exchange * self_exchange, struct ibv_context * ibv_ctx) {

	Exchanges_Client * exchanges_client = (Exchanges_Client *) malloc(sizeof(Exchanges_Client));
	if (exchanges_client == NULL){
		fprintf(stderr, "Error: malloc failed in allocating exchanges client\n");
		return NULL;
	}

	// eventually will grow such that num_exchanges == max_exchanges
	exchanges_client -> num_exchanges = num_exchanges;
	exchanges_client -> max_exchanges = max_exchanges;
	exchanges_client -> max_outstanding_bids = max_outstanding_bids;


	float load_factor = .5f;
	float shrink_factor = .1f;
	Hash_Func hash_func_connection = &exch_connection_hash_func;
	Item_Cmp item_cmp_connection = &exch_connection_item_cmp;


	// for now setting min_size == max_size because fixed number of exchanges, 
	// but leaving room for node entry/exit protocol...
	uint64_t min_exchanges = max_exchanges;
	Table * exchanges = init_table(min_exchanges, max_exchanges, load_factor, shrink_factor, hash_func_connection, item_cmp_connection);
	if (exchanges == NULL){
		fprintf(stderr, "Error: could not initialize exchanges table\n");
		return NULL;
	}

	exchanges_client -> exchanges = exchanges;


	uint64_t min_outstanding_bids_size = 1 << 10;
	if (min_outstanding_bids_size < max_outstanding_bids){
		min_outstanding_bids_size = max_outstanding_bids;
	}

	Hash_Func hash_func_bid = &bid_hash_func;
	Item_Cmp item_cmp_bid = &bid_item_cmp;

	Table * outstanding_bids = init_table(min_outstanding_bids_size, max_outstanding_bids, load_factor, shrink_factor, hash_func_bid, item_cmp_bid);
	if (outstanding_bids == NULL){
		fprintf(stderr, "Error: could not initialize outstanding_bids table\n");
		return NULL;
	}

	exchanges_client -> outstanding_bids = outstanding_bids;


	pthread_mutex_init(&(exchanges_client -> exchanges_client_lock), NULL);

	exchanges_client -> exchange_client_qp = NULL;

	exchanges_client -> self_exchange_id = self_exchange -> id;
	exchanges_client -> self_exchange = self_exchange;

	// Set the IBV context passed in by configuration
	exchanges_client -> ibv_ctx = ibv_ctx;

	// Need to intialize PD, CQ, and QP here

	// 1.) PD based on inputted configuration
	struct ibv_pd * pd = ibv_alloc_pd(ibv_ctx);
	if (pd == NULL) {
		fprintf(stderr, "Error: could not allocate pd for exchanges_client\n");
		return NULL;
	}

	// 2.) CQ based on inputted configuration
	int num_cq_entries = 1U << 12;

	/* "The pointer cq_context will be used to set user context pointer of the cq structure" */
	
	// SHOULD BE THE EXCHANGE_CLIENT COMPLETITION HANDLER 
	void * cq_context = NULL;

	struct ibv_cq_init_attr_ex cq_attr;
	memset(&cq_attr, 0, sizeof(cq_attr));
	cq_attr.cqe = num_cq_entries;
	cq_attr.cq_context = cq_context;

	// every cq will be in its own thread...
	uint32_t cq_create_flags = IBV_CREATE_CQ_ATTR_SINGLE_THREADED;
	cq_attr.flags = cq_create_flags;
	
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
	qp_attr.cap.max_send_wr = 1U << 12;  // increase if you want to keep more send work requests in the SQ.
	qp_attr.cap.max_recv_wr = 1U << 12;  // increase if you want to keep more receive work requests in the RQ.
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

	exchanges_client -> exchange_client_pd = pd;
	exchanges_client -> exchange_client_cq = cq;
	exchanges_client -> exchange_client_qp = qp;

	Exchange_Connection * self_exchange_connection = init_self_connection(self_exchange, exchanges_client, max_outstanding_bids);
	if (self_exchange_connection == NULL){
		fprintf(stderr, "Error: could not initialize self exchange connection\n");
		return NULL;
	}

	exchanges_client -> self_exchange_connection = self_exchange_connection;

	// INITIALIZE COMPLETITION QUEUE HANDLER THREADS

	// num threads should equal number of CQs
	int num_threads = 1;
	Exchanges_Client_Completition * handler_thread_data = malloc(num_threads * sizeof(Exchanges_Client_Completition));
	if (handler_thread_data == NULL){
		fprintf(stderr, "Error: malloc failed allocating handler thread data\n");
		return NULL;
	}

	pthread_t * completion_threads = (pthread_t *) malloc(num_threads * sizeof(pthread_t));
	if (completion_threads == NULL){
		fprintf(stderr, "Error: malloc failed allocating pthreads for completition handlers\n");
		return NULL;
	}

	exchanges_client -> completion_threads = completion_threads;

	for (int i = 0; i < num_threads; i++){
		handler_thread_data[i].completition_thread_id = i;
		handler_thread_data[i].exchanges_client = exchanges_client;
		// start the completion thread
		pthread_create(&completion_threads[i], NULL, exchange_client_completition_handler, (void *) &handler_thread_data[i]);
	}

	return exchanges_client;
}



// the smaller of exchange_id and local_id will serve as the "server" when establishing RDMA connection
int setup_exchange_connection(Exchanges_Client * exchanges_client, uint32_t exchange_id, char * exchange_ip, uint32_t location_id, char * location_ip, char * server_port, uint32_t capacity_channels) {

	int ret;

	Exchange_Connection * exchange_connection = (Exchange_Connection *) malloc(sizeof(Exchange_Connection));
	if (exchange_connection == NULL){
		fprintf(stderr, "Error: malloc failed when allocating exchange connection\n");
		return -1;
	}

	exchange_connection -> exchange_id = exchange_id;
	uint64_t exch_start_val = get_start_val_from_exch_id(exchanges_client -> num_exchanges, exchange_id);
	uint64_t exch_end_val = get_end_val_from_exch_id(exchanges_client -> num_exchanges, exchange_id);
	exchange_connection -> start_val = exch_start_val;
	// uint64_t, so loop around gives max
	exchange_connection -> end_val = exch_end_val;


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
		is_server = 1;
		server_id = location_id;
		server_ip = location_ip;
		server_pd = exchanges_client -> exchange_client_pd;
		server_qp = exchanges_client -> exchange_client_qp;
		server_cq = exchanges_client -> exchange_client_cq;
		client_id = exchange_id;
		client_ip = exchange_ip;
		client_pd = NULL;
		client_qp = NULL;
		client_cq = NULL;
	}
	else{
		is_server = 0;
		server_id = exchange_id;
		server_ip = exchange_ip;
		server_pd = NULL;
		server_qp = NULL;
		server_cq = NULL;
		client_id = location_id;
		client_ip = location_ip;
		client_pd = exchanges_client -> exchange_client_pd;
		client_qp = exchanges_client -> exchange_client_qp;
		client_cq = exchanges_client -> exchange_client_cq;
	}

	// if exchange_client_qp is null, then it will be created, otherwise connection will use that qp
	ret = setup_connection(exchange_connection_type, is_server, server_id, server_ip, server_port, server_pd, server_qp, server_cq,
							client_id, client_ip, client_pd, client_qp, client_cq, &connection);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup exchange connection\n");
		return -1;
	}

	// set exchange_client_qp if null
	if (exchanges_client -> exchange_client_qp == NULL){
		exchanges_client -> exchange_client_qp = connection -> cm_id -> qp;
	}
	if (exchanges_client -> exchange_client_cq == NULL){
		exchanges_client -> exchange_client_cq = connection -> cq;
	}
	if (exchanges_client -> exchange_client_pd == NULL){
		exchanges_client -> exchange_client_pd = connection -> pd;
	}


	exchange_connection -> connection = connection;

	// now we need to allocate and register ring buffers to receive incoming orders
	exchange_connection -> capacity_channels = capacity_channels;

	exchange_connection -> out_bid_orders = init_channel(location_id, exchange_id, capacity_channels, BID_ORDER, sizeof(Bid_Order), true, false, false, exchanges_client -> exchange_client_pd, exchanges_client -> exchange_client_qp, exchanges_client -> exchange_client_cq);
	exchange_connection -> out_offer_orders = init_channel(location_id, exchange_id, capacity_channels, OFFER_ORDER, sizeof(Offer_Order), true, false, false, exchanges_client -> exchange_client_pd, exchanges_client -> exchange_client_qp, exchanges_client -> exchange_client_cq);
	exchange_connection -> out_future_orders = init_channel(location_id, exchange_id, capacity_channels, FUTURE_ORDER, sizeof(Future_Order), true, false, false, exchanges_client -> exchange_client_pd, exchanges_client -> exchange_client_qp , exchanges_client -> exchange_client_cq);
	// setting is_recv to true, because we will be posting sends from this channel
	exchange_connection -> in_bid_matches = init_channel(location_id, exchange_id, capacity_channels, BID_MATCH, sizeof(Bid_Match), true, true, false, exchanges_client -> exchange_client_pd, exchanges_client -> exchange_client_qp, exchanges_client -> exchange_client_cq);

	if ((exchange_connection -> out_bid_orders == NULL) || (exchange_connection -> out_offer_orders == NULL) || 
			(exchange_connection -> out_future_orders == NULL) || (exchange_connection -> in_bid_matches == NULL)){
		fprintf(stderr, "Error: was unable to initialize channels\n");
		return -1;
	}

	// now add the connection to table so we can lookup the connection by exchange_id (aka destination metadata-shard) when we need to query object locations
	ret = insert_item_table(exchanges_client -> exchanges, exchange_connection);
	if (ret != 0){
		fprintf(stderr, "Error: could not add exchange connection to exchanges table\n");
		return -1;
	}

	return 0;

}


int submit_bid(Exchanges_Client * exchanges_client, uint32_t location_id, uint8_t * fingerprint, uint64_t data_bytes, uint64_t * ret_bid_match_wr_id, uint32_t * dest_exchange_id) {

	int ret;

	// 1.) Determine what exchange connection to use
	//		- for now equally partitioning across number of exchanges the lower 64 bits of fingerprint
	
	uint32_t self_exchange_id = exchanges_client -> self_exchange_id;

	// -a.) Optionally specifiy the target exchange or use default search method
	uint32_t target_exchange_id;
	if (dest_exchange_id != NULL){
		target_exchange_id = *dest_exchange_id;
	}
	else{
		uint32_t num_exchanges = exchanges_client -> num_exchanges;
		uint64_t least_sig64 = fingerprint_to_least_sig64(fingerprint, FINGERPRINT_NUM_BYTES);
		target_exchange_id = least_sig64 % num_exchanges;
	}

	// b.) Either use self-connection or lookup in table
	Exchange_Connection * target_exchange_connection;
	if (target_exchange_id == self_exchange_id){
		target_exchange_connection = exchanges_client -> self_exchange_connection;
	}
	else {
		Exchange_Connection target_exch;
		target_exch.exchange_id = target_exchange_id;
		Exchange_Connection * dest_exchange_connection = find_item_table(exchanges_client -> exchanges, &target_exch);
		if (dest_exchange_connection == NULL){
			fprintf(stderr, "Error: could not find destination exchange connection with id: %u\n", target_exchange_id);
		}
		target_exchange_connection =  dest_exchange_connection;
	}
	

	// 2.) Ensure that we would be able to receive a response, by submitting item to in channel
	//	   Posts receive item 
	Channel * in_bid_matches = target_exchange_connection -> in_bid_matches;
	uint64_t bid_match_wr_id;
	uint64_t bid_match_addr;
	ret = submit_in_channel_reservation(in_bid_matches, &bid_match_wr_id, &bid_match_addr);
	if (ret != 0){
		fprintf(stderr, "Error: could not submit inbound channel reservation for bid match\n");
		return -1;
	}

	// 3.) Now we can populate our bid_order telling the exchange the wr_id to send when it finds match
	// 		- If self, then just call the post_bid function directly
	if (target_exchange_id == self_exchange_id){
		Exchange * exchange = exchanges_client -> self_exchange;
		printf("[Client %u]. Posting self-BID...\n", self_exchange_id);
		ret = post_bid(exchange, fingerprint, data_bytes, location_id, bid_match_wr_id);
		if (ret != 0){
			fprintf(stderr, "Error: could not post bid to self-exchange\n");
		}
	}

	else{
		Bid_Order bid_order;

		// copy the fingerprint to this bid order
		memcpy(bid_order.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);

		bid_order.location_id = location_id;
		bid_order.data_bytes = data_bytes;
		bid_order.wr_id = bid_match_wr_id;

		// 4.) We can submit this bid order message now
		Channel * out_bid_orders = target_exchange_connection -> out_bid_orders;
		uint64_t bid_order_wr_id;
		uint64_t bid_order_addr;
		printf("[Client %u]. Sending BID order to: %u...\n", self_exchange_id, target_exchange_id);
		// don't have a known wr_id to send, so using specified protocol and retrieving the wr_id back
		ret = submit_out_channel_message(out_bid_orders, &bid_order, NULL, &bid_order_wr_id, &bid_order_addr);
		if (ret != 0){
			fprintf(stderr, "Error: could not submit out channel message with bid order\n");
			return -1;
		}
	}

	// maintain mapping between bid_match_wr_id and fingerprint
	Outstanding_Bid * outstanding_bid = malloc(sizeof(Outstanding_Bid));
	outstanding_bid -> bid_match_wr_id = bid_match_wr_id;
	memcpy(outstanding_bid -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
	
	ret = insert_item_table(exchanges_client -> outstanding_bids, outstanding_bid);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert outstanding bid into table\n");
		return -1;
	}

	if (ret_bid_match_wr_id != NULL){
		*ret_bid_match_wr_id = bid_match_wr_id;
	}
	

	return 0;
}

// For now having 
int submit_offer(Exchanges_Client * exchanges_client, uint32_t location_id, uint8_t * fingerprint, uint64_t data_bytes, uint64_t * ret_offer_resp_wr_id, uint32_t * dest_exchange_id){

	int ret;

	// 1.) Determine what exchange connection to use
	//		- for now equally partitioning across number of exchanges the lower 64 bits of fingerprint
	
	uint32_t self_exchange_id = exchanges_client -> self_exchange_id;

	// -a.) Optionally specifiy the target exchange or use default search method
	uint32_t target_exchange_id;
	if (dest_exchange_id != NULL){
		target_exchange_id = *dest_exchange_id;
	}
	else{
		uint32_t num_exchanges = exchanges_client -> num_exchanges;
		uint64_t least_sig64 = fingerprint_to_least_sig64(fingerprint, FINGERPRINT_NUM_BYTES);
		target_exchange_id = least_sig64 % num_exchanges;
	}

	// b.) Either use self-connection or lookup in table
	Exchange_Connection * target_exchange_connection;
	if (target_exchange_id == self_exchange_id){
		target_exchange_connection = exchanges_client -> self_exchange_connection;
	}
	else {
		Exchange_Connection target_exch;
		target_exch.exchange_id = target_exchange_id;
		Exchange_Connection * dest_exchange_connection = find_item_table(exchanges_client -> exchanges, &target_exch);
		if (dest_exchange_connection == NULL){
			fprintf(stderr, "Error: could not find destination exchange connection with id: %u\n", target_exchange_id);
		}
		target_exchange_connection =  dest_exchange_connection;
	}
	

	// 2.) Ensure that we would be able to receive a response, by submitting item to in channel
	//	   Posts receive item 
	// SHOULD MAKE AN OFFER RESPONSE (As an ack)
	//	- TODO
	uint64_t offer_resp_wr_id = 0;

	// 3.) Now we can populate our offre_order telling the exchange the wr_id to send when it finds match
	// 		- If self, then just call the post_bid function directly
	if (target_exchange_id == self_exchange_id){
		Exchange * exchange = exchanges_client -> self_exchange;
		printf("[Client %u]. Posting self-OFFER...\n", self_exchange_id);
		ret = post_offer(exchange, fingerprint, data_bytes, location_id);
		if (ret != 0){
			fprintf(stderr, "Error: could not post offer to self-exchange\n");
		}
	}

	else{
		Offer_Order offer_order;

		// copy the fingerprint to this bid order
		memcpy(offer_order.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);

		offer_order.location_id = location_id;
		offer_order.data_bytes = data_bytes;

		// 4.) We can submit this bid order message now
		Channel * out_offer_orders = target_exchange_connection -> out_offer_orders;
		uint64_t offer_order_wr_id;
		uint64_t offer_order_addr;
		printf("[Client %u]. Sending OFFER order to: %u...\n", self_exchange_id, target_exchange_id);
		// don't have a known wr_id to send, so using specified protocol and retrieving the wr_id back
		ret = submit_out_channel_message(out_offer_orders, &offer_order, NULL, &offer_order_wr_id, &offer_order_addr);
		if (ret != 0){
			fprintf(stderr, "Error: could not submit out channel message with bid order\n");
			return -1;
		}
	}

	if (ret_offer_resp_wr_id != NULL){
		*ret_offer_resp_wr_id = offer_resp_wr_id;
	}

	return 0;
}

// wait for completition threads to finish (currently infinite loop) for both exchange client and exchange
int keep_alive_and_block(Exchanges_Client * exchanges_client){

	// for now hardcoding to 1 thread for exchange_client and exchanges, but should be dynamic and based on NIC configurations...
	// should be passed in as argument....

	Exchange * self_exchange = exchanges_client -> self_exchange;
	int exch_num_threads = 1;
	pthread_t * exch_completition_threads = self_exchange -> completion_threads;
	for (int i = 0; i < exch_num_threads; i++){
		pthread_join(exch_completition_threads[i], NULL);
	}

	int client_num_threads = 1;
	pthread_t * client_completition_threads = exchanges_client -> completion_threads;
	for (int i = 0; i < client_num_threads; i++){
		pthread_join(client_completition_threads[i], NULL);
	}

	return 0;
}


	