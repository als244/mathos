#ifndef EXCHANGE_CLIENT_H
#define EXCHANGE_CLIENT_H

#include "common.h"
#include "table.h"
#include "communicate.h"
#include "channel.h"
#include "exchange.h"


typedef struct outstanding_bid {
	uint64_t wr_id;
	uint64_t location_id;
	uint8_t * fingerprint;
} Outstanding_Bid;


typedef struct Exchange_Connection {
	uint64_t exchange_id;
	uint64_t start_val;
	uint64_t end_val;
	Connection * connection;
	// To withstand bursts without locking
	uint16_t capacity_channels;
	Channel * out_bid_orders;
	Channel * out_offer_orders;
	Channel * out_future_orders;
	Channel * in_bid_matches;
} Exchange_Connection;

typedef struct Exchanges_Client {
	// for now assuming after intialization num_exchanges == max_exchanges
	uint64_t num_exchanges;
	uint64_t max_exchanges;
	uint64_t max_outstanding_bids;
	// could make exchanges a simple array, but leaving room for growing/shrinking exchanges protocol...
	Table * exchanges;
	Table * outstanding_bids;
	// not sure what this might be needed for yet...
	pthread_mutex_t exchanges_client_lock;
	// initialized upon first connection and then reused to connect to other exchanges
	struct ibv_qp * exchange_client_qp;
} Exchanges_Client;



Exchanges_Client * init_exchanges_client(uint64_t max_exchanges, uint64_t max_outstanding_bids);

// providing local_id & exchange_id to know which end will serve as the server during connection establishment
// always saying that smaller id will be the server
int setup_exchange_connection(Exchanges_Client * exchanges_client, uint64_t exchange_id, char * exchange_ip, uint64_t location_id, char * location_ip, char * server_port, uint16_t capacity_channels);



#endif