#include "exchange_client.h"


#define MY_EXCH_ID 0UL
#define MY_EXCH_IP "192.168.50.23"
#define OTHER_EXCH_ID 1UL
#define OTHER_EXCH_IP "192.168.50.32"
#define SERVER_PORT_EXCH "7471"
#define SERVER_PORT_CLIENT "7472"


// NOT DOING MUCH ERROR CHECKING HERE...

int main(int argc, char * argv[]){

	int ret;


	// 1.) Create Own Exchange 
	printf("Initializing Exchange...\n");
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
	uint64_t max_outstanding_bids = 1UL << 12;
	Exchanges_Client * exchanges_client = init_exchanges_client(max_exchanges, max_outstanding_bids);
	if (exchanges_client == NULL){
		fprintf(stderr, "Error: could not initialize exchanges client\n");
		return -1;
	}

	// Maximum number of 2^15 work requests for each send/recv side of queue pair
	//	- need not set to be symmetric. can also use SRQ which has limit of 2^15 - 1


	// For now: same QP used for 3 recv channels and and 1 send channel in "setup_exchange_connection"
	//		- per connection! (but connections can be sharded across the UD QPs)
	// For now: same QP used fdor 3 send chanels and 1 recv channel in "setup_client_connection"
	//		- per connection! (but connections can sharded across the UD QPs)

	// THUS: for completley symmetric channel capacity must be <= max_work_requests_per_qp_per_side / (max_per_side_channels_per_connection * num_connections_per_qp)
	//														   <= 2^15 / (3 * 1) <= 2^13, but this is excessive...
	//	- for now doing it this way because simpler to configure...

	//	- IMPORTANT: need not be symmetric across different types of channels (message types, recv vs. send)
	//					- in reality we want the number of recv work requests for outstanding bids to be the highest by far!

	uint16_t capacity_channels = 1U << 12;

	// 3.) Setup connection to other exchanges
	printf("Setting up connection to exchange: %lu\n\n", OTHER_EXCH_ID);
	//		- currently not asynchronous, so need to do in the proper order, otherwise deadlock
	//		- aka should be reverse order on the other end (setup_client_connection first)
	// sets up client connection to exchange
	ret = setup_exchange_connection(exchanges_client, OTHER_EXCH_ID, OTHER_EXCH_IP, MY_EXCH_ID, MY_EXCH_IP, SERVER_PORT_CLIENT, capacity_channels);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup exchange connection\n");
		return -1;
	}

	// 4.) Setup connection to client
	printf("Setting up connection with client: %lu\n\n", OTHER_EXCH_ID);
	// sets up exchange connection to client
	ret = setup_client_connection(exchange, MY_EXCH_ID, MY_EXCH_IP, OTHER_EXCH_ID, OTHER_EXCH_IP, SERVER_PORT_EXCH, capacity_channels);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup client connection\n");
		return -1;
	}

	printf("SUCCESS!!!\n\n");

	return 0;

}