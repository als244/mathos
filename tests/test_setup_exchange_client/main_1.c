#include "exchange_client.h"


#define OTHER_EXCH_ID 0UL
#define OTHER_EXCH_IP "192.168.50.23"
#define MY_EXCH_ID 1UL
#define MY_EXCH_IP "192.168.50.32"
#define SERVER_PORT_EXCH "7471"
#define SERVER_PORT_CLIENT "7472"


// NOT DOING MUCH ERROR CHECKING HERE...

int main(int argc, char * argv[]){

	int ret;




	// 1.) Create Own Exchange 
	printf("Initializing Exchange...\n\n");
	uint64_t exchange_id = MY_EXCH_ID;

	// Only 1 exchange so doing full range	
	uint64_t start_val = 0;
	// wrap around, max value
	uint64_t end_val = -1;

	uint64_t max_bids = 1UL << 36;
	uint64_t max_offers = 1UL << 36;
	uint64_t max_futures = 1UL << 36;
	uint64_t max_clients = 1UL << 12;


	Exchange * exchange = init_exchange(exchange_id, start_val, end_val, max_bids, max_offers, max_futures, max_clients);
	if (exchange == NULL){
		fprintf(stderr, "Error: could not initialize exchange\n");
		return -1;
	}


	// 2.) Initialize Own Exchanges_Client
	printf("Initializing Exchanges Client...\n\n");
	uint64_t max_exchanges = 1;
	uint64_t max_outstanding_bids = 1UL << 8;
	Exchanges_Client * exchanges_client = init_exchanges_client(max_exchanges, max_outstanding_bids);
	if (exchanges_client == NULL){
		fprintf(stderr, "Error: could not initialize exchanges client\n");
		return -1;
	}

	uint16_t capacity_channels = 1 << 8;

	// 3.) Setup connection to client
	printf("Setting up connection with client: %lu\n\n", OTHER_EXCH_ID);
	//		- currently not asynchronous, so need to do in the proper order, otherwise deadlock
	//		- aka should be reverse order on the other end (setup_client_connection first)
	ret = setup_client_connection(exchange, MY_EXCH_ID, MY_EXCH_IP, OTHER_EXCH_ID, OTHER_EXCH_IP, SERVER_PORT_CLIENT, capacity_channels);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup client connection\n");
		return -1;
	}

	// 4.) Setup connection to other exchanges
	printf("Setting up connection to exchange: %lu\n\n", OTHER_EXCH_ID);
	ret = setup_exchange_connection(exchanges_client, OTHER_EXCH_ID, OTHER_EXCH_IP, MY_EXCH_ID, MY_EXCH_IP, SERVER_PORT_EXCH, capacity_channels);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup exchange connection\n");
		return -1;
	}

	

	printf("SUCCESS!!!\n\n");

	return 0;

}