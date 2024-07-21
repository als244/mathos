#include "table.h"

// Simple HashTable implementation
// Using LinearProbing for simplicity / performance

Table * init_table(uint64_t min_size, uint64_t max_size, float load_factor, float shrink_factor, Hash_Func hash_func, Item_Cmp item_cmp) {

	int ret;

	Table * tab = (Table *) malloc(sizeof(Table));

	// set number of elements to size passed in
	tab -> size = min_size;
	tab -> min_size = min_size;
	tab -> max_size = max_size;
	tab -> load_factor = load_factor;
	tab -> shrink_factor = shrink_factor;
	// initially 0 items in table
	tab -> cnt = 0;

	// init table with no references to items (all Null)
	void ** table = (void **) malloc(min_size * sizeof(void *));
	for (uint64_t i = 0; i < min_size; i++){
		table[i] = NULL;
	}
	tab -> table = table;

	ret = pthread_mutex_init(&(tab -> size_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init table lock\n");
		return NULL;
	}


	ret = pthread_mutex_init(&(tab -> cnt_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init cnt lock\n");
		return NULL;
	}

	pthread_mutex_t * slot_locks = (pthread_mutex_t *) malloc(min_size * sizeof(pthread_mutex_t));
	for (uint64_t i = 0; i < min_size; i++){
		ret = pthread_mutex_init(&(slot_locks[i]), NULL);
		if (ret != 0){
			fprintf(stderr, "Error: could not slock lock\n");
			return NULL;
		}
	}

	tab -> slot_locks = slot_locks;

	tab -> hash_func = hash_func;
	tab -> item_cmp = item_cmp;

	return tab;
}

void destroy_table(Table * table){
	fprintf(stderr, "Destroy Table: Unimplemented Error\n");
}

uint64_t get_count(Table * table){
	pthread_mutex_lock(&(table -> cnt_lock));
	uint64_t count = table -> cnt;
	pthread_mutex_unlock(&(table -> cnt_lock));
	return count;
}


/* USING A PASSED IN HASH FUNCTION INSTEAD */

// with prototype uint64_t hash_func(void * item, uint64_t table_size)

// // Mixing hash function
// // Taken from "https://github.com/shenwei356/uint64-hash-bench?tab=readme-ov-file"
// // Credit: Thomas Wang
// uint64_t hash_func(uint64_t key){
// 	key = (key << 21) - key - 1;
// 	key = key ^ (key >> 24);
// 	key = (key + (key << 3)) + (key << 8);
// 	key = key ^ (key >> 14);
// 	key = (key + (key << 2)) + (key << 4);
// 	key = key ^ (key >> 28);
// 	key = key + (key << 31);
// 	return key;
// }


// assert(holding tab_lock), 
// everyone else (insert/remove/find) is locked out so don't need to care about locks
int resize_table(Table * table, uint64_t new_size) {

	if (table == NULL){
		fprintf(stderr, "Error in resize_table, item table is null\n");
		return -1;
	}

	void ** old_table = table -> table;
	pthread_mutex_t * old_slot_locks = table -> slot_locks;
	uint64_t old_size = table -> size;

	// create new table that will replace old one
	// ensure new table is initialized to all null, otherwise placement errors
	void ** new_table = (void **) calloc(new_size, sizeof(void *));
	pthread_mutex_t * new_slot_locks = malloc(new_size * sizeof(pthread_mutex_t));
	for (uint64_t i = 0; i < new_size; i++){
		pthread_mutex_init(&(new_slot_locks[i]), NULL);
	}

	// re-hash all the items into new table
	uint64_t hash_ind, table_ind;
	for (uint64_t i = 0; i < old_size; i++){
		//pthread_mutex_lock(&(old_slot_locks[i]));
		if (old_table[i] == NULL){
			//pthread_mutex_unlock(&(old_slot_locks[i]));
			continue;
		}
		hash_ind = (table -> hash_func)(old_table[i], new_size);
		// do open addressing insert
		for (uint64_t j = hash_ind; j < hash_ind + new_size; j++){
			table_ind = j % new_size;
			//pthread_mutex_lock(&(new_slot_locks[table_ind]));

			// found empty slot to insert
			if (new_table[table_ind] == NULL) {
				new_table[table_ind] = old_table[i];
				// inserted new item so break from inner loop
				//pthread_mutex_unlock(&(new_slot_locks[table_ind]));
				break;
			}
			else{
				//pthread_mutex_unlock(&(new_slot_locks[table_ind]));
			}
		}
		//pthread_mutex_unlock(&(old_slot_locks[i]));
	}

	// only need to modify the table field and size field
	table -> table = new_table;
	table -> slot_locks = new_slot_locks;
	table -> size = new_size;

	for (uint64_t i = 0; i < old_size; i++){
		pthread_mutex_destroy(&(old_slot_locks[i]));
	}

	free(old_slot_locks);

	// free the old table and reset to new one
	free(old_table);

	return 0;
}


// return -1 upon error
int insert_item_table(Table * table, void * item) {

	if (table == NULL){
		fprintf(stderr, "Error in insert_item, item table is null\n");
		return -1;
	}

	if (item == NULL){
		fprintf(stderr, "Error in insert_item, item is null\n");
		return -1;
	}

	int ret;


	// make sure types are correct when multiplying uint64_t by float
	pthread_mutex_lock(&(table -> size_lock));
	uint64_t size = table -> size;
	
	pthread_mutex_lock(&(table -> cnt_lock));
	uint64_t cnt = table -> cnt;
	pthread_mutex_unlock(&(table -> cnt_lock));

	
	// should only happen when cnt = max_size
	uint64_t max_size = table -> max_size;
	if ((cnt + 1) > max_size){
		printf("Greater than max size\n");
		pthread_mutex_unlock(&(table -> size_lock));
		return -1;
	} 
	// check if we exceed load and are below max cap
	// if so, grow
	float load_factor = table -> load_factor;
	// make sure types are correct when multiplying uint64_t by float
	uint64_t load_cap = (uint64_t) (size * load_factor);
	// optimisitically assume that we would find the item 
	// (without actually changing cnt)
	if ((size < max_size) && ((cnt + 1) > load_cap)){
		size = (uint64_t) (size * round(1 / load_factor));
		if (size > max_size){
			size = max_size;
		}
		// updates table -> size within function
		ret = resize_table(table, size);
		// check if there was an error growing table
		if (ret == -1){
			printf("Resize failed\n");
			pthread_mutex_unlock(&(table -> size_lock));
			return -1;
		}
	}
	pthread_mutex_unlock(&(table -> size_lock));

	
	uint64_t hash_ind = (table -> hash_func)(item, size);
	uint64_t table_ind;
	// doing the Linear Probing
	// worst case O(size) insert time
	void ** tab = table -> table;
	pthread_mutex_t * slot_locks = table -> slot_locks;
	for (uint64_t i = hash_ind; i < hash_ind + size; i++){
		table_ind = i % size;
		pthread_mutex_lock(&(slot_locks[table_ind]));
		// there is a free slot to insert
		if (tab[table_ind] == NULL) {
			tab[table_ind] = item;
			pthread_mutex_lock(&(table -> cnt_lock));
			table -> cnt += 1;
			pthread_mutex_unlock(&(table -> cnt_lock));
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			break;
		}
		else{
			pthread_mutex_unlock(&(slot_locks[table_ind]));
		}
	}

	return 0;
}

// ASSERT(table will not grow/shrink!)
int insert_item_get_index_table(Table * table, void * item, uint64_t * ret_index) {

	if (table == NULL){
		fprintf(stderr, "Error in insert_item, item table is null\n");
		return -1;
	}

	if (item == NULL){
		fprintf(stderr, "Error in insert_item, item is null\n");
		return -1;
	}

	// might want to assert min_size == max_size

	uint64_t size = table -> size;
	uint64_t hash_ind = (table -> hash_func)(item, size);
	uint64_t table_ind;
	// doing the Linear Probing
	// worst case O(size) insert time
	void ** tab = table -> table;
	pthread_mutex_t * slot_locks = table -> slot_locks;
	for (uint64_t i = hash_ind; i < hash_ind + size; i++){
		table_ind = i % size;
		pthread_mutex_lock(&(slot_locks[table_ind]));
		// there is a free slot to insert
		if (tab[table_ind] == NULL) {
			tab[table_ind] = item;
			*ret_index = table_ind;
			pthread_mutex_lock(&(table -> cnt_lock));
			table -> cnt += 1;
			pthread_mutex_unlock(&(table -> cnt_lock));
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			return 0;
		}
		else{
			pthread_mutex_unlock(&(slot_locks[table_ind]));
		}
	}

	return -1;
}


void * find_item_table(Table * table, void * item){

	if (table == NULL){
		fprintf(stderr, "Error in find_item, item table is null\n");
		return NULL;
	}
	
	// for simplicity we are saying that find_item needs the table lock
	// (i.e. when there is an on-going find, no inserts/removes can happen because they might trigger a resize)
	pthread_mutex_lock(&(table -> size_lock));
	uint64_t size = table -> size;
	uint64_t hash_ind = (table -> hash_func)(item, size);
	void ** tab = table -> table;
	void * found_item;
	pthread_mutex_t * slot_locks = table -> slot_locks;
	uint64_t table_ind;
	// do linear scan
	for (uint64_t i = hash_ind; i < hash_ind + size; i++){
		table_ind = i % size;
		pthread_mutex_lock(&(slot_locks[table_ind]));
		if ((tab[table_ind] != NULL) && ((table -> item_cmp)(item, tab[table_ind]) == 0)){
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			found_item = tab[table_ind];
			pthread_mutex_unlock(&(table -> size_lock));
			return found_item;
		}
		else{
			pthread_mutex_unlock(&(slot_locks[table_ind]));
		}
	}
	pthread_mutex_unlock(&(table -> size_lock));
	// didn't find item
	return NULL;
}

// ASSERT(no growing or shrinking)!
int find_item_index_table(Table * table, void * item, uint64_t * ret_index) {

	if (table == NULL){
		fprintf(stderr, "Error in find_item, item table is null\n");
		return -1;
	}

	// might want to assert min_size == max_size
	
	// Assuming no growing or shrinking table calling this, so we don't need size lock
	uint64_t size = table -> size;
	uint64_t hash_ind = (table -> hash_func)(item, size);
	void ** tab = table -> table;
	pthread_mutex_t * slot_locks = table -> slot_locks;
	uint64_t table_ind;
	// do linear scan
	for (uint64_t i = hash_ind; i < hash_ind + size; i++){
		table_ind = i % size;
		pthread_mutex_lock(&(slot_locks[table_ind]));
		if ((tab[table_ind] != NULL) && ((table -> item_cmp)(item, tab[table_ind]) == 0)){
			*ret_index = table_ind;
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			return 0;
		}
		else{
			pthread_mutex_unlock(&(slot_locks[table_ind]));
		}
	}
	// didn't find item
	return -1;
}

// ASSERT(no growing or shrinking)!
int remove_random_item(Table * table, void ** ret_item, uint64_t * ret_index) {
	if (table == NULL){
		fprintf(stderr, "Error in find_item, item table is null\n");
		return -1;
	}

	// might want to assert min_size == max_size

	// Assuming no growing or shrinking table calling this, so we don't need size lock
	uint64_t size = table -> size;
	void ** tab = table -> table;
	pthread_mutex_t * slot_locks = table -> slot_locks;
	uint64_t table_ind;

	// do linear scan starting at random location
	uint64_t rand_start = rand() % size;

	for (uint64_t i = rand_start; i < rand_start + size; i++){
		table_ind = i % size;
		pthread_mutex_lock(&(slot_locks[table_ind]));
		if (tab[table_ind] != NULL){
			*ret_item = tab[table_ind];
			*ret_index = table_ind;
			tab[table_ind] = NULL;
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			return 0;
		}
		else{
			pthread_mutex_unlock(&(slot_locks[table_ind]));
		}
	}
	*ret_item = NULL;
	// didn't find item
	return -1;
}

// ASSERT(no growing or shrinking)!
int remove_item_at_index_table(Table * table, void * item, uint64_t index){

	if (table == NULL){
		fprintf(stderr, "Error: in remove_item, item table is null\n");
		return -1;
	}

	// might want to assert min_size == max_size
	void ** tab = table -> table;
	uint64_t size = table -> size;

	if (index >= size){
		fprintf(stderr, "Error: trying to remove at an index > table size\n");
		return -1;
	}

	pthread_mutex_t * slot_locks = table -> slot_locks;
	
	// acquire lock and confirm correct item
	pthread_mutex_lock(&(table -> cnt_lock));
	pthread_mutex_lock(&(slot_locks[index]));
	if ((tab[index] == NULL) || ((table -> item_cmp)(item, tab[index]) != 0)){
		fprintf(stderr, "Error: trying to remove at an index that doesn't contain correct item\n");
		pthread_mutex_unlock(&(slot_locks[index]));
		pthread_mutex_unlock(&(table -> cnt_lock));
		return -1;
	}
	tab[index] = NULL;
	table -> cnt -= 1;
	pthread_mutex_unlock(&(slot_locks[index]));
	pthread_mutex_unlock(&(table -> cnt_lock));

	return 0;
}

// remove from table and return pointer to item (can be used later for destroying)
void * remove_item_table(Table * table, void * item) {
	
	if (table == NULL){
		fprintf(stderr, "Error in remove_item, item table is null\n");
		return NULL;
	}

	int ret;


	float shrink_factor = table -> shrink_factor;
	uint64_t min_size = table -> min_size;

	// check if we should shrink if we would have removed item
	// make sure types are correct when multiplying uint64_t by float
	pthread_mutex_lock(&(table -> size_lock));
	uint64_t size = table -> size;

	pthread_mutex_lock(&(table -> cnt_lock));
	uint64_t cnt = table -> cnt;
	pthread_mutex_unlock(&(table -> cnt_lock));

	// if there are no items then we can shortcut everything
	if (cnt == 0){
		pthread_mutex_unlock(&(table -> size_lock));
		return NULL;
	}
	uint64_t shrink_cap = (uint64_t) (size * shrink_factor);
	// optimisitically assume that we would find the item 
	// (without actually changing cnt)
	if ((size > min_size) && ((cnt - 1) < shrink_cap)) {
		size = (uint64_t) (size * (1 - shrink_factor));
		if (size < min_size){
			size = min_size;
		}
		ret = resize_table(table, size);
		// check if there was an error growing table
		if (ret == -1){
			pthread_mutex_unlock(&(table -> size_lock));
			return NULL;
		}
	}
	pthread_mutex_unlock(&(table -> size_lock));

		
	uint64_t hash_ind = (table -> hash_func)(item, size);

	// orig set to null in case item not in table 
	// in which case we want to return NULL
	void * ret_item = NULL;
	uint64_t table_ind;
	// do linear scan
	void ** tab = table -> table;
	pthread_mutex_t * slot_locks = table -> slot_locks;
	for (uint64_t i = hash_ind; i < hash_ind + size; i++){
		table_ind = i % size;
		// check if we found item, remember its contents and make room in table
		// use function pointer to check for item key
		pthread_mutex_lock(&(slot_locks[table_ind]));
		if ((tab[table_ind] != NULL) && ((table -> item_cmp)(item, tab[table_ind]) == 0)){
			// set item to be the item removed so we can return it
			ret_item = tab[table_ind];
			// remove reference from table
			tab[table_ind] = NULL;
			pthread_mutex_lock(&(table -> cnt_lock));
			table -> cnt -= 1;
			pthread_mutex_unlock(&(table -> cnt_lock));
			// found item so break
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			break;
		}
		else{
			pthread_mutex_unlock(&(slot_locks[table_ind]));
		}
		
	}

	// if found is pointer to item, otherwise null
	return ret_item;
}
