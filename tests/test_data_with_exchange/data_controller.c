#include "data_controller.h"

int data_connection_cmp(void * data_connection, void * other_connection) {
	uint32_t id_a = ((Data_Connection *) data_connection) -> peer_id;
	uint32_t id_b = ((Data_Connection *) other_connection) -> peer_id;
	return id_a - id_b;
}

uint64_t data_connection_hash_func(void * data_connection, uint64_t table_size) {
	uint32_t key = ((Data_Connection *) data_connection) -> peer_id;
	// Take from "https://gist.github.com/badboy/6267743"
	// Credit: Robert Jenkins
	key = (key+0x7ed55d16) + (key<<12);
   	key = (key^0xc761c23c) ^ (key>>19);
   	key = (key+0x165667b1) + (key<<5);
   	key = (key+0xd3a2646c) ^ (key<<9);
   	key = (key+0xfd7046c5) + (key<<3);
   	key = (key^0xb55a4f09) ^ (key>>16);
	return (uint64_t) key % table_size;
}


// FUNCTIONS TO HANDLE RECEIVE WORK COMPELTITIONS...
// ALL ARE CALLED WITHIN "data_completion_handler" and error checked prior...

int handle_data_request(Data_Controller * data_controller, Data_Connection * data_connection, uint64_t wr_id) {
	
	int ret;

	Channel * in_data_req = data_connection -> in_data_req;
	uint64_t channel_item_id = wr_id;

	Data_Request data_request;

	// ensure to replace the receive with a new one
	// all control channel items have wr_id as their id
	ret = extract_channel_item(in_data_req, channel_item_id, true, &data_request);
	if (ret != 0){
		fprintf(stderr, "Error: could not extract data request from in channel\n");
		return -1;
	}


	uint32_t transfer_start_id = data_request.transfer_start_id;
	uint8_t * fingerprint = data_request.fingerprint;

	Data_Channel * out_data_channel = data_connection -> out_data;

	// FIND OBJECT LOCATION AND SUBMIT OUT TRANSFER
	Obj_Location * obj_location;
	ret = lookup_obj_location(data_controller -> inventory, fingerprint, &obj_location);
	if (ret != 0){
		fprintf(stderr, "Error: could not find fingerprint in inventory within handling data request\n");
		return -1;
	}

	// make sure to wait until object is available.
	pthread_mutex_lock(&(obj_location -> inbound_lock));
	if (!obj_location -> is_available){
		fprintf(stderr, "Error: object is not available\n");
		// BIG TODO: SHOULD EITHER FORCE AVAILABLITY OR RETRY HERE...?
		// for now just failing over...
		pthread_mutex_unlock(&(obj_location -> inbound_lock));
		return -1;
	}	

	// Now need to acquire the outbound lock and increment the inflight counter before releasing inbound
	pthread_mutex_lock(&(obj_location -> outbound_lock));
	
	
	// BIG TODO: SHOULD HANDLE DOING MULTIPLE TRANSFERS OF LARGE OBJECTS HERE.
	// for now assuming all transfers fit in 32-bits
	ret = submit_out_transfer(out_data_channel, fingerprint, obj_location -> addr, obj_location -> data_bytes, obj_location -> lkey, transfer_start_id);
	if (ret != 0){
		fprintf(stderr, "Error: issue submitting outbound transfer\n");
		pthread_mutex_unlock(&(obj_location -> inbound_lock));
		pthread_mutex_unlock(&(obj_location -> outbound_lock));
		return -1;
	}

	// now outbound transfer was successful so increment outbound in-flight counter
	// when we see send work request completion indicated completed transfer, 
	// we obtain this outbound lock and decrement
	obj_location -> outbound_inflight_cnt += 1;
	pthread_mutex_unlock(&(obj_location -> outbound_lock));

	// now that the outbound counter has been incremented we can release the inbound lock
	// this prevents moving the object's availability status (i.e. doing migration) during ongoing transfer
	pthread_mutex_lock(&(obj_location -> inbound_lock));

	return 0;

}

