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
	// If the min_size != max_size, then this table may grow/shrink
	tab -> resizable = min_size != max_size;
	tab -> resizing = false;

	// This variables only matter if resizable == true
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


	// TODO: ADD SYNC VARIABLE TO ENSURE O(1) REMOVALS/FIND (when missing) (instead of current O(N))

	ret = pthread_mutex_init(&(tab -> op_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not table op lock\n");
		return NULL;
	}
	ret = pthread_cond_init(&(tab -> removal_cv), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init table removal condition variable\n");
		return NULL;
	}
	tab -> num_removals = 0;

	ret = pthread_cond_init(&(tab -> insert_cv), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init table insert condition variable\n");
		return NULL;
	}
	tab -> num_inserts = 0;
	tab -> num_finds = 0;

	// TODO: ADD LOCK RATIO SO NOT EVERY SLOT NEEDS A LOCK!

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


// Wait for all pending operations to finish
// for returning count

uint64_t get_count_table(Table * table, bool to_wait_pending){

	uint64_t count;
	if (to_wait_pending){
		pthread_mutex_lock(&(table -> op_lock));
		
		// Sleep while there are ongoing removals
		//	- need to wait because a concurrent removal might swap item to a location that we already swept through
		while (table -> num_removals > 0){
			pthread_cond_wait(&(table -> removal_cv), &(table -> op_lock));
		}

		// Sleep while there are ongoing inserts
		while (table -> num_inserts > 0){
			pthread_cond_wait(&(table -> insert_cv), &(table -> op_lock));
		}

		count = table -> cnt;
		pthread_mutex_unlock(&(table -> op_lock));
	}
	else{
		count = table -> cnt;
	}
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


// assert(holding op_lock), 
// everyone else (insert/remove/find) is locked out so don't need to care about locks
// However, we will 
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
		if (old_table[i] == NULL){
			continue;
		}
		hash_ind = (table -> hash_func)(old_table[i], new_size);
		// do open addressing insert
		for (uint64_t j = hash_ind; j < hash_ind + new_size; j++){
			table_ind = j % new_size;
			// found empty slot to insert
			if (new_table[table_ind] == NULL) {
				new_table[table_ind] = old_table[i];
				break;
			}
			else{
			}
		}
	}

	// only need to modify the table field and size field
	table -> table = new_table;
	table -> slot_locks = new_slot_locks;
	table -> size = new_size;

	// we know these are unlocked
	for (uint64_t i = 0; i < old_size; i++){
		// Practice to hold lock before destorying
		pthread_mutex_lock(&(old_slot_locks[i]));
		pthread_mutex_destroy(&(old_slot_locks[i]));
	}

	free(old_slot_locks);

	// free the old table and reset to new one
	free(old_table);

	return 0;
}


