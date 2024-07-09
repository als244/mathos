#include "inventory.h"

// floor(log2(index) + 1)
// number of low order 1's bits in table_size bitmask
static const char LogTable512[512] = {
	0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9
};


uint64_t obj_location_hash_func(void * obj_location, uint64_t table_size) {
	Obj_Location * item_casted = (Obj_Location *) obj_location;
	uint8_t * fingerprint = item_casted -> fingerprint;
	uint64_t least_sig_64bits = fingerprint_to_least_sig64(fingerprint, FINGERPRINT_NUM_BYTES);
	// bitmask should be the lower log(2) lower bits of table size.
	// i.e. if table_size = 2^12, we should have a bit mask of 12 1's
	uint64_t bit_mask;
	uint64_t hash_ind;

	// optimization for power's of two table sizes, no need for leading-zero/table lookup or modulus
	// but might hurt due to branch prediction...
	// if (__builtin_popcountll(table_size) == 1){
	// 	bit_mask = table_size - 1;
	// 	hash_ind = least_sig_64bits & bit_mask;
	// 	return hash_ind;
	// }

	int leading_zeros = __builtin_clzll(table_size);
	// 64 bits as table_size type
	// taking ceil of power of 2, then subtracing 1 to get low-order 1's bitmask
	int num_bits = (64 - leading_zeros) + 1;
	bit_mask = (1L << num_bits) - 1;
	hash_ind = (least_sig_64bits & bit_mask) % table_size;
	return hash_ind;
}


// Ref: "https://graphics.stanford.edu/~seander/bithacks.html#ModulusDivisionEasy"
uint64_t obj_location_hash_func_no_builtin(void * obj_location, uint64_t table_size) {
	Obj_Location * item_casted = (Obj_Location *) obj_location;
	uint8_t * fingerprint = item_casted -> fingerprint;
	uint64_t least_sig_64bits = fingerprint_to_least_sig64(fingerprint, FINGERPRINT_NUM_BYTES);
	// bitmask should be the lower log(2) lower bits of table size.
	// i.e. if table_size = 2^12, we should have a bit mask of 12 1's
	uint64_t bit_mask;
	uint64_t hash_ind;
	
	// here we set the log table to be floor(log(ind) + 1), which represents number of bits we should have in bitmask before modulus
	uint64_t num_bits;
	register uint64_t temp;
	if (temp = table_size >> 56){
		num_bits = 56 + LogTable512[temp];
	}
	else if (temp = table_size >> 48) {
		num_bits = 48 + LogTable512[temp];
	}
	else if (temp = table_size >> 40){
		num_bits = 40 + LogTable512[temp];
	}
	else if (temp = table_size >> 32){
		num_bits = 32 + LogTable512[temp];
	}
	else if (temp = table_size >> 24){
		num_bits = 24 + LogTable512[temp];
	}
	else if (temp = table_size >> 16){
		num_bits = 18 + LogTable512[temp];
	}
	else if (temp = table_size >> 8){
		num_bits = 8 + LogTable512[temp];
	}
	else{
		num_bits = LogTable512[temp];
	}
	bit_mask = (1 << num_bits) - 1;
	// now after computing bit_mask the hash_ind may be greater than table_size
	hash_ind = (least_sig_64bits & bit_mask) % table_size;
	
	return hash_ind;
}


int obj_location_cmp(void * obj_location, void * other_item) {
	uint8_t * item_fingerprint = ((Obj_Location *) obj_location) -> fingerprint;
	uint8_t * other_fingerprint = ((Obj_Location *) other_item) -> fingerprint;
	int cmp_res = memcmp(item_fingerprint, other_fingerprint, FINGERPRINT_NUM_BYTES);
	return cmp_res;
}	


