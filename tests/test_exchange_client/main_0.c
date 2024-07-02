#include "exchange_client.h"


#define MY_ID 0UL
#define MY_IP "192.168.50.23"
#define OTHER_ID 1UL
#define OTHER_IP "192.168.50.32"
#define SERVER_PORT_EXCH "7471"
#define SERVER_PORT_CLIENT "7472"


// NOT DOING MUCH ERROR CHECKING HERE...

int main(int argc, char * argv[]){

	int ret;

	// OPEN IB DEVICE CONTEXT. USE DEVICE 0 FOR TESTING
	// Needed for initializing exchange & exchanges_client PD, CQ, and QPs

	int num_devices;
	struct ibv_device ** devices = ibv_get_device_list(&num_devices);
	if (devices == NULL){
		fprintf(stderr, "Error: could not get ibv_device list\n");
		return -1;
	}

	if (num_devices == 0){
		fprintf(stderr, "Error: no rdma device fonud\n");
		return -1;
	}

	struct ibv_device * device = devices[0];
	
	struct ibv_context * ibv_dev_ctx = ibv_open_device(device);
	if (ibv_dev_ctx == NULL){
		fprintf(stderr, "Error: could not open device to get ibv context\n");
		return -1;
	}



	// 1.) Create Own Exchange 
	printf("Initializing Exchange...\n");

	uint64_t num_exchanges = 2;
	uint64_t exchange_id = MY_ID;

	// Only 1 exchange so doing full range	
	uint64_t start_val = get_start_val_from_exch_id(num_exchanges, exchange_id);
	// wrap around, max value
	uint64_t end_val = get_end_val_from_exch_id(num_exchanges, exchange_id);

	uint64_t max_bids = 1UL << 36;
	uint64_t max_offers = 1UL << 36;
	uint64_t max_futures = 1UL << 36;

	// really should be num_exchanges - 1
	uint64_t max_clients = num_exchanges;


	Exchange * exchange = init_exchange(exchange_id, start_val, end_val, max_bids, max_offers, max_futures, max_clients, ibv_dev_ctx);
	if (exchange == NULL){
		fprintf(stderr, "Error: could not initialize exchange\n");
		return -1;
	}


	// 2.) Initialize Own Exchanges_Client
	printf("Initializing Exchanges Client...\n\n");
	uint64_t max_outstanding_bids = 1UL << 12;
	uint64_t max_exchanges = num_exchanges;
	Exchanges_Client * exchanges_client = init_exchanges_client(num_exchanges, max_exchanges, max_outstanding_bids, exchange_id, exchange, ibv_dev_ctx);
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

	uint16_t capacity_channels = 1U << 10;

	// 3.) Setup connection to other exchanges
	printf("Setting up connection to exchange: %lu\n\n", OTHER_ID);
	//		- currently not asynchronous, so need to do in the proper order, otherwise deadlock
	//		- aka should be reverse order on the other end (setup_client_connection first)
	// sets up client connection to exchange
	ret = setup_exchange_connection(exchanges_client, OTHER_ID, OTHER_IP, MY_ID, MY_IP, SERVER_PORT_CLIENT, capacity_channels);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup exchange connection\n");
		return -1;
	}

	// 4.) Setup connection to client
	printf("Setting up connection with client: %lu\n\n", OTHER_ID);
	// sets up exchange connection to client
	ret = setup_client_connection(exchange, MY_ID, MY_IP, OTHER_ID, OTHER_IP, SERVER_PORT_EXCH, capacity_channels);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup client connection\n");
		return -1;
	}

	// 5.) Submit messages
	printf("\n\nNow actually submiting orders...!\n\n");

	uint8_t * example_fingerprint = (uint8_t *) malloc(FINGERPRINT_NUM_BYTES);
	// for now assume that we will be sending to exchange 1 (in the upper half of 0xFFFF)
	for (int i = 0; i < FINGERPRINT_NUM_BYTES; i++){
		example_fingerprint[i] = (uint8_t) 255;
	}

	uint64_t example_data_bytes = 100;

	uint64_t bid_match_wr_id;
	ret = submit_bid(exchanges_client, MY_ID, example_fingerprint, example_data_bytes, &bid_match_wr_id, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not submit bid\n");
		return -1;
	}

	keep_alive_and_block(exchanges_client);

	return 0;

}