// return -1 upon error
int insert_item_table(Table * table, void * item) {


	int ret;

	// Cannot insert during pending removals
	pthread_mutex_lock(&(table -> op_lock));
	while ((table -> num_removals > 0) || (table -> resizing)) {
		printf("In insert item. Woke up\n\tNum removals: %lu\n\n", table -> num_removals);
		pthread_cond_wait(&(table -> removal_cv), &(table -> op_lock));
	}


	// Now incdicate we are doing an insert
	// to prevent removals from occurring
	// or to inform future inserts that
	// are on boarder of growth region to wait for this
	table -> num_inserts += 1;
	
	// we now know there are no pending removals, so we can obtain the count
	// and (worst-case) assume all inserts are new
	uint64_t current_cnt = table -> cnt;
	uint64_t total_cnt = current_cnt + table -> num_inserts;
	uint64_t max_size = table -> max_size;

	// before releasing op lock we need to determine if the table will need to
	// grow/shrink before searching through it

	// If the table is resizeable then we might have to grow
	if (table -> resizable){
			uint64_t cur_size = table -> size;
			float load_factor = table -> load_factor;
			// make sure types are correct when multiplying uint64_t by float
			// This is the total_cnt load that would trigger a growth
			uint64_t load_cap = (uint64_t) (cur_size * load_factor);

			// We would want to grow before inserting this item

			// Can have multiple concurrent inserts reach this point,
			//	- because we release the op_lock within the cond_wait below
			// but they all must have unique total_cnts. 
			// Only one of these inserts will have total_cnt == cap
			//		- this one will wait for the others to finish before resizing
			if ((cur_size < max_size) && (total_cnt == load_cap)){


				uint64_t new_size = (uint64_t) ((double) cur_size * (1.0f / load_factor));
				if (new_size > max_size){
					new_size = max_size;
				}

				// Set this variable before releasing lock
				table -> resizing = true;
				
				// wait for all previous inserts & finds to finish
				//	because we incremented num inserts at the beginning
				// (to prevent simulateneous removals) we should wait 
				// until there is 1 insert left (which is this thread's function)
				while((table -> num_inserts > 1) || (table -> num_finds > 0)){
					pthread_cond_wait(&(table -> insert_cv), &(table -> op_lock));
				}

				// We hold the OP-lock while resize is happening
				// so that means no other functions can occur (as we wanted)

				// now there are no more operations so we can properly resize
				ret = resize_table(table, new_size);
				if (ret != 0){
					fprintf(stderr, "Error: resize table failed when growing from %lu to %lu. Was triggered when current count was %lu and total cnt was %lu\n", 
											cur_size, new_size, current_cnt, total_cnt);
					pthread_mutex_unlock(&(table -> op_lock));
					return -1;
				}

				// we can wake up the pending functions now
				table -> resizing = false;
				
				// Need allow the "find" functions and other "inserts"
				// that were blocked by resizing to go through now
				pthread_cond_broadcast(&(table -> removal_cv));
			}
	}


	// ensure size is checked while holding lock
	uint64_t size = table -> size;

	// we can release the op lock now, but will acquire it again when finished to decrement
	// the inserts
	pthread_mutex_unlock(&(table -> op_lock));

	
	uint64_t hash_ind = (table -> hash_func)(item, size);
	uint64_t table_ind;
	// doing the Linear Probing
	// worst case O(size) insert time
	void ** tab = table -> table;
	pthread_mutex_t * slot_locks = table -> slot_locks;
	bool is_duplicate = false;
	bool is_inserted = false;
	
	// can shortcut the insertion


	for (uint64_t i = hash_ind; i < hash_ind + size; i++){
		table_ind = i % size;
		pthread_mutex_lock(&(slot_locks[table_ind]));
		// if item was already in table
		if ((tab[table_ind] != NULL) && ((table -> item_cmp)(item, tab[table_ind]) == 0)){
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			is_duplicate = true;
			is_inserted = true;
			break;
		}
		else if (tab[table_ind] == NULL) {
			tab[table_ind] = item;
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			is_inserted = true;
			break;
		}
		else{
			pthread_mutex_unlock(&(slot_locks[table_ind]));
		}
	}

	if (!is_inserted){
		fprintf(stderr, "Error: item was not inserted into table. Table size was %lu and count value was %lu\n", size, table -> cnt);
	}


	pthread_mutex_lock(&(table -> op_lock));
	table -> num_inserts -= 1;
	// If we added a new item to the table
	if (is_inserted && !is_duplicate){
		table -> cnt += 1;
	}
	// The reason for num_inserts == 1 is because when the table is growing
	// there will be a pending insert waiting on this insert to finish
	bool is_insert_notify = ((table -> num_inserts == 0) || (table -> num_inserts == 1));
	pthread_mutex_unlock(&(table -> op_lock));

	// Indicate to pending finds & removals that they might be able to go
	if (is_insert_notify){
		pthread_cond_broadcast(&(table -> insert_cv));
	}

	if (is_duplicate){
		return 1;
	}

	return 0;
}