Inventory * init_inventory(uint64_t min_objects, uint64_t max_objects) {

	int ret;

	Inventory * inventory = (Inventory *) malloc(sizeof(Inventory));
	if (inventory == NULL){
		fprintf(stderr, "Error: malloc failed to alloc inventory struct\n");
		return NULL;
	}

	inventory -> min_objects = min_objects;
	inventory -> max_objects = max_objects;


	// Can use the fixed-size table functions for connections table
	// SETTING DEFAULT TABLE PARAMS HERE...
	// should really change location / args to config this better
	float load_factor = 0.5f;
	float shrink_factor = 0.1f;

	Hash_Func hash_func_obj_location = &obj_location_hash_func;
	Item_Cmp item_cmp_obj_location = &obj_location_cmp;

	Table * obj_locations = init_table(min_objects, max_objects, load_factor, shrink_factor, hash_func_obj_location, item_cmp_obj_location);
	if (obj_locations == NULL){
		fprintf(stderr, "Error: could not initialize object locations table in inventory\n");
		return NULL;
	}

	inventory -> obj_locations = obj_locations;

	ret = pthread_mutex_init(&(inventory -> inventory_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init inventory lock\n");
		return NULL;
	}

	return inventory;
}


Obj_Location * init_obj_location(uint8_t * fingerprint, void * addr, uint64_t data_bytes, uint32_t lkey, bool is_available){

	int ret;
	
	Obj_Location * obj_location = (Obj_Location *) malloc(sizeof(Obj_Location));
	if (obj_location == NULL){
		fprintf(stderr, "Error: malloc failed to allocate object location\n");
		return NULL;
	}

	memcpy(obj_location -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);
	obj_location -> addr = addr;
	obj_location -> data_bytes = data_bytes;
	obj_location -> lkey = lkey;

	ret = pthread_mutex_init(&(obj_location -> inbound_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init object status lock\n");
		return NULL;
	}

	obj_location -> is_available = is_available;

	ret = pthread_mutex_init(&(obj_location -> outbound_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init object status lock\n");
		return NULL;
	}

	obj_location -> outbound_inflight_cnt = 0;

	return obj_location;

}


/* ONLY DISTINCTION BETWEEN PUT vs. RESERVE IS MARKING THE "IS_AVAILABLE" BIT */

int put_obj_local(Inventory * inventory, uint8_t * fingerprint, void * addr, uint64_t data_bytes, uint32_t lkey) {

	int ret;

	bool is_available = true;
	Obj_Location * obj_location = init_obj_location(fingerprint, addr, data_bytes, lkey, is_available);
	if (obj_location == NULL){
		fprintf(stderr, "Error: could not initialize object location\n");
		return -1;
	}	

	// TODO: CURRENTLY IGNORING DUPLICATE OBJECT LOCATIONS, BUT SHOULD HANDLE THIS

	ret = insert_item_table(inventory -> obj_locations, obj_location);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert object location into table\n");
		return -1;
	}

	return 0;
}


// BIG TODO: INSERT A LOT OF LOGIC HERE TO OBTAIN addr and lkey
//			- Shouldn't be allocating and registering here, but am for now
Obj_Location * reserve_obj_local(Inventory * inventory, uint8_t * fingerprint, uint64_t data_bytes, struct ibv_pd * pd) {

	int ret;

	// 1.) FOR NOW: Allocate space and register with IB-Verbs
	//		- should be sophisticated here with device memories and other pools...
	void * reserved_buffer = calloc(data_bytes, 1);
	if (reserved_buffer == NULL){
		fprintf(stderr, "Error: malloc failed for reserving an object buffer\n");
		return NULL;
	}

	// now need to register with ib_verbs to get mr => lkey needed for posting sends/recvs
	struct ibv_mr * reserved_mr;
	ret = register_virt_memory(pd, reserved_buffer, data_bytes, &reserved_mr);
	if (ret != 0){
		fprintf(stderr, "Error: could not register reserved object buffer mr\n");
		return NULL;
	}

	uint32_t lkey = reserved_mr -> lkey;

	// 2.) Initialize object location
	bool is_available = false;
	Obj_Location * obj_location = init_obj_location(fingerprint, reserved_buffer, data_bytes, lkey, is_available);
	if (obj_location == NULL){
		fprintf(stderr, "Error: could not initialize object location\n");
		return NULL;
	}	

	// TODO: CURRENTLY IGNORING DUPLICATE OBJECT LOCATIONS, BUT SHOULD HANDLE THIS

	// 3.) Insert object location into the table for future lookups
	ret = insert_item_table(inventory -> obj_locations, obj_location);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert object location into table\n");
		free(obj_location);
		return NULL;
	}

	return obj_location;
}


int lookup_obj_location(Inventory * inventory, uint8_t * fingerprint, Obj_Location ** ret_obj_location) {

	Obj_Location target_obj_location;
	memcpy(target_obj_location.fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);

	Obj_Location * found_obj = find_item_table(inventory -> obj_locations, &target_obj_location);
	if (found_obj == NULL){
		fprintf(stderr, "Error: could not find object in inventory\n");
		*ret_obj_location = NULL;
		return -1;
	}

	*ret_obj_location = found_obj;
	return 0;
}


int copy_obj_local(Inventory * inventory, uint8_t * fingerprint, void ** ret_cloned_obj) {

	int ret;

	Obj_Location * obj_location;
	ret = lookup_obj_location(inventory, fingerprint, &obj_location);
	if (ret != 0){
		fprintf(stderr, "Error: could not copy object because not found in inventory\n");
		*ret_cloned_obj = NULL;
		return -1;
	}

	void * obj_addr = obj_location -> addr;
	uint64_t obj_size = obj_location -> data_bytes;

	void * cloned_obj = malloc(obj_size);
	if (cloned_obj == NULL){
		fprintf(stderr, "Error: malloc failed to alloc cloned obj\n");
		*ret_cloned_obj = NULL;
		return -1;
	}

	memcpy(cloned_obj, obj_addr, obj_size);

	*ret_cloned_obj = cloned_obj;

	return 0;

}