int handle_data_response(Data_Controller * data_controller, Data_Connection * data_connection, uint64_t wr_id) {
	return 0;
}

int handle_data_packet(Data_Controller * data_controller, Data_Connection * data_connection, uint64_t wr_id) {

	int ret;

	uint32_t packet_id = decode_packet_id(wr_id);
	Data_Channel * in_data = data_connection -> in_data;

 	Transfer_Complete * transfer_complete;
	ret = ack_packet_local(in_data, packet_id, &transfer_complete);
	if (ret != 0){
		fprintf(stderr, "Error: could not handle data packet\n");
		return -1;
	}
	// THERE WAS A COMPLETITION
	if (transfer_complete != NULL){
		printf("[Data Controller %u]. Completed transfer.\n\tNow at Addr: %p\n\tTransfer Start ID: %u\n\tFingerprint: ", data_controller -> self_id, transfer_complete -> addr, transfer_complete -> start_id);
		print_hex(transfer_complete -> fingerprint, FINGERPRINT_NUM_BYTES);
		free(transfer_complete);
	}
	return 0;
}


// For now only care about receive completions (aka sender != self) ...
void * data_completion_handler(void * _thread_data){

	int ret;

	Data_Completion * completion_handler_data = (Data_Completion *) _thread_data;

	int completion_thread_id = completion_handler_data -> completion_thread_id;
	Data_Controller * data_controller = completion_handler_data -> data_controller;

	// really should look up based on completion_thread_id
	struct ibv_cq_ex * cq = data_controller -> data_cq;

    struct ibv_poll_cq_attr poll_qp_attr = {};
    ret = ibv_start_poll(cq, &poll_qp_attr);

    // If Error after start, do not call "end_poll"
    if ((ret != 0) && (ret != ENOENT)){
        fprintf(stderr, "Error: could not start poll for completion queue\n");
        return NULL;
    }

    // if ret = 0, then ibv_start_poll already consumed an item
    int seen_new_completion;
    int is_done = 0;
    
    enum ibv_wc_status status;
    uint64_t wr_id;

    MessageType message_type;
    uint32_t sender_id;

    uint32_t self_id = data_controller -> self_id;

    
    Data_Connection * data_connection;

    // used for looking up data connections needed to get to channels
    Table * data_conn_table = data_controller -> data_connections_table;
    Data_Connection target_data_conn;

    int handle_ret;

    // For now, infinite loop
    while (!is_done){
        // return is 0 if a new item was cosumed, otherwise it equals ENOENT
        if (ret == 0){
            seen_new_completion = 1;
        }
        else{
            seen_new_completion = 0;
        }
        
        // Consume the completed work request
        wr_id = cq -> wr_id;
        status = cq -> status;
        // other fields as well...
        if (seen_new_completion){

        	message_type = decode_wr_id(wr_id, &sender_id);

        	/* DO SOMETHING WITH wr_id! */
            printf("[Data Controller %u]. Saw completion of wr_id = %ld (Sender_ID = %u, MessageType = %s)\n\tStatus: %d\n\n", self_id, wr_id, sender_id, message_type_to_str(message_type), status);

            if (status != IBV_WC_SUCCESS){
                fprintf(stderr, "Error: work request id %ld had error\n", wr_id);
                // DO ERROR HANDLING HERE!
            }

        	// inbound (recveive) completions
        	if (sender_id != self_id){

        		// lookup the connection based on sender id

        		target_data_conn.peer_id = sender_id;

        		data_connection = find_item_table(data_conn_table, &target_data_conn);
        		if (data_connection != NULL){
	        		// MAY WANT TO HAVE SEPERATE THREADS FOR PROCESSING THE WORK DO BE DONE...
		        	switch(message_type){
		        		case DATA_REQUEST:
		        			handle_ret = handle_data_request(data_controller, data_connection, wr_id);
		        			break;
		        		case DATA_PACKET:
		        			handle_ret = handle_data_packet(data_controller, data_connection, wr_id);
		        			break;
		        		default:
		        			fprintf(stderr, "Error: unsupported data completion handler message type of: %d\n", message_type);
		        			break;
		        	}
		        	if (handle_ret != 0){
		        		fprintf(stderr, "Error: data completion handler had an error\n");
		        	}
		        }
	        	else{
	        		fprintf(stderr, "Error: within completion handler, could not find data connection with id: %u\n", sender_id);
	        	}
	        }
	        // should be handling send compeltitions here....
	        // need to move outbound messages to global table instead of per-connection!!!
	        // need to lookup by wr_id instead of by packet_id...
	        // else{

	        // }
        }

        // Check for next completed work request...
        ret = ibv_next_poll(cq);

        if ((ret != 0) && (ret != ENOENT)){
            // If Error after next, call "end_poll"
            ibv_end_poll(cq);
            fprintf(stderr, "Error: could not do next poll for completion queue\n");
            return NULL;
        }
    }

    // should never get here...
    ibv_end_poll(cq);

    return NULL;
}


