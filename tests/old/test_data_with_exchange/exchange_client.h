#ifndef EXCHANGE_CLIENT_H
#define EXCHANGE_CLIENT_H

#include "common.h"
#include "table.h"
#include "communicate.h"
#include "channel.h"
#include "exchange.h"
#include "data_controller.h"


typedef struct outstanding_bid {
	uint64_t bid_match_wr_id;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	uint64_t data_bytes;
} Outstanding_Bid;


typedef struct Exchange_Connection {
	uint32_t exchange_id;
	uint64_t start_val;
	uint64_t end_val;
	// To withstand bursts without fully locking
	uint16_t capacity_channels;
	Channel * out_bid_orders;
	Channel * out_offer_orders;
	Channel * out_future_orders;
	Channel * in_bid_matches;
	Connection * connection;
} Exchange_Connection;

typedef struct Exchanges_Client {
	// for now assuming after intialization num_exchanges == max_exchanges
	uint32_t num_exchanges;
	uint32_t max_exchanges;
	uint64_t max_outstanding_bids;
	// could make exchanges a simple array, but leaving room for growing/shrinking exchanges protocol...
	Table * exchanges;
	Table * outstanding_bids;
	// not sure what this might be needed for yet...
	pthread_mutex_t exchanges_client_lock;
	struct ibv_context * ibv_ctx;
	struct ibv_pd * exchange_client_pd;
	// initialized upon first connection and then reused to connect to other exchanges
	// may want to shard across many
	struct ibv_qp * exchange_client_qp;
	// initialized upon first connection and then reused to connect to other exchanges
	// may want to shard across many
	struct ibv_cq_ex * exchange_client_cq;
	uint32_t self_exchange_id;
	Exchange * self_exchange;
	Exchange_Connection * self_exchange_connection;
	Data_Controller * data_controller;
	// number of completion threads should equal number of CQs, likely equal number of QPs...
	pthread_t * completion_threads;
} Exchanges_Client;

typedef struct exchanges_client_completion {
	uint64_t completion_thread_id;
	Exchanges_Client * exchanges_client;
} Exchanges_Client_Completion;



uint64_t get_start_val_from_exch_id(uint32_t num_exchanges, uint32_t exchange_id);
uint64_t get_end_val_from_exch_id(uint32_t num_exchanges, uint32_t exchange_id);


Exchanges_Client * init_exchanges_client(uint32_t num_exchanges, uint32_t max_exchanges, uint64_t max_outstanding_bids, Exchange * self_exchange, Data_Controller * data_controller, struct ibv_context * ibv_ctx);

// providing local_id & exchange_id to know which end will serve as the server during connection establishment
// always saying that smaller id will be the server
int setup_exchange_connection(Exchanges_Client * exchanges_client, uint32_t exchange_id, char * exchange_ip, uint32_t location_id, char * location_ip, char * server_port, uint32_t capacity_channels);


// The last 2 arguments are optional
int submit_bid(Exchanges_Client * exchanges_client, uint32_t location_id, uint8_t * fingerprint, uint64_t data_bytes, uint64_t * ret_bid_match_wr_id, uint32_t * dest_exchange_id);

// The last 2 arguments are optional 
int submit_offer(Exchanges_Client * exchanges_client, uint32_t location_id, uint8_t * fingerprint, uint64_t * ret_offer_resp_wr_id, uint32_t * dest_exchange_id);

// The last 2 arguments are optional
// Submit offer and submit future are near identical implementations...
int submit_future(Exchanges_Client * exchanges_client, uint32_t location_id, uint8_t * fingerprint, uint64_t * ret_future_resp_wr_id, uint32_t * dest_exchange_id);



// can be called at end of main executable to help with testing...
// will cause inifinte loop because completion threads never finish...
int keep_alive_and_block(Exchanges_Client * exchanges_client);

#endif