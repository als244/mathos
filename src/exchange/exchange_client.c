#include "exchange_client.h"

int exch_connection_item_cmp(void * connection_item, void * other_item) {
	uint64_t id_a = ((Exchange_Connection *) connection_item) -> exchange_id;
	uint64_t id_b = ((Exchange_Connection *) other_item) -> exchange_id;
	return id_a - id_b;
}

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


int bid_item_cmp(void * bid_item, void * other_item) {
	uint64_t wr_id_a = ((Outstanding_Bid *) bid_item) -> wr_id;
	uint64_t wr_id_b = ((Outstanding_Bid *) other_item) -> wr_id;
	return wr_id_a - wr_id_b;
}

uint64_t bid_hash_func(void * bid_item, uint64_t table_size) {
	uint64_t key = ((Outstanding_Bid *) bid_item) -> wr_id;
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



Exchanges_Client * init_exchanges_client(uint64_t max_exchanges, uint64_t max_outstanding_bids) {

	Exchanges_Client * exchanges_client = (Exchanges_Client *) malloc(sizeof(Exchanges_Client));
	if (exchanges_client == NULL){
		fprintf(stderr, "Error: malloc failed in allocating exchanges client\n");
		return NULL;
	}

	// eventually will grow such that num_exchanges == max_exchanges
	exchanges_client -> num_exchanges = 0;
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

	return exchanges_client;
}


// the smaller of exchange_id and local_id will serve as the "server" when establishing RDMA connection
int setup_exchange_connection(Exchanges_Client * exchanges_client, uint64_t exchange_id, char * exchange_ip, uint64_t location_id, char * location_ip, char * server_port, uint16_t capacity_channels) {

	int ret;

	Exchange_Connection * exchange_connection = (Exchange_Connection *) malloc(sizeof(Exchange_Connection));
	if (exchange_connection == NULL){
		fprintf(stderr, "Error: malloc failed when allocating exchange connection\n");
		return -1;
	}

	exchange_connection -> exchange_id = exchange_id;
	exchange_connection -> start_val = 0;
	// uint64_t, so loop around gives max
	exchange_connection -> end_val = -1;


	Connection * connection;
	//RDMAConnectionType exchange_connection_type = RDMA_UD;
	// FOR NOW MAKING IT RDMA_RC FOR EASIER ESTABLISHMENT BUT WILL BE RDMA_UD
	RDMAConnectionType exchange_connection_type = RDMA_RC;

	int is_server;
	uint64_t server_id, client_id;
	char *server_ip, *client_ip;
	struct ibv_qp *server_qp, *client_qp;
	if (location_id < exchange_id){
		is_server = 1;
		server_id = location_id;
		server_ip = location_ip;
		server_qp = exchanges_client -> exchange_client_qp;
		client_id = exchange_id;
		client_ip = exchange_ip;
		client_qp = NULL;
	}
	else{
		is_server = 0;
		server_id = exchange_id;
		server_ip = exchange_ip;
		server_qp = NULL;
		client_id = location_id;
		client_ip = location_ip;
		client_qp = exchanges_client -> exchange_client_qp;
	}

	ret = setup_connection(exchange_connection_type, is_server, server_id, server_ip, server_port, server_qp, 
							client_id, client_ip, client_qp, &connection);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup exchange connection\n");
		return -1;
	}


	exchange_connection -> connection = connection;

	// now we need to allocate and register ring buffers to receive incoming orders
	exchange_connection -> capacity_channels = capacity_channels;

	exchange_connection -> out_bid_orders = init_channel(location_id, exchange_id, capacity_channels, BID_ORDER, sizeof(Bid_Order), false, connection -> pd, exchanges_client -> exchange_client_qp);
	exchange_connection -> out_offer_orders = init_channel(location_id, exchange_id, capacity_channels, OFFER_ORDER, sizeof(Offer_Order), false, connection -> pd, exchanges_client -> exchange_client_qp);
	exchange_connection -> out_future_orders = init_channel(location_id, exchange_id, capacity_channels, FUTURE_ORDER, sizeof(Future_Order), false, connection -> pd, exchanges_client -> exchange_client_qp);
	// setting is_recv to true, because we will be posting sends from this channel
	exchange_connection -> in_bid_matches = init_channel(location_id, exchange_id, capacity_channels, BID_MATCH, sizeof(Bid_Match), true, connection -> pd, exchanges_client -> exchange_client_qp);

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

	exchanges_client -> num_exchanges += 1;

	return 0;

}