// FOR NOW ASSUME NUM_CQs = 1
Data_Controller * init_data_controller(uint32_t self_id, Inventory * inventory, uint32_t max_connections, int num_cqs, struct ibv_context * ibv_ctx){

	Data_Controller * data_controller = (Data_Controller *) malloc(sizeof(Data_Controller));
	if (data_controller == NULL){
		fprintf(stderr, "Error: malloc failed initializing data controller\n");
		return NULL;
	}

	data_controller -> self_id = self_id;
	data_controller -> inventory = inventory;
	data_controller -> max_connections = max_connections;

	// Can use the fixed-size table functions for connections table
	// SETTING DEFAULT TABLE PARAMS HERE...
	// should really change location / args to config this better
	float load_factor = 1.0f;
	float shrink_factor = 0.0f;
	uint64_t min_connections = max_connections;
	Hash_Func hash_func_data_conn = &data_connection_hash_func;
	Item_Cmp item_cmp_data_conn = &data_connection_cmp;

	Table * data_connections_table = init_table(min_connections, max_connections, load_factor, shrink_factor, hash_func_data_conn, item_cmp_data_conn);
	if (data_connections_table == NULL){
		fprintf(stderr, "Error: could not initialize data connections table\n");
		return NULL;
	}

	data_controller -> data_connections_table = data_connections_table;

	// Set pointers to ibv structs that are needed for channel operations...
	data_controller -> ibv_ctx = ibv_ctx;

	// Need to intialize PD, CQ, and QP here

	// 1.) PD based on inputted configuration
	struct ibv_pd * pd = ibv_alloc_pd(ibv_ctx);
	if (pd == NULL) {
		fprintf(stderr, "Error: could not allocate pd for data_controller\n");
		return NULL;
	}

	// TODO: For 2.) and 3.) really should create multiple copies based on num_cqs or num_qps...
	
	// for now just passing in 1 to num_cqs, but this should be higher, likely in proportion with number of QPs or SRQs..
	data_controller -> num_cqs = num_cqs;


	// 2.) CQ based on inputted configuration
	int num_cq_entries = 1U << 9;

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
		fprintf(stderr, "Error: could not create cq for data_controller\n");
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
	qp_attr.cap.max_send_wr = 1U << 8;  // increase if you want to keep more send work requests in the SQ.
	qp_attr.cap.max_recv_wr = 1U << 8;  // increase if you want to keep more receive work requests in the RQ.
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
		fprintf(stderr, "Error: could not create qp for data_controller\n");
		return NULL;
	}


	data_controller -> data_pd = pd;
	data_controller -> data_qp = qp;
	data_controller -> data_cq = cq;


	// INITIALIZE COMPLETITION QUEUE HANDLER THREADS

	// num threads should equal number of CQs
	int num_threads = num_cqs;
	Data_Completion * handler_thread_data = malloc(num_threads * sizeof(Data_Completion));
	if (handler_thread_data == NULL){
		fprintf(stderr, "Error: malloc failed allocating handler thread data for data controller\n");
		return NULL;
	}

	pthread_t * completion_threads = (pthread_t *) malloc(num_threads * sizeof(pthread_t));
	if (completion_threads == NULL){
		fprintf(stderr, "Error: malloc failed allocating pthreads for completion handlers for data controller\n");
		return NULL;
	}

	data_controller -> completion_threads = completion_threads;

	for (int i = 0; i < num_threads; i++){
		handler_thread_data[i].completion_thread_id = i;
		handler_thread_data[i].data_controller = data_controller;
		// start the completion thread
		pthread_create(&completion_threads[i], NULL, data_completion_handler, (void *) &handler_thread_data[i]);
	}

	return data_controller;
}



