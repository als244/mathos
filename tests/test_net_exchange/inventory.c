#include "inventory.h"


void print_fingerprint_match(uint32_t node_id, WorkerType worker_type, int thread_id, uint32_t source_node_id, Inventory_Message * inventory_message){

	Fingerprint_Match * fingerprint_match = (Fingerprint_Match *) &(inventory_message -> message);

	uint8_t * fingerprint = fingerprint_match -> fingerprint;
	uint32_t num_nodes = fingerprint_match -> num_nodes;
	uint32_t * node_ids = fingerprint_match -> node_ids;

	char fingerprint_as_hex_str[2 * FINGERPRINT_NUM_BYTES + 1];

	// within utils.c
	copy_byte_arr_to_hex_str(fingerprint_as_hex_str, FINGERPRINT_NUM_BYTES, fingerprint);

	char node_ids_str_list[num_nodes * 64];

	copy_id_list_to_str(node_ids_str_list, num_nodes, node_ids);
	
	char worker_type_buf[100];

	switch(worker_type){
		case INVENTORY_WORKER:
			strcpy(worker_type_buf, "Inventory Worker");
			break;
		case EXCHANGE_WORKER:
			strcpy(worker_type_buf, "Exchange Worker");
			break;
		case EXCHANGE_CLIENT:
			strcpy(worker_type_buf, "Exchange Client");
			break;
		default:
			strcpy(worker_type_buf, "Unknown Worker Type");
			break;
	}

	printf("[Node %u: %s -- %d] Received FINGERPRINT_MATCH!\n\tSource Exchange: %u\n\tFingerprint: %s\n\tNum Matching Locations: %u\n\tMatching Locations List: %s\n\n",
						node_id, worker_type_buf, thread_id, source_node_id, fingerprint_as_hex_str, num_nodes, node_ids_str_list);
	
	return;
}

void print_transfer_initiate(uint32_t node_id, WorkerType worker_type, int thread_id, uint32_t source_node_id, Inventory_Message * inventory_message){

	printf("[Inventory Worker %d] Received TRANSFER_INITIATE from Exchange #%u.\n\n", thread_id, source_node_id);
	return;
}

void print_transfer_response(uint32_t node_id, WorkerType worker_type, int thread_id, uint32_t source_node_id, Inventory_Message * inventory_message){

	printf("[Inventory Worker %d] Received TRANSFER_RESPONSE from Exchange #%u.\n\n", thread_id, source_node_id);
	return;
}

void print_inventory_q(uint32_t node_id, WorkerType worker_type, int thread_id, uint32_t source_node_id, Inventory_Message * inventory_message){

	printf("[Inventory Worker %d] Received INVENTORY_Q from Exchange #%u.\n\n", thread_id, source_node_id);
	return;
}


// Thread ID = -1, means self exchange client!
void print_inventory_message(uint32_t node_id, WorkerType worker_type, int thread_id, Ctrl_Message * ctrl_message) {

	// assert (ctrl_message -> header).message_class == INVENTORY_CLASS

	uint32_t source_node_id = (ctrl_message -> header).source_node_id;


	Inventory_Message * inventory_message = (Inventory_Message *) (&(ctrl_message -> contents));
	
	InventoryMessageType message_type = inventory_message -> message_type;


	switch(message_type){
		case FINGERPRINT_MATCH:
			print_fingerprint_match(node_id, worker_type, thread_id, source_node_id, inventory_message);
			return;
		case TRANSFER_INITIATE:
			print_transfer_initiate(node_id, worker_type, thread_id, source_node_id, inventory_message);
			return;
		case TRANSFER_RESPONSE:
			print_transfer_response(node_id, worker_type, thread_id, source_node_id, inventory_message);
			return;
		case INVENTORY_Q:
			print_inventory_q(node_id, worker_type, thread_id, source_node_id, inventory_message);
			return;
		default:
			printf("Received UNKNOWN_INVENTORY_MESSAGE_TYPE from node id: %u\n", source_node_id);
			return;
	}
}


void inventory_message_type_to_str(char * buf, InventoryMessageType inventory_message_type) {

	switch(inventory_message_type){
		case FINGERPRINT_MATCH:
			strcpy(buf, "FINGERPRINT_MATCH");
			return;
		case TRANSFER_INITIATE:
			strcpy(buf, "TRANSFER_INITIATE");
			return;
		case TRANSFER_RESPONSE:
			strcpy(buf, "TRANSFER_RESPONSE");
			return;
		case INVENTORY_Q:
			strcpy(buf, "INVENTORY_Q");
			return;
		default:
			strcpy(buf, "UNKNOWN_INVENTORY_MESSAGE_TYPE");
			return;
	}
}