// For "correctness" find_item needs to wait for just pending removals
void * find_item_table(Table * table, void * item){


	// Cannot do find during pending removals
	pthread_mutex_lock(&(table -> op_lock));
	while (table -> num_removals > 0 || table -> resizing){
		pthread_cond_wait(&(table -> removal_cv), &(table -> op_lock));
	}

	// Now incdicate we are doing a find
	// to prevent removals from occurring
	table -> num_finds += 1;

	// ensure size is checked while holding lock
	uint64_t size = table -> size;

	// we can release the op lock now, but will acquire it again when finished to decrement
	// the inserts
	pthread_mutex_unlock(&(table -> op_lock));


	
	uint64_t hash_ind = (table -> hash_func)(item, size);
	void ** tab = table -> table;
	void * found_item = NULL;
	pthread_mutex_t * slot_locks = table -> slot_locks;
	uint64_t table_ind;
	
	
	// do linear scan
	for (uint64_t i = hash_ind; i < hash_ind + size; i++){
		table_ind = i % size;
		pthread_mutex_lock(&(slot_locks[table_ind]));
		if ((tab[table_ind] != NULL) && ((table -> item_cmp)(item, tab[table_ind]) == 0)){
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			found_item = tab[table_ind];
			break;
		}
		// There was an empty slot, so we know item doesn't exist
		else if (tab[table_ind] == NULL){
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			found_item = NULL;
			break;
		}
		else{
			// continue searching
			pthread_mutex_unlock(&(slot_locks[table_ind]));
		}
	}


	pthread_mutex_lock(&(table -> op_lock));
	table -> num_finds -= 1;
	// The reason for table inserts == 1 is because when table is growing there will be a pending insert waiting 
	// on this find to finish
	bool is_find_notify = (table -> num_finds == 0) && ((table -> num_inserts == 0) || (table -> num_inserts == 1));
	pthread_mutex_unlock(&(table -> op_lock));

	// Indicate to pending removals that they might be able to go
	if (is_find_notify){
		pthread_cond_broadcast(&(table -> insert_cv));
	}

	return found_item;
}

