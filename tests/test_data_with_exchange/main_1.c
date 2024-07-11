#include "exchange_client.h"


#define OTHER_ID 0U
#define OTHER_IP "192.168.50.23"
#define MY_ID 1U
#define MY_IP "192.168.50.32"
#define SERVER_PORT_EXCH "8471"
#define SERVER_PORT_CLIENT "8472"
#define SERVER_PORT_CONTROL "8473"
#define SERVER_PORT_DATA "8474"


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

	uint32_t capacity_channels = 1U << 3;


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
	

	// 6.) Submit messages based on user input
	printf("\n\nNow actually ready for orders..!\n\n");


	bool is_done = false;
	char *command;
	char *obj_ref;
	char *obj_data;
	uint32_t num_bytes;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	struct ibv_mr * simulated_obj_mr;
	int command_cnt = 1;
	void * obj_data_saved;
	while (!is_done){
		printf("\n %d). Please input a command. Either order type [BID|OFFER|FUTURE] or exit [EXIT]: ", command_cnt);
		scanf("%ms", &command);
		if (strcmp(command, "BID") == 0){
			printf("\n\n\tPrepare your BID order...\n\t\tPlease input the number of bytes of the object you are searching for: ");
			scanf("%u", &num_bytes);
			if (num_bytes <= 0){
				fprintf(stderr, "Error. Your object needs to have > 0 bytes\n");
				continue;
			}
			printf("\n\t\tPlease input an object reference (i.e. function representation): ");
			scanf("%ms", &obj_ref);
			do_fingerprinting(obj_ref, strlen(obj_ref), fingerprint, FINGERPRINT_TYPE);
			printf("\n\t\t\tFingerprint of reference: ");
			print_hex(fingerprint, FINGERPRINT_NUM_BYTES);
			printf("\n");
			ret = submit_bid(exchanges_client, MY_ID, fingerprint, num_bytes, NULL, NULL);
			if (ret != 0){
				fprintf(stderr, "Error: could not submit bid\n");
				continue;
			}
			printf("\n\t\tSuccessfully submitted BID for object reference: %s\n", obj_ref);
			free(obj_ref);
		}
		else if (strcmp(command, "OFFER") == 0){
                        printf("\n\n\tPrepare your OFFER order...\n\t\tPlease input the object reference (i.e. function representation) you are simulating: ");

                        // 1.) get "simulated" object reference (would typically refer the hash of function you just finished computing)
                        scanf("%ms", &obj_ref);

                        // 2.) Do fingerprinting of object reference
                        do_fingerprinting(obj_ref, strlen(obj_ref), fingerprint, FINGERPRINT_TYPE);
                        printf("\n\t\t\tFingerprint of reference: ");
                        print_hex(fingerprint, FINGERPRINT_NUM_BYTES);
                        printf("\n");

                        // 3.) Get "simulated" data (would typically refer to the contents of the function you just finished computing)
                        printf("\n\t\tPlease input the data corresponding to simulated object ref (i.e. function output): ");
                        scanf("%ms", &obj_data);

                        size_t data_size = strlen(obj_data);
                        obj_data_saved = malloc(data_size);
                        memcpy(obj_data_saved, obj_data, data_size);
                        // 4.) register this memory with ib verbs (which would normally already exist in a registered region)
                        ret = register_virt_memory(data_controller -> data_pd, obj_data_saved, data_size, &simulated_obj_mr);
                        if (ret != 0){
                                fprintf(stderr, "Error: couldn't register simulated object with ib verbs\n");
                                continue;
                        }


                        // 5.) Tell the inventory manager where we have this object with a given fingerprint
                        ret = put_obj_local(inventory, fingerprint, obj_data_saved, data_size, simulated_obj_mr -> lkey);
                        if (ret != 0){
                                fprintf(stderr, "Error: failed to put simulated object in local inventory\n");
                                return -1;
                        }

                        // 6.) Submit offer
                        ret = submit_offer(exchanges_client, MY_ID, fingerprint, NULL, NULL);
                        if (ret != 0){
                                fprintf(stderr, "Error: could not submit offer\n");
                                continue;
                        }
                        printf("\nSuccessfully submitted OFFER for object reference: %s\n", obj_ref);
                        free(obj_ref);
                        // Note can free obj_data because now exists within inventory by using obj_data_saved (clear memory leak but ok for demo)
                        free(obj_data);
                }
		else if (strcmp(command, "FUTURE") == 0){
			printf("\n\n\tPrepare your FUTURE order...\n\t\tPlease input the object reference (i.e. function representation) you are simulating: ");
			
			// 1.) get "simulated" object reference (would typically refer the hash of function you just finished computing)
			scanf("%ms", &obj_ref);
			
			// 2.) Do fingerprinting of object reference
			do_fingerprinting(obj_ref, strlen(obj_ref), fingerprint, FINGERPRINT_TYPE);
			printf("\n\t\t\tFingerprint of reference: ");
			print_hex(fingerprint, FINGERPRINT_NUM_BYTES);
			printf("\n");

			// 3.) Submit offer
			ret = submit_future(exchanges_client, MY_ID, fingerprint, NULL, NULL);
			if (ret != 0){
				fprintf(stderr, "Error: could not submit future\n");
				continue;
			}
			printf("\nSuccessfully submitted FUTURE for object reference: %s\n", obj_ref);
			free(obj_ref);
			// Note: can't free obj_data because now exists within inventory

		}
		else if (strcmp(command, "EXIT") == 0){
			printf("\nThank you for submitting your orders.\nThe program will be kept alive in case others ask for data.\nExiting order inputting terminal, hit control-c to quit program.\n\n");
			is_done = true;
		}
		else{
			fprintf(stderr, "Error. Unrecognized command of: %s\n", command);
		}
		free(command);
		command_cnt += 1;

	}

	keep_alive_and_block(exchanges_client);

	return 0;

}
