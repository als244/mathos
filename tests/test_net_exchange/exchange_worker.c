#include "exchange_worker.h"

// called internally from post_bid and post_offer
int handle_bid_match_notify(Exchange * exchange, uint32_t offer_location_id, uint32_t bid_location_id, uint64_t bid_match_wr_id) {

	int ret;

	// used for looking up client connections needed to get to channel buffer to send to bid participant
	Table * client_conn_table = exchange -> clients;
	Client_Connection target_client_conn;
	target_client_conn.location_id = bid_location_id;

	Client_Connection * client_connection = find_item_table(client_conn_table, &target_client_conn);

	if (client_connection == NULL){
		fprintf(stderr, "Error: could not find client connection for id: %u\n", bid_location_id);
		return -1;
	}

	// message to send to bid participant
	Bid_Match bid_match;
	bid_match.location_id = offer_location_id;

	Channel * out_bid_matches = client_connection -> out_bid_matches;

	printf("[Exchange %u]. Sending BID_MATCH notification to: %u...\n", exchange -> id, bid_location_id);

	// specifying the wr_id to use and don't need the addr of bid_match within registered channel buffer
	uint64_t wr_id_to_send = bid_match_wr_id;
	ret = submit_out_channel_message(out_bid_matches, &bid_match, &wr_id_to_send, NULL, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not submit out channel message with bid order\n");
		return -1;
	}

	return 0;
}


// called by the completion handler
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
	switch(order_type) {
		case BID_ITEM:
			fingerprint = ((Bid_Order *) client_order) -> fingerprint;
			printf("[Exchange %u] Posting BID from client: %u...\n", exchange -> id, ((Bid_Order *) client_order) -> location_id);
			ret = post_bid(exchange, fingerprint, ((Bid_Order *) client_order) -> location_id, ((Bid_Order *) client_order) -> wr_id);
			break;
		case OFFER_ITEM:
			fingerprint = ((Offer_Order *) client_order) -> fingerprint;
			printf("[Exchange %u] Posting OFFER from client: %u...\n", exchange -> id, ((Offer_Order *) client_order) -> location_id);
			ret = post_offer(exchange, fingerprint, ((Offer_Order *) client_order) -> location_id);
			break;
		case FUTURE_ITEM:
			fingerprint = ((Future_Order *) client_order) -> fingerprint;
			printf("[Exchange %u] Posting Future from client: %u...\n", exchange -> id, ((Future_Order *) client_order) -> location_id);
			ret = post_offer(exchange, fingerprint, ((Future_Order *) client_order) -> location_id);
			break;
		default:
			fprintf(stderr, "Error: order type not supported\n");
			return -1;
	}

	if (ret != 0){
		fprintf(stderr, "Error: unsuccessful in posting order\n");
		return -1;
	}

	return 0;
}


// For now only care about receive completions (aka sender != self) ...
void * exchange_completion_handler(void * _thread_data){

	int ret;

	Exchange_Completion * completion_handler_data = (Exchange_Completion *) _thread_data;

	uint64_t completion_thread_id = completion_handler_data -> completion_thread_id;
	Exchange * exchange = completion_handler_data -> exchange;


	// really should look up based on completion_thread_id
	struct ibv_cq_ex * cq_ex = exchange -> exchange_cq;

	struct ibv_cq * cq = ibv_cq_ex_to_cq(cq_ex);

	enum ibv_wc_status status;
	uint64_t wr_id;

	MessageType message_type;
	uint32_t sender_id;

	uint32_t self_id = exchange -> id;

	
	Client_Connection * client_connection;

	// used for looking up exchange connections needed to get to channel buffer
	Table * client_conn_table = exchange -> clients;
	Client_Connection target_client_conn;

	int handle_ret;
	bool work_err;
	bool is_done = false;

	int num_complete;
	// maximum number of completion queue entries to return per poll...
	int max_wc_entries = 1;

	struct ibv_wc wc;

	// For now, infinite loop
	while (!is_done){

		num_complete = 0;

		// poll for next completition
		while (num_complete == 0){
			num_complete = ibv_poll_cq(cq, max_wc_entries, &wc);
		}
		if (num_complete < 0){
			fprintf(stderr, "Error: ibv_poll_cq() failed\n");
			is_done = true;
			continue;
		}

		// now we have consumed a new work completition
		// assert(num_complete > 0 && num_complete <= max_wc_entries)
		
		// Consume the completed work request
		wr_id = wc.wr_id;
		status = wc.status;
		// other fields as well...
		// could get timestamp/source qp num/other info...

		message_type = decode_wr_id(wr_id, &sender_id);

		/* DO SOMETHING WITH wr_id! */
		printf("[Exchange %u]. Saw completion of wr_id = %lu (Sender_ID = %u, MessageType = %s)\n\tStatus: %d\n\n", self_id, wr_id, sender_id, message_type_to_str(message_type), status);

		if (status != IBV_WC_SUCCESS){
			fprintf(stderr, "Error: work request id %lu had error\n", wr_id);
			work_err = true;
			// DO ERROR HANDLING HERE!
		}
		else{
			work_err = false;
		}

		// for now can ignore the send completions
		// eventually need to have an ack in place and also
		// need to remove the send data from channel's buffer table
		if ((!work_err) && (sender_id != self_id)){

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
					exit(1);
				}
			}
			else{
				fprintf(stderr, "Error: within completion handler, could not find exchange connection with id: %u\n", sender_id);
			}
		}
	}

	return NULL;
}