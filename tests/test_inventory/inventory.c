#include "inventory.h"

int inventory_item_cmp(void * inventory_item, void * other_item) {
	uint8_t * item_fingerprint = ((Object *) inventory_item) -> fingerprint;
	uint8_t * other_fingerprint = ((Object *) other_item) -> fingerprint;
	int cmp_res = memcmp(item_fingerprint, other_fingerprint, FINGERPRINT_NUM_BYTES);
	return cmp_res;
}	


uint64_t inventory_hash_func(void * inventory_item, uint64_t table_size) {
	Object * item_casted = (Object *) inventory_item;
	unsigned char * fingerprint = item_casted -> fingerprint;
	uint64_t least_sig_64bits = fingerprint_to_least_sig64(fingerprint, FINGERPRINT_NUM_BYTES);
	return least_sig_64bits % table_size;
}


Inventory * init_inventory(Memory * memory) {

	Inventory * inventory = malloc(sizeof(Inventory));
	if (!inventory){
		fprintf(stderr, "Error: malloc() failed for inventory\n");
		return NULL;
	}

	inventory -> memory = memory;
	inventory -> num_pools = memory -> num_devices + 1;

	Hash_Func hash_func = &inventory_hash_func;
	Item_Cmp item_cmp = &inventory_item_cmp;

	inventory -> object_table = init_table(INVENTORY_MIN_FINGERPRINTS_TABLE_ITEMS, INVENTORY_MAX_FINGERPRINTS_TABLE_ITEMS, 
											INVENTORY_TABLES_LOAD_FACTOR, INVENTORY_TABLES_SHRINK_FACTOR, hash_func, item_cmp);

	if (!(inventory -> object_table)){
		fprintf(stderr, "Error: init_table failed for inventory object table\n");
		return NULL;
	}

	return inventory;
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
int reserve_object(Inventory * inventory, uint8_t * fingerprint, int pool_id, uint64_t size_bytes, int num_backup_pools, int * backup_pool_ids, int mem_client_id, Obj_Location ** ret_obj_location) {

	Object target_obj;
	memcpy(target_obj.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
	Object * obj = find_item_table(inventory -> object_table, &target_obj);

	Obj_Location * locations;

	// assume we will fail
	*ret_obj_location = NULL;

	if (!obj){

		obj = malloc(sizeof(Object));
		if (!obj){
			fprintf(stderr, "Error: malloc() failed for new object\n");
			return -1;
		}

		memcpy(obj -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);

		obj -> size_bytes = size_bytes;
		locations = malloc(inventory -> num_pools * sizeof(Obj_Location));
		if (!(obj -> locations)){
			fprintf(stderr, "Error: malloc() failed for new object locations\n");
			return -1;
		}

		for (int i = 0; i < inventory -> num_pools; i++){
			locations[i].obj = obj;
			locations[i].pool_id = i;
			if (i == inventory -> num_pools - 1){
				locations[i].pool_id = -1;
			}
			locations[i].buffer = NULL;
			locations[i].reservation = NULL;
			locations[i].is_available = false;
			locations[i].outbound_inflight_cnt = 0;
			pthread_mutex_init(&(locations[i].inbound_lock), NULL);
			pthread_mutex_init(&(locations[i].outbound_lock), NULL);
		}

		obj -> locations = locations;
		obj -> num_reserved_locations = 0;

		// insert the object we just created

		int ins_ret = insert_item_table(inventory -> object_table, obj);

		if (ins_ret < 0){
			fprintf(stderr, "Error: unable to insert object into inventory table\n");
			return -1;
		}

	}
	else{

		// check if already reserved on the identified pool
		locations = obj -> locations;

		// already exists on that pool

		if (pool_id == -1){
			if (locations[inventory -> num_pools - 1].buffer){
				*ret_obj_location = &(locations[inventory -> num_pools - 1]);
				return 0;
			}
		}
		else{
			if (locations[pool_id].buffer){
				*ret_obj_location = &(locations[pool_id]);
				return 0;
			}
		}

		// otherwise make a new mem reservation
	}

	// need to make a new memory reservation

	Mem_Reservation * new_mem_reservation = malloc(sizeof(Mem_Reservation));
	if (!new_mem_reservation){
		fprintf(stderr, "Error: malloc() failed for new memory reservation\n");
		return -1;
	}

	// allocate on device 0 and wanting chunk_size bytes;
	new_mem_reservation -> mem_client_id = mem_client_id;
	new_mem_reservation -> pool_id = pool_id;
	new_mem_reservation -> size_bytes = size_bytes; 
	
	int true_num_backup_pools = 0;
	int backup_pool_ind;
	// ensure that we don't try reserving on a backup pool already holding this object
	for (int i = 0; i < num_backup_pools; i++){
		backup_pool_ind = backup_pool_ids[i];
		if (backup_pool_ind == -1){
			backup_pool_ind = inventory -> num_pools - 1;
		}
		if (!(locations[backup_pool_ind].buffer)){
			(new_mem_reservation -> backup_pool_ids)[true_num_backup_pools] = backup_pool_ids[i];
			true_num_backup_pools++;
		}
	}
	new_mem_reservation -> num_backup_pools = true_num_backup_pools;


	// in case we want to track memory timestamps
	Mem_Op_Timestamps mem_op_timestamps;

	void * new_buffer = reserve_memory(inventory -> memory, new_mem_reservation, &mem_op_timestamps);
	if (!new_buffer){
		fprintf(stderr, "Error: failed to reserve memory on pool id %d of size %lu\n", 
					new_mem_reservation -> pool_id, new_mem_reservation -> size_bytes);
		return -1;
	}

	// modify object location

	int fulfilled_pool_id = new_mem_reservation -> fulfilled_pool_id;

	if (fulfilled_pool_id == -1){
		fulfilled_pool_id = inventory -> num_pools - 1;
	}

	locations[fulfilled_pool_id].buffer = new_buffer;
	locations[fulfilled_pool_id].reservation = new_mem_reservation;

	obj -> num_reserved_locations += 1;


	*ret_obj_location = &(locations[fulfilled_pool_id]);

	return 0;
}


// Responsbile for checking if fingerprint exists in fingerprint table and exists at that objects's locations(pool_id). Otherwise error
// Once object at location is found, call release_memory upon obj_location -> reservation
// Remove obj_location from object -> locations and free obj_location struct. 
// If object -> locations now is empty, remove object from inventory -> fingerprints and free object struct
int release_object(Inventory * inventory, uint8_t * fingerprint, Obj_Location * obj_location, int mem_client_id) {

	Object target_obj;
	memcpy(target_obj.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
	Object * table_obj = find_item_table(inventory -> object_table, &target_obj);

	if (!table_obj){
		fprintf(stderr, "Error: unable to find object in table with specified fingerprint upon release_object\n");
		return -1;
	}

	// TODO: check to ensure we can acquire locks and outbound inflight cnt == 0

	// in case we want to track memory timestamps
	Mem_Op_Timestamps mem_op_timestamps;

	// in case the client is a different than who reserved, update this
	obj_location -> reservation -> mem_client_id = mem_client_id;
	release_memory(inventory -> memory, obj_location -> reservation, &mem_op_timestamps);


	obj_location -> buffer = NULL;
	free(obj_location -> reservation);
	pthread_mutex_destroy(&(obj_location -> inbound_lock));
	pthread_mutex_destroy(&(obj_location -> outbound_lock));
	
	table_obj -> num_reserved_locations -= 1;

	if (table_obj -> num_reserved_locations == 0){
		// remove from table and free object
		remove_item_table(inventory -> object_table, table_obj);
		free(table_obj -> locations);
		free(table_obj);
	}

	return 0;
}



// Release object from all pools, and remove from inventory -> fingerprints and free object struct
int destroy_object(Inventory * inventory, uint8_t * fingerprint, int mem_client_id) {

	Object target_obj;
	memcpy(target_obj.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
	Object * table_obj = find_item_table(inventory -> object_table, &target_obj);

	if (!table_obj){
		return 0;
	}

	Obj_Location * locations = table_obj -> locations;

	Mem_Op_Timestamps mem_op_timestamps;

	for (int i = 0; i < inventory -> num_pools; i++){
		// TODO: check to ensure we can acquire locks and outbound inflight cnt == 0
		if (locations[i].buffer){
			(locations[i].reservation) -> mem_client_id = mem_client_id;
			release_memory(inventory -> memory, locations[i].reservation, &mem_op_timestamps);
			free(locations[i].reservation);
			pthread_mutex_destroy(&(locations[i].inbound_lock));
			pthread_mutex_destroy(&(locations[i].outbound_lock));
			table_obj -> num_reserved_locations -= 1;
		}
	}

	// assert table_obj -> num_reserved_locations == 0
	// remove from table and free object
	remove_item_table(inventory -> object_table, table_obj);
	free(table_obj -> locations);
	free(table_obj);

	return 0;
}



// returns the object within fingerprint table
int lookup_object(Inventory * inventory, uint8_t * fingerprint, Object ** ret_object) {

	Object target_obj;
	memcpy(target_obj.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
	Object * table_obj = find_item_table(inventory -> object_table, &target_obj);

	*ret_object = table_obj;

	if (!table_obj){
		return -1;
	}

	return 0;
}

















/* BELOW ARE HELPER FUNCTIONS FOR PRINTING */




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