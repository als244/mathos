#include "inventory.h"



Inventory * init_inventory(int num_compute_pools, Mempool ** compute_pools, uint64_t min_fingerprints, uint64_t max_fingerprints) {

	fprintf(stderr, "Unimplmented error: init_inventory\n");
	return NULL;

}



// THE MAIN FUNCTION THAT IS EXPOSED

int do_inventory_function(Inventory * inventory, Ctrl_Message * ctrl_message, uint32_t * ret_num_ctrl_messages, Ctrl_Message ** ret_ctrl_messages) {

	fprintf(stderr, "Unimplmented error: do_inventory_function\n");
	return -1;

}	



// THE FUNCTIONS THAT DO THE CORE WORK

// returns 0 upon success, otherwise error

// Responsible for first checking if fingerprint is in inventory -> fingerprints. If not, allocate object and insert
// Allocates an object location and populates it with the mem_reservation returned from reserve_memory
// Inserts the object location into object -> locations
// Populates ret_obj_location
int reserve_object(Inventory * inventory, int pool_id, uint8_t * fingerprint, uint64_t size_bytes, Obj_Location * ret_obj_location){

	fprintf(stderr, "Unimplmented error: reserve_object\n");
	return -1;

}


// Responsbile for checking if fingerprint exists in fingerprint table and exists at that objects's locations(pool_id). Otherwise error
// Once object at location is found, call release_memory upon obj_location -> reservation
// Remove obj_location from object -> locations and free obj_location struct. 
// If object -> locations now is empty, remove object from inventory -> fingerprints and free object struct
int release_object(Inventory * inventory, int pool_id, uint8_t * fingerprint) {

	fprintf(stderr, "Unimplmented error: release_object\n");
	return -1;

}



// Release object from all pools, and remove from inventory -> fingerprints and free object struct
int destroy_object(Inventory * inventory, uint8_t * fingerprint) {

	fprintf(stderr, "Unimplmented error: destroy_object\n");
	return -1;

}



// returns the object within fingerprint table
int lookup_object(Inventory * inventory, uint8_t * fingerprint, Object * ret_object) {

	fprintf(stderr, "Unimplmented error: lookup_object\n");
	return -1;

}

















/* BELOW ARE HELPER FUNCTIONS FOR PRINTING */




void print_fingerprint_match(WorkerType worker_type, int thread_id, uint32_t source_node_id, Inventory_Message * inventory_message){

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

	printf("[%s %d] Received FINGERPRINT_MATCH!\n\tSource Exchange: %u\n\tFingerprint: %s\n\tNum Matching Locations: %u\n\tMatching Locations List: %s\n\n",
						worker_type_buf, thread_id, source_node_id, fingerprint_as_hex_str, num_nodes, node_ids_str_list);
	
	return;
}

void print_transfer_initiate(WorkerType worker_type, int thread_id, uint32_t source_node_id, Inventory_Message * inventory_message){

	printf("[Inventory Worker %d] Received TRANSFER_INITIATE from Exchange #%u.\n\n", thread_id, source_node_id);
	return;
}

void print_transfer_response(WorkerType worker_type, int thread_id, uint32_t source_node_id, Inventory_Message * inventory_message){

	printf("[Inventory Worker %d] Received TRANSFER_RESPONSE from Exchange #%u.\n\n", thread_id, source_node_id);
	return;
}

void print_inventory_q(WorkerType worker_type, int thread_id, uint32_t source_node_id, Inventory_Message * inventory_message){

	printf("[Inventory Worker %d] Received INVENTORY_Q from Exchange #%u.\n\n", thread_id, source_node_id);
	return;
}


// Thread ID = -1, means self exchange client!
void print_inventory_message(WorkerType worker_type, int thread_id, Ctrl_Message * ctrl_message) {

	// assert (ctrl_message -> header).message_class == INVENTORY_CLASS

	uint32_t source_node_id = (ctrl_message -> header).source_node_id;


	Inventory_Message * inventory_message = (Inventory_Message *) (&(ctrl_message -> contents));
	
	InventoryMessageType message_type = inventory_message -> message_type;


	switch(message_type){
		case FINGERPRINT_MATCH:
			print_fingerprint_match(worker_type, thread_id, source_node_id, inventory_message);
			return;
		case TRANSFER_INITIATE:
			print_transfer_initiate(worker_type, thread_id, source_node_id, inventory_message);
			return;
		case TRANSFER_RESPONSE:
			print_transfer_response(worker_type, thread_id, source_node_id, inventory_message);
			return;
		case INVENTORY_Q:
			print_inventory_q(worker_type, thread_id, source_node_id, inventory_message);
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