// remove from table and return pointer to item (can be used later for destroying)
void * remove_item_table(Table * table, void * item) {
	
	int ret;

	// Cannot remove during pending insert/find
	pthread_mutex_lock(&(table -> op_lock));
	while (((table -> num_inserts > 0) || (table -> num_finds > 0)) || (table -> resizing)){
		printf("In remove item. Woke up\n\tNum inserts: %lu\n\tNum finds: %lu\n\n", table -> num_inserts, table -> num_finds);
		pthread_cond_wait(&(table -> insert_cv), &(table -> op_lock));
	}

	// Now incdicate we are doing a removal
	// to prevent other inserts and finds from going
	table -> num_removals += 1;

	// we now know there are no pending inserts/finds, so we can obtain the count
	// and (worst-case) assume all removals will purge an existing item
	uint64_t current_cnt = table -> cnt;
	uint64_t pending_removals = table -> num_removals;

	uint64_t total_cnt;

	// Assuming all removals will succeed
	if (current_cnt < pending_removals){
		// to prevent overflow
		total_cnt = 0;
	}
	else {
		total_cnt = current_cnt - table -> num_removals;
	}


	bool resized = false;

	// If the table is resizeable then we might have to shrink
	if (table -> resizable){
		uint64_t cur_size = table -> size;
		float shrink_factor = table -> shrink_factor;
		uint64_t min_size = table -> min_size;
		// This is the total_cnt that would be needed in order to cause a shrink
		uint64_t shrink_cap = (uint64_t) (cur_size * shrink_factor);


		// Can have multiple concurrent removes reach this point,
		//	- because we release the op_lock within the cond_wait below
		// but they all must have unique total_cnts. 
		// Only one of these inserts will have total_cnt == cap
		//		- this one will wait for the others to finish before resizing
		if ((cur_size > min_size) && (total_cnt == shrink_cap)) {
			uint64_t new_size = (uint64_t) ((double) cur_size * (1.0f - shrink_factor));
			if (new_size < min_size){
				new_size = min_size;
			}


			// Ensure to set this before releasing lock to prevent new functions from starting
			table -> resizing = true;

			// wait for all previous removals to finish
			// The > 1 is because this thread incremented num_removals at the beginning so 
			// we want all other to finish
			while(table -> num_removals > 1){
				pthread_cond_wait(&(table -> removal_cv), &(table -> op_lock));
			}


			// We hold the OP-lock while resize is happening
			// so that means no other functions can occur (as we wanted)

			// now there are no more operations so we can properly resize
			ret = resize_table(table, new_size);
			if (ret != 0){
				fprintf(stderr, "Error: resize table failed when shrinking from %lu to %lu. Was triggered when current count was %lu and total cnt was %lu\n", 
										cur_size, new_size, current_cnt, total_cnt);
				pthread_mutex_unlock(&(table -> op_lock));
				return NULL;
			}


			// we can wake up the pending functions now
			table -> resizing = false;

			resized = true;

			// Need allow the other "remove" functions that
			// were blocked by resizing to go through now
			// that were blocked by resizing to go through now
			pthread_cond_broadcast(&(table -> insert_cv));
		}
	}


	// ensure to get size while still holding lock
	uint64_t size = table -> size;

	pthread_mutex_unlock(&(table -> op_lock));
		
	uint64_t hash_ind = (table -> hash_func)(item, size);

	// orig set to null in case item not in table 
	// in which case we want to return NULL
	void * ret_item = NULL;
	uint64_t table_ind;
	// do linear scan
	void ** tab = table -> table;
	pthread_mutex_t * slot_locks = table -> slot_locks;

	bool is_exists = false;

	for (uint64_t i = hash_ind; i < hash_ind + size; i++){
		table_ind = i % size;
		// check if we found item, remember its contents and make room in table
		// use function pointer to check for item key
		pthread_mutex_lock(&(slot_locks[table_ind]));
		if ((tab[table_ind] != NULL) && ((table -> item_cmp)(item, tab[table_ind]) == 0)){
			// set item to be the item removed so we can return it
			ret_item = tab[table_ind];
			// Now need to find a replacement for this NULL to maintain invariant for insert/finds
			uint64_t replacement_ind;
			
		

			// Advance to the next non-null. Find first item that could
			// be found again by swapping it to the table_ind 

			// If none of items were able to be found again before non-null
			// that means we can just set table_ind to null and everything will be ok

			// Because j started at 1, we know maximum value for replacement_ind will be table_ind - 1
			//	(and won't run into double locking problems)

			// Start at next index
			uint64_t j = 1;
			uint64_t rehash_ind;

			while (j < size){
				replacement_ind = (table_ind + j) % size;
				pthread_mutex_lock(&(slot_locks[replacement_ind]));
				if (tab[replacement_ind] != NULL){
					rehash_ind = (table -> hash_func)(tab[replacement_ind], size);
					// Now check conditions to ensure a valid replacement (need to still be able to find
					//	the replacement item after swapping)

					// There are 3 conditions in which we wouldn't be able to find it again,
					//	so we check that none of these are true
					if (!((rehash_ind > table_ind && rehash_ind < replacement_ind) || 
							(table_ind > replacement_ind && table_ind < replacement_ind) || 
								(replacement_ind >= rehash_ind && replacement_ind < table_ind))){
						break;
					}
					else{
						// This element wouldn't be able to be found again at table_ind spot
						// so keep it where it is and try the next element
						pthread_mutex_unlock(&(slot_locks[replacement_ind]));
					}
					
				}
				else{
					// We encounerted a NULL spot without any problems refinding
					// elements so we should just remove the table ind
					replacement_ind = table_ind;
					break;
				}
				j++;
			}

			if (replacement_ind != table_ind){
				tab[table_ind] = tab[replacement_ind];
				tab[replacement_ind] = NULL;
				pthread_mutex_unlock(&(slot_locks[replacement_ind]));
			}
			else{
				tab[table_ind] = NULL;
			}
			// found item so break
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			is_exists = true;
			break;
		}
		// There was an empty slot, so we know item doesn't exist
		else if (tab[table_ind] == NULL){
			pthread_mutex_unlock(&(slot_locks[table_ind]));
			ret_item = NULL;
			break;
		}
		else{
			// continue searching
			pthread_mutex_unlock(&(slot_locks[table_ind]));
		}
		
	}

	// Indicate to pending inserts/finds that they might be able to go
	pthread_mutex_lock(&(table -> op_lock));
	table -> num_removals -= 1;
	// If we actually removed the item
	if (is_exists){
		table -> cnt -= 1;
	}

	// When shrinking final removal is actually when there is 1 left
	// if we resized then we need to notify pending "finds" that they can go through
	bool is_removal_notify = (table -> num_removals == 0) || (table -> num_removals == 1) || (resized);
	pthread_mutex_unlock(&(table -> op_lock));

	if (is_removal_notify){
		pthread_cond_broadcast(&(table -> removal_cv));
	}
	
	// if found is pointer to item, otherwise null
	return ret_item;
}