int setup_data_connection(Data_Controller * data_controller, uint32_t peer_id, char * self_ip, char * peer_ip, char * server_port, uint32_t capacity_control_channels, 
	uint32_t packet_max_bytes, uint32_t max_packets, uint32_t max_packet_id, uint32_t max_transfers) {

	
	int ret;


	Data_Connection * data_connection = (Data_Connection *) malloc(sizeof(Data_Connection));
	if (data_connection == NULL){
		fprintf(stderr, "Error: malloc failed to allocate data connection\n");
		return -1;
	}

	data_connection -> peer_id = peer_id;
	data_connection -> capacity_control_channels = capacity_control_channels;
	data_connection -> packet_max_bytes = packet_max_bytes;



	uint32_t self_id = data_controller -> self_id;

	Connection * connection;
	//RDMAConnectionType exchange_connection_type = RDMA_UD;
	// FOR NOW MAKING IT RDMA_RC FOR EASIER ESTABLISHMENT BUT WILL BE RDMA_UD
	RDMAConnectionType data_connection_type = RDMA_RC;

	int is_server;
	uint64_t server_id, client_id;
	char *server_ip, *client_ip;
	struct ibv_qp *server_qp, *client_qp;
	struct ibv_cq_ex *server_cq, *client_cq;
	struct ibv_pd *server_pd, *client_pd;
	if (self_id < peer_id){
		is_server = 1;
		server_id = self_id;
		server_ip = self_ip;
		server_pd = data_controller -> data_pd;
		server_qp = data_controller -> data_qp;
		server_cq = data_controller -> data_cq;
		client_id = peer_id;
		client_ip = peer_ip;
		client_pd = NULL;
		client_qp = NULL;
		client_cq = NULL;
	}
	else{
		is_server = 0;
		server_id = peer_id;
		server_ip = peer_ip;
		server_pd = NULL;
		server_qp = NULL;
		server_cq = NULL;
		client_id = self_id;
		client_ip = self_ip;
		client_pd = data_controller -> data_pd;
		client_qp = data_controller -> data_qp;
		client_cq = data_controller -> data_cq;
	}

	// if exchange_client_qp is null, then it will be created, otherwise connection will use that qp
	ret = setup_connection(data_connection_type, is_server, server_id, server_ip, server_port, server_pd, server_qp, server_cq,
							client_id, client_ip, client_pd, client_qp, client_cq, &connection);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup data connection\n");
		return -1;
	}

	// set exchange_client_qp if null
	if (data_controller -> data_pd == NULL){
		data_controller -> data_pd = connection -> pd;
	}
	if (data_controller -> data_qp == NULL){
		data_controller -> data_qp = connection -> cm_id -> qp;
	}
	if (data_controller -> data_cq == NULL){
		data_controller -> data_cq = connection -> cq;
	}

	data_connection -> connection = connection;


	// Now set up channels...

	// A.) Setting Up Control Channels

	// might want to consider putting these on different QP/SRQ/CQ...

	// For now just doing data intiate
	
	// for receiving incoming data requests from others. need to prepopulate the receives because this is an inital message
	Channel * in_data_req = init_channel(self_id, peer_id, capacity_control_channels, DATA_REQUEST, sizeof(Data_Request), true, true, data_controller -> data_pd, data_controller -> data_qp, data_controller -> data_cq);
	// for sending data requests to others
	Channel * out_data_req = init_channel(self_id, peer_id, capacity_control_channels, DATA_REQUEST, sizeof(Data_Request), false, false, data_controller -> data_pd, data_controller -> data_qp, data_controller -> data_cq);
	if ((in_data_req == NULL) || (out_data_req == NULL)){
		fprintf(stderr, "Error: could not initialize control channels\n");
		return -1;
	}
	data_connection -> in_data_req = in_data_req;
	data_connection -> out_data_req = out_data_req;

	// Should also later include data_response messages (which serve as acks for "data_not_found" or other stuff)


	// B.) Setting up Data Channels
	Data_Channel * in_data = init_data_channel(self_id, peer_id, packet_max_bytes, max_packets, max_packet_id, max_transfers, true, data_controller -> data_pd, data_controller -> data_qp, data_controller -> data_cq);
	Data_Channel * out_data = init_data_channel(self_id, peer_id, packet_max_bytes, max_packets, max_packet_id, max_transfers, false, data_controller -> data_pd, data_controller -> data_qp, data_controller -> data_cq);

	if ((in_data == NULL) || (out_data == NULL)){
		fprintf(stderr, "Error: could not initialize data channels\n");
		return -1;
	}

	data_connection -> in_data = in_data;
	data_connection -> out_data = out_data;

	ret = insert_item_table(data_controller -> data_connections_table, data_connection);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert data connection with peer id: %u\n", peer_id);
		return -1;
	}

	return 0;
}


