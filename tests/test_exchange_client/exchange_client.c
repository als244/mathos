#include "exchange_client.h"

int connection_item_cmp(void * connection_item, void * other_item) {
	uint64_t id_a = ((Exchange_Connection *) connection_item) -> id;
	uint64_t id_b = ((Exchange_Connection *) other_item) -> id;
	return id_a - id_b;
}

uint64_t connection_hash_func(void * connection_item, uint64_t table_size) {
	uint64_t key = ((Exchange_Connection *) connection_item) -> id;
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
	uint64_t wr_id_a = ((Bid *) bid_item) -> wr_id;
	uint64_t wr_id_b = ((Bid *) other_item) -> wr_id;
	return wr_id_a - wr_id_b;
}

uint64_t bid_hash_func(void * bid_item, uint64_t table_size) {
	uint64_t key = ((Bid *) bid_item) -> wr_id;
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
	Hash_Func hash_func_connection = &connection_hash_func;
	Item_Cmp item_cmp_connection = &connection_item_cmp;


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
	Hash_Func hash_func_bid = &bid_hash_func;
	Item_Cmp item_cmp_bid = &bid_item_cmp;

	Table * outstanding_bids = init_table(min_outstanding_bids_size, max_outstanding_bids, load_factor, shrink_factor, hash_func_bid, item_cmp_bid);
	if (outstanding_bids == NULL){
		fprintf(stderr, "Error: could not initialize outstanding_bids table\n");
		return NULL;
	}

	exchanges_client -> outstanding_bids = outstanding_bids;


	pthread_mutex_init(&(exchanges_client -> exchanges_client_lock), NULL);

	return exchanges_client;
}


int setup_exchange_connection(Exchanges_Client * exchanges_client, uint64_t exchange_id, char * exchange_ip, char * exchange_port, uint64_t client_id, char * client_ip, char * client_port) {

}