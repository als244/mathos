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
// ALL ARE CALLED WITHIN "data_completition_handler" and error checked prior...

int handle_data_initiate(Data_Controller * data_controller, Data_Connection * data_connection, uint64_t wr_id) {
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


// For now only care about receive completitions (aka sender != self) ...
void * data_completition_handler(void * _thread_data){

	int ret;

	Data_Completition * completition_handler_data = (Data_Completition *) _thread_data;

	int completition_thread_id = completition_handler_data -> completion_thread_id;
	Data_Controller * data_controller = completition_handler_data -> data_controller;

	// really should look up based on completition_thread_id
	struct ibv_cq_ex * cq = data_controller -> data_cq;

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
            printf("[Data Controller %u]. Saw completion of wr_id = %ld (Sender_ID = %u, MessageType = %s)\n\tStatus: %d\n\n", self_id, wr_id, sender_id, message_type_to_str(message_type), status);

            if (status != IBV_WC_SUCCESS){
                fprintf(stderr, "Error: work request id %ld had error\n", wr_id);
                // DO ERROR HANDLING HERE!
            }

        	// inbound (recveive) completitions
        	if (sender_id != self_id){

        		// lookup the connection based on sender id

        		target_data_conn.peer_id = sender_id;

        		data_connection = find_item_table(data_conn_table, &target_data_conn);
        		if (data_connection != NULL){
	        		// MAY WANT TO HAVE SEPERATE THREADS FOR PROCESSING THE WORK DO BE DONE...
		        	switch(message_type){
		        		case DATA_INITIATE:
		        			handle_ret = handle_data_initiate(data_controller, data_connection, wr_id);
		        			break;
		        		case DATA_RESPONSE:
		        			handle_ret = handle_data_response(data_controller, data_connection, wr_id);
		        			break;
		        		case DATA_PACKET:
		        			handle_ret = handle_data_packet(data_controller, data_connection, wr_id);
		        			break;
		        		default:
		        			fprintf(stderr, "Error: unsupported data completition handler message type of: %d\n", message_type);
		        			break;
		        	}
		        	if (handle_ret != 0){
		        		fprintf(stderr, "Error: data completition handler had an error\n");
		        	}
		        }
	        	else{
	        		fprintf(stderr, "Error: within completition handler, could not find data connection with id: %u\n", sender_id);
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
            fprintf(stderr, "Error: could not do next poll for completition queue\n");
            return NULL;
        }
    }

    // should never get here...
    ibv_end_poll(cq);

    return NULL;
}


Data_Controller * init_data_controller(uint32_t self_id, uint32_t max_connections, struct ibv_pd * data_pd, struct ibv_qp * data_qp, struct ibv_cq_ex * data_cq, int num_cqs){

	Data_Controller * data_controller = (Data_Controller *) malloc(sizeof(Data_Controller));
	if (data_controller == NULL){
		fprintf(stderr, "Error: malloc failed initializing data controller\n");
		return NULL;
	}

	data_controller -> self_id = self_id;
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

	data_controller -> data_pd = data_pd;
	data_controller -> data_qp = data_qp;
	data_controller -> data_cq = data_cq;
	// for now just passing in 1 to num_cqs, but this should be higher, likely in proportion with number of QPs or SRQs..
	data_controller -> num_cqs = num_cqs;


	// INITIALIZE COMPLETITION QUEUE HANDLER THREADS

	// num threads should equal number of CQs
	int num_threads = num_cqs;
	Data_Completition * handler_thread_data = malloc(num_threads * sizeof(Data_Completition));
	if (handler_thread_data == NULL){
		fprintf(stderr, "Error: malloc failed allocating handler thread data for data controller\n");
		return NULL;
	}

	pthread_t * completion_threads = (pthread_t *) malloc(num_threads * sizeof(pthread_t));
	if (completion_threads == NULL){
		fprintf(stderr, "Error: malloc failed allocating pthreads for completition handlers for data controller\n");
		return NULL;
	}

	data_controller -> completion_threads = completion_threads;

	for (int i = 0; i < num_threads; i++){
		handler_thread_data[i].completion_thread_id = i;
		handler_thread_data[i].data_controller = data_controller;
		// start the completion thread
		pthread_create(&completion_threads[i], NULL, data_completition_handler, (void *) &handler_thread_data[i]);
	}

	return data_controller;
}