int send_data_request(Data_Controller * data_controller, uint32_t peer_id, uint8_t * fingerprint, void * recv_addr, uint32_t data_bytes, uint32_t lkey, uint32_t * ret_start_id) {

	int ret;

	// 1.) lookup connection to get out_data_req channel
	Data_Connection target_data_conn;
	target_data_conn.peer_id = peer_id;

	Data_Connection * data_connection = find_item_table(data_controller -> data_connections_table, &target_data_conn);
	if (data_connection == NULL){
		fprintf(stderr, "Error: could not find data connection for peer id: %u\n", peer_id);
		return -1;
	}

	// 2.) Ensure that we will be able to receive data by first submitting in_data_transfer
	//		- get the start_id for receives that we will send as part of the outgoing data_request
	Data_Channel * in_data_channel = data_connection -> in_data;
	uint32_t transfer_start_id;
	ret = submit_in_transfer(in_data_channel, fingerprint, recv_addr, data_bytes, lkey, &transfer_start_id);
	if (ret != 0){
		fprintf(stderr, "Error: could not submit inbound transfer receives\n");
		return -1;
	}

	// 3.) Now send an outbound data request to peer with the fingerprint (for the other side to lookup location) 
	//		and the start id so it knows what wr_id's to send to
	Channel * out_data_req = data_connection -> out_data_req;

	// build data request
	Data_Request data_request;
	memcpy(data_request.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
	data_request.transfer_start_id = transfer_start_id;

	// don't have a known wr_id to send, so using specified protocol and retrieving the wr_id back
	
	// not using these for now...
	// maybe want them for fault-handling...??
	uint64_t sent_out_req_wr_id;
	uint64_t sent_out_req_addr;
	ret = submit_out_channel_message(out_data_req, &data_request, NULL, &sent_out_req_wr_id, &sent_out_req_addr);

	if (ret != 0){
		fprintf(stderr, "Error: could not submit outbound data request to peer_id: %u\n", peer_id);
		return -1;
	}

	return 0;
}