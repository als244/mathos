#ifndef EXCHANGE_CLIENT_H
#define EXCHANGE_CLIENT_H

#include "common.h"
#include "table.h"
#include "connection.h"
#include "exchange.h"


typedef struct Bid {
	uint64_t wr_id;
	// going to correspond to the global id of nic assoicated to this clients posting
	uint64_t location_id;
	uint64_t data_bytes;
	unsigned char * fingerprint;
	// MIGHT BE WASTE OF SPACE HERE...
	uint8_t fingerprint_bytes;
} Bid;

typedef struct Exchange_Connection {
	uint64_t id;
	uint64_t start_val;
	uint64_t end_val;
	Connection * connection;
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
} Exchanges_Client;



Exchanges_Client * init_exchanges_client(uint64_t max_exchanges, uint64_t max_outstanding_bids);

// providing local_id & exchange_id to know which end will serve as the server during connection establishment
// always saying that smaller id will be the server
int setup_exchange_connection(Exchanges_Client * exchanges_client, uint64_t exchange_id, char * exchange_ip, char * exchange_port, uint64_t client_id, char * client_ip, char * client_port);



#endif