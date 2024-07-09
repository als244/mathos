#include "exchange_client.h"


#define OTHER_ID 0U
#define OTHER_IP "192.168.50.23"
#define MY_ID 1U
#define MY_IP "192.168.50.32"
#define SERVER_PORT_EXCH "7471"
#define SERVER_PORT_CLIENT "7472"
#define SERVER_PORT_CONTROL "7473"
#define SERVER_PORT_DATA "7474"


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

	uint32_t num_exchanges = 2;
	uint32_t exchange_id = MY_ID;

	// Only 1 exchange so doing full range	
	uint64_t start_val = get_start_val_from_exch_id(num_exchanges, exchange_id);
	// wrap around, max value
	uint64_t end_val = get_end_val_from_exch_id(num_exchanges, exchange_id);

	uint64_t max_bids = 1UL << 36;
	uint64_t max_offers = 1UL << 36;
	uint64_t max_futures = 1UL << 36;

	// really should be num_exchanges - 1
	uint32_t max_clients = num_exchanges;


	Exchange * exchange = init_exchange(exchange_id, start_val, end_val, max_bids, max_offers, max_futures, max_clients, ibv_dev_ctx);
	if (exchange == NULL){
		fprintf(stderr, "Error: could not initialize exchange\n");
		return -1;
	}


	// 2.) Create Own Inventory
	printf("Initializing Inventory...\n");
	uint64_t min_objects = 1UL << 12;
	uint64_t max_objects = 1UL << 36;
	Inventory * inventory = init_inventory(min_objects, max_objects);
	if (inventory == NULL){
		fprintf(stderr, "Error: could not initialize inventory\n");
		return -1;
	}


	// 3.) Create Own Data Controller
	printf("Initializing Data_Controller...\n");
	uint32_t max_connections = num_exchanges;
	int num_cqs = 1;
	Data_Controller * data_controller = init_data_controller(MY_ID, inventory, max_connections, num_cqs, ibv_dev_ctx);
	if (data_controller == NULL){
		fprintf(stderr, "Error: could not initialize data controller\n");
		return -1;
	}


	// 2.) Initialize Own Exchanges_Client
	printf("Initializing Exchanges Client...\n\n");
	uint64_t max_outstanding_bids = 1UL << 8;
	uint32_t max_exchanges = num_exchanges;
	Exchanges_Client * exchanges_client = init_exchanges_client(num_exchanges, max_exchanges, max_outstanding_bids, exchange, data_controller, ibv_dev_ctx);
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

	uint32_t capacity_channels = 1U << 8;


	// 3.) Setup connection to client
	printf("Setting up connection with client: %u\n\n", OTHER_ID);
	//		- currently not asynchronous, so need to do in the proper order, otherwise deadlock
	//		- aka should be reverse order on the other end (setup_client_connection first)
	ret = setup_client_connection(exchange, MY_ID, MY_IP, OTHER_ID, OTHER_IP, SERVER_PORT_CLIENT, capacity_channels);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup client connection\n");
		return -1;
	}

	// THIS EXECUTABLE IS CLIENT SO NEEDS TO WAIT FOR SERVER TO SETUP
	sleep(2);

	// 4.) Setup connection to other exchanges
	printf("Setting up connection to exchange: %u\n\n", OTHER_ID);
	ret = setup_exchange_connection(exchanges_client, OTHER_ID, OTHER_IP, MY_ID, MY_IP, SERVER_PORT_EXCH, capacity_channels);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup exchange connection\n");
		return -1;
	}


	// THIS EXECUTABLE IS CLIENT SO NEEDS TO WAIT FOR SERVER TO SETUP
	sleep(2);

	// 5.) Setup data connection
	printf("Setting up data connection with peer: %u\n\n", OTHER_ID);
	// defined in data_channel.h
	// in local example this is 4096 (i used ifconfig to change eth mtu to 4200), but commonly 1024 (with eth mtu of 1500)
	uint32_t packet_max_bytes = PATH_MTU;
	int packet_id_num_bits = 24;
	// potentially could have more packets than packet id. max packets reference to size of packets hash table
	// could store more packets than id's if need to deal with network loss and re-transmission...
	uint32_t max_packets = 1U << packet_id_num_bits;
	uint32_t max_packet_id = 1U << packet_id_num_bits;
	// max transfers should be order of magnitude less than max packets, but being safe here...
	uint32_t max_transfers = 1U << packet_id_num_bits;

	ret = setup_data_connection(data_controller, OTHER_ID, MY_IP, OTHER_IP, SERVER_PORT_CONTROL, SERVER_PORT_DATA, capacity_channels, 
									packet_max_bytes, max_packets, max_packet_id, max_transfers);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup data connection\n");
		return -1;
	}
	

	// 6.) Fake that we computed an object and put into inventory
	//		- this is simulating what we would be done by the actual function executor...
	printf("Creating fake object and putting in inventory...\n\n");
	uint64_t example_data_bytes = 100 * 4096;
	// fake object
	int * example_data = malloc(example_data_bytes);
	int num_ints = example_data_bytes / sizeof(int);
	for (int i = 0; i < num_ints; i++){
		example_data[i] = i;
	}
	// fake register object
	struct ibv_mr * fake_obj_mr;
	ret = register_virt_memory(data_controller -> data_pd, example_data, example_data_bytes, &fake_obj_mr);
	if (ret != 0){
		fprintf(stderr, "Error: couldn't register fake object with ib verbs\n");
		return -1;
	}

	uint8_t * example_fingerprint = (uint8_t *) malloc(FINGERPRINT_NUM_BYTES);
	// for now assume that we will be sending to exchange 1 (in the upper half of 0xFFFF)
	for (int i = 0; i < FINGERPRINT_NUM_BYTES; i++){
		example_fingerprint[i] = (uint8_t) 255;
	}

	ret = put_obj_local(inventory, example_fingerprint, example_data, example_data_bytes, fake_obj_mr -> lkey);
	if (ret != 0){
		fprintf(stderr, "Error: failed to put fake object in inventory\n");
		return -1;
	}

	// 7.) Submit messages
	printf("\n\nNow actually submiting orders...!\n\n");
	uint64_t offer_wr_id;
	ret = submit_offer(exchanges_client, MY_ID, example_fingerprint, &offer_wr_id, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not submit offer\n\n");
		return -1;
	}

	keep_alive_and_block(exchanges_client);

	return 0;

}