// Notes: 
//		- Needs to wait for all pending functions to finish
//		- Locks out all functions from starting while processing
//			- holds op_lock the whole time
int get_all_items_table(Table * table, bool to_start_rand, bool to_sort, uint64_t * ret_cnt, void *** ret_all_items) {


	pthread_mutex_lock(&(table -> op_lock));
	
	// Sleep while there are ongoing removals
	//	- need to wait because a concurrent removal might swap item to a location that we already swept through
	while (table -> num_removals > 0){
		pthread_cond_wait(&(table -> removal_cv), &(table -> op_lock));
	}

	// Sleep while there are ongoing inserts
	while (table -> num_inserts > 0){
		pthread_cond_wait(&(table -> insert_cv), &(table -> op_lock));
	}

	// Don't release this mutex until completely finished
	// 	(i.e. don't allow inserts/finds/removals to occur during this)


	// 1.) Acquire size & cnt 

	uint64_t size = table -> size;
	uint64_t cnt = table -> cnt;

	// 2.) Allocate container for all the items
	void ** all_items = (void **) malloc(cnt * sizeof(void *));
	if (all_items == NULL){
		fprintf(stderr, "Error: malloc failed to allocate all_items container\n");
		pthread_mutex_unlock(&(table -> op_lock));
		return -1;
	}

	// 3.) Iterate over table and add items

	void ** tab = table -> table;
	// shouldn't need to acquire any slot_locks because
	// other functions are locked out, but doing it anyways for readability
	pthread_mutex_t * slot_locks = table -> slot_locks;

	uint64_t num_added = 0;

	// normally start at index 0 and count up
	uint64_t table_ind_start = 0;
	// if the to_start_rand bool is set, then choose a random starting index 
	// and count up/loop around
	if (to_start_rand){
		table_ind_start = rand() % size;
	}

	uint64_t table_ind;
	for (uint64_t i = table_ind_start; i < table_ind_start + size; i++){
		table_ind = i % size;
		pthread_mutex_lock(&(slot_locks[table_ind]));
		// there was an item at this location
		if (tab[table_ind] != NULL){
			all_items[num_added] = tab[table_ind];
			num_added++;
		}
		pthread_mutex_unlock(&(slot_locks[table_ind]));

		// can break early if we've already seen everything
		if (num_added == cnt){
			break;
		}

	}

	if (num_added != cnt){
		fprintf(stderr, "Error: in get_all_items_table(). The table count (%lu) differs from items added (%lu)\n", cnt, num_added);
	}


	// 4.) Release the lock so the table can be modified again
	pthread_mutex_unlock(&(table -> op_lock));

	// 5.) If to_sort is set then call qsort on all items
	if (to_sort){
		qsort(all_items, cnt, sizeof(void *), table -> item_cmp);
	}

	// 6.) Set the returned count and container of all items
	*ret_cnt = cnt;
	*ret_all_items = all_items;

	return 0;
}
