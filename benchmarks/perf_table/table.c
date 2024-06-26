#include "table.h"

// Simple HashTable implementation
// Using LinearProbing for simplicity / performance

Table * init_table(uint64_t min_size, uint64_t max_size, float load_factor, float shrink_factor, Hash_Func hash_func, Item_Cmp item_cmp) {

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

	// TODO:
	//	- look into mutex attributes (not sure if NULL is what we want)
	pthread_mutex_init(&(tab -> table_lock), NULL);

	tab -> hash_func = hash_func;
	tab -> item_cmp = item_cmp;

	return tab;
}

void destroy_table(Table * table){
	fprintf(stderr, "Destroy Table: Unimplemented Error\n");
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

int resize_table(Table * table, uint64_t new_size) {

	if (table == NULL){
		fprintf(stderr, "Error in resize_table, item table is null\n");
		return -1;
	}

	// ensure that new size can fit all items
	uint64_t cnt = table -> cnt;
	if (cnt > new_size){
		return -1;
	}

	void ** old_table = table -> table;
	uint64_t old_size = table -> size;

	// create new table that will replace old one
	// ensure new table is initialized to all null
	void ** new_table = (void **) calloc(new_size, sizeof(void *));

	// re-hash all the items into new table
	uint64_t hash_ind, table_ind;
	bool is_inserted;
	uint64_t reinsert_cnt = 0;
	for (uint64_t i = 0; i < old_size; i++){
		if (old_table[i] == NULL){
			continue;
		}
		hash_ind = (table -> hash_func)(old_table[i], new_size);
		is_inserted = false;
		// do open addressing insert
		for (table_ind = hash_ind; table_ind < new_size; table_ind++){
			// found empty slot to insert
			if (new_table[table_ind] == NULL) {
				new_table[table_ind] = old_table[i];
				is_inserted = true;
				// inserted new item so break from inner loop
				break;
			}
		}
		if (!is_inserted){
			for (table_ind = 0; table_ind < hash_ind; table_ind++){
				// found empty slot to insert
				if (new_table[table_ind] == NULL) {
					new_table[table_ind] = old_table[i];
					is_inserted = true;
					// inserted new item so break from inner loop
					break;
				}
			}
		}
		// for sparsely populated hash tables we want to break early when done
		reinsert_cnt += 1;
		if (reinsert_cnt == cnt){
			break;
		}
	}

	// only need to modify the table field and size field
	table -> table = new_table;
	table -> size = new_size;

	// free the old table and reset to new one
	free(old_table);
	
	return 0;
}


// return -1 upon error
int insert_item(Table * table, void * item) {

	if (table == NULL){
		fprintf(stderr, "Error in insert_item, item table is null\n");
		return -1;
	}

	if (item == NULL){
		fprintf(stderr, "Error in insert_item, item is null\n");
		return -1;
	}

	int ret;
	uint64_t size = table -> size;
	uint64_t cnt = table -> cnt;
	

	// should only happen when cnt = max_size
	if (cnt == size){
		return -1;
	}

	uint64_t hash_ind = (table -> hash_func)(item, size);
	;
	// doing the Linear Probing
	// worst case O(size) insert time
	void ** tab = table -> table;

	// seperating wrap around to avoid modulus operation
	bool is_inserted = false;
	// start looking until the end of the table
	for (uint64_t table_ind = hash_ind; table_ind < size; table_ind++){
		// there is a free slot to insert
		if (tab[table_ind] == NULL){
			tab[table_ind] = item;
			is_inserted = true;
			break;
		}
	}
	
	if (!is_inserted){
		// now look from the beginning
		for (uint64_t table_ind = 0; table_ind < hash_ind; table_ind++){
			// there is a free slot to insert
			if (tab[table_ind] == NULL){
				tab[table_ind] = item;
				is_inserted = true;
				break;
			}
		}
	}

	cnt += 1;
	table -> cnt = cnt;

	// check if we exceed load and are below max cap
	// if so, grow
	float load_factor = table -> load_factor;
	uint64_t max_size = table -> max_size;
	// make sure types are correct when multiplying uint64_t by float
	uint64_t load_cap = (uint64_t) (size * load_factor);
	if ((size < max_size) && (cnt > load_cap)){
		size = (uint64_t) (size * round(1 / load_factor));
		if (size > max_size){
			size = max_size;
		}
		ret = resize_table(table, size);
		// check if there was an error growing table
		if (ret == -1){
			return -1;
		}
	}
	return 0;
}


void * find_item(Table * table, void * item){

	if (table == NULL){
		fprintf(stderr, "Error in find_item, item table is null\n");
		return NULL;
	}

	uint64_t size = table -> size;
	uint64_t hash_ind = (table -> hash_func)(item, size);
	void ** tab = table -> table;
	uint64_t table_ind;
	// do linear scan
	// seperating wrap around to avoid modulus operation
	for (uint64_t table_ind = hash_ind; table_ind < size; table_ind++){
		// check if we found item
		// use function pointer to check for item key
		if ((tab[table_ind] != NULL) && ((table -> item_cmp)(item, tab[table_ind]) == 0)){
			return tab[table_ind];
		}
	}
	for (uint64_t table_ind = 0; table_ind < hash_ind; table_ind++){
		// check if we found item
		// use function pointer to check for item key
		if ((tab[table_ind] != NULL) && ((table -> item_cmp)(item, tab[table_ind]) == 0)){
			return tab[table_ind];
		}
	}
	// didn't find item
	return NULL;
}

// remove from table and return pointer to item (can be used later for destroying)
void * remove_item(Table * table, void * item) {
	
	if (table == NULL){
		fprintf(stderr, "Error in remove_item, item table is null\n");
		return NULL;
	}

	int ret;

	uint64_t size = table -> size;
	uint64_t cnt = table -> cnt;
	
	uint64_t hash_ind = (table -> hash_func)(item, size);

	// orig set to null in case item not in table 
	// in which case we want to return NULL
	void * ret_item = NULL;
	// do linear scan
	void ** tab = table -> table;
	
	bool is_removed = false;
	for (uint64_t table_ind = hash_ind; table_ind < size; table_ind++){
		// check if we found item, remember its contents and make room in table
		// use function pointer to check for item key
		if ((tab[table_ind] != NULL) &&  ((table -> item_cmp)(item, tab[table_ind]) == 0)){
			// set item to be the item removed so we can return it
			ret_item = tab[table_ind];
			// remove reference from table
			tab[table_ind] = NULL;
			cnt -= 1;
			table -> cnt = cnt;
			is_removed = true;
			// found item so break
			break;
		}
	}

	if (!is_removed){
		for (uint64_t table_ind = 0; table_ind < hash_ind; table_ind++){
			// check if we found item, remember its contents and make room in table
			// use function pointer to check for item key
			if ((tab[table_ind] != NULL) &&  ((table -> item_cmp)(item, tab[table_ind]) == 0)){
				// set item to be the item removed so we can return it
				ret_item = tab[table_ind];
				// remove reference from table
				tab[table_ind] = NULL;
				cnt -= 1;
				table -> cnt = cnt;
				is_removed = true;
				// found item so break
				break;
			}
		}
	}

	// check if we should shrink
	float shrink_factor = table -> shrink_factor;
	uint64_t min_size = table -> min_size;
	// make sure types are correct when multiplying uint64_t by float
	uint64_t shrink_cap = (uint64_t) (size * shrink_factor);
	if ((size > min_size) && (cnt < shrink_cap)) {
		size = (uint64_t) (size * (1 - shrink_factor));
		if (size < min_size){
			size = min_size;
		}
		ret = resize_table(table, size);
		// check if there was an error growing table
		if (ret == -1){
			return NULL;
		}
	}

	// if found is pointer to item, otherwise null
	return ret_item;
}