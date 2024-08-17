#include "fast_table.h"

// Assumes memory has already been allocated for fast_table container

Fast_Table_Config * save_fast_table_config(Hash_Func hash_func, uint64_t key_size_bytes, uint64_t value_size_bytes, 
						uint64_t min_table_size, uint64_t max_table_size, float load_factor, float shrink_factor) {

	Fast_Table_Config * config = malloc(sizeof(Fast_Table_Config));
	if (!config){
		fprintf(stderr, "Error: malloc failed when trying to allocate fast table config\n");
		return NULL;
	}

	config -> hash_func = hash_func;
	config -> min_size = min_table_size;
	config -> max_size = max_table_size;
	config -> load_factor = load_factor;
	config -> shrink_factor = shrink_factor;
	config -> key_size_bytes = key_size_bytes;
	config -> value_size_bytes = value_size_bytes;

	return config;

}

// if memory error on creating table returns -1
// otherwise 0
int init_fast_table(Fast_Table * fast_table, Fast_Table_Config * config) {

	uint64_t min_size = config -> min_size;
	fast_table -> config = config;
	fast_table -> cnt = 0;

	fast_table -> size = min_size;




	// also uses an extra byte per entry to indicate if 
	// there is an item in that slot (otherwise wouldn't be able
	// to distinguish the key/value should be 0's or if they are empty)

	// this will be a bit vector where each item is a uint64_t
	// it is of size ceil(fast_table -> size / 64) == size shifted by 

	// the index into the table is the high order bits 56 bits of the bucket index within
	// fast_table -> items

	// and the bit position within each vector is the low order 6 bits
	fast_table -> is_empty_bit_vector = (uint64_t *) malloc(((min_size >> 6) + 1) * sizeof(uint64_t));
	if (unlikely(!fast_table -> is_empty_bit_vector)){
		return -1;
	}

	// initialize everything to empty
	memset(fast_table -> is_empty_bit_vector, 0xFF, ((config -> min_size >> 6) + 1) * sizeof(uint64_t));

	fast_table -> items = calloc(config -> min_size, config -> key_size_bytes + config -> value_size_bytes);
	if (unlikely(!fast_table -> items)){
		return -1;
	}
	return 0;
}


void destroy_fast_table(Fast_Table * fast_table) {
	free(fast_table -> items);
}

// returns size upon failure to find slot. should never happen because checked if null
// beforehand

// leaving the option to set to_get_empty to true, meaning that we should search for the next non-null slot
// in the table

// This is valuable during resizing
uint64_t get_next_ind_fast_table(uint64_t * is_empty_bit_vector, uint64_t table_size, uint64_t start_ind, bool to_flip_empty){

	// need to pass in a valid starting index
	if (unlikely(start_ind >= table_size)){
		return table_size;
	}

	// assert (is_empty_bit_vector) == (table_size >> 6) + 1

	// Determine the next largest empty index in the table (wraping around at the end)
	// The low order 6 bits of hash_ind refer to the bit position within each element of the
	// the bit vector.  The high-order 56 bits refer the index within the actual vector

	uint64_t bit_vector_size = (table_size >> 6) + 1;
	// higher order bits the hash index
	uint64_t start_vec_ind = start_ind >> 6;
	// low order 6 bits
	uint8_t start_bit_ind = start_ind & (0xFF >> 2); 
		
	// before we start we need to clear out the bits strictly less than bit ind
	// if we don't find a slot looking through all the other elements in the bit 
	// vector we will wrap around to these value
	uint64_t search_vector = is_empty_bit_vector[start_vec_ind] & (0xFFFFFFFFFFFFFFFF << start_bit_ind);

	// 64 because each element in bit-vector contains 64 possible hash buckets that could be full
	// Will add the returned next closest bit position to this value to obtain the next empty slot
	// With a good hash function and load factor hopefully this vector contains the value or at least
	// in the next few searches and won't need more than 1 search vector
		
	
	uint64_t cur_vec_ind = start_vec_ind;
	uint8_t least_sig_one_bit_ind;
	uint64_t insert_ind;

	// less than or equal because we might need the low order bits that we originally masked out

	// With good hash function and load factor this should only be 1 iteration when doing inserts
	// However for resizing we may find a long 

	// can be equal if we wrap around to the same starting index and
	// look at low order bits
	while(cur_vec_ind <= start_vec_ind + bit_vector_size){

		

		// we are are seaching for next full index we just flip the bits
		if(to_flip_empty){
			search_vector = (search_vector ^ 0xFFFFFFFFFFFFFFFF);
		}

		// returns 1 + the least significant 1-bit, so we want to subtract 1
		// when computing the proper empty value
		least_sig_one_bit_ind = __builtin_ffsll(search_vector);
		
		// is there is a matching slot
		// we can return the value corresponding to bucked ind
		if (least_sig_one_bit_ind != 0){
			insert_ind = 64 * (cur_vec_ind % bit_vector_size) + (least_sig_one_bit_ind - 1);
			return insert_ind;
		}

		// There were no empty slots (bit vector was all 0's), so we continue to the next
		// set of 64 buckets
		cur_vec_ind++;

		// if the cur_vec_ind would be wrapped around we don't
		// need to do any masking because we just care about the low
		// order bits which weren't seen the first go-around
		search_vector = is_empty_bit_vector[cur_vec_ind % bit_vector_size];
	}

	// indicate that we couldn't find an empty slot
	return table_size;

}


int resize_fast_table(Fast_Table * fast_table, uint64_t new_size){


	uint64_t old_size = fast_table -> size;
	uint64_t cnt = fast_table -> cnt;


	// 1.) Allocate new memory for the table

	// create new table that will replace old one
	// ensure new table is initialized to all null

	uint64_t key_size_bytes = fast_table -> config -> key_size_bytes;
	uint64_t value_size_bytes = fast_table -> config -> value_size_bytes;

	uint64_t * new_is_empty_bit_vector = (uint64_t *) malloc(((new_size >> 6) + 1) * sizeof(uint64_t));
	if (unlikely(!new_is_empty_bit_vector)){
		fprintf(stderr, "Error: trying to resize fast table from %lu to %lu failed.\n", old_size, new_size);
		return -1;
	}

	// initialize everything to empty by setting to 1 everywhere
	memset(fast_table -> is_empty_bit_vector, 0xFF, ((new_size >> 6) + 1) * sizeof(uint64_t));
	
	void * new_items = (void *) calloc(new_size, key_size_bytes + value_size_bytes);
	if (unlikely(!new_items)){
		fprintf(stderr, "Error: trying to resize fast table from %lu to %lu failed.\n", old_size, new_size);
		return -1;
	}

	void * old_items = fast_table -> items;
	uint64_t * old_is_empty_bit_vector = fast_table -> is_empty_bit_vector;
	uint64_t next_old_item_ind;

	// we know how many items we need to re-insert

	uint64_t seen_cnt = 0;

	void * old_key;
	void * old_value;
	void * new_key_pos;
	void * new_value_pos;


	uint64_t new_hash_ind;
	uint64_t new_insert_ind;

	uint64_t old_get_next_start_ind = 0;


	// because we know the count we don't need to error check for next index returning the table size.
	// they are guaranteed to succeed
	while (seen_cnt < cnt){

		next_old_item_ind = get_next_ind_fast_table(old_is_empty_bit_vector, old_size, old_get_next_start_ind, true);

		// Now we need to re-hash this item into new table
		old_key = (void *) (((uint64_t) old_items) + (next_old_item_ind * (key_size_bytes + value_size_bytes)));
		old_value = (void *) (((uint64_t) old_key) + key_size_bytes);

		new_hash_ind = (fast_table -> config -> hash_func)(old_key, new_size);

		// now we can get the insert index for the new table. now we are searching for next free slot
		// and we will use the new bit vector
		new_insert_ind = get_next_ind_fast_table(new_is_empty_bit_vector, new_size, new_hash_ind, false);


		// now setting the pointer to be within new_items
		new_key_pos = (void *) (((uint64_t) new_items) + (new_insert_ind * (key_size_bytes + value_size_bytes)));
		new_value_pos = (void *) (((uint64_t) new_key_pos) + key_size_bytes);

		// Actually copy into the new table place in table
		memcpy(new_key_pos, old_key, key_size_bytes);
		memcpy(new_value_pos, old_value, value_size_bytes);

		// Ensure to update the new bit vector that we inserted at new_insert_ind 
		// (because there may be collisions within re-inserting and we need the bit vector to track these)

		// No point in updating old bit vector because we started from zero and are only increasing updwards 
		// until we see all cnt elements

		// clearing the entry for this insert_ind in the bit vector

		// the bucket's upper bits represent index into the bit vector elements
		// and the low order 6 bits represent offset into element. Set to 0 
		new_is_empty_bit_vector[new_insert_ind >> 6] &= ~(1 << (new_insert_ind & (0xFF >> 2)));
		seen_cnt += 1;
	}


	// now reset the table size, items, and bit vector 
	// and free the old memory

	fast_table -> size = new_size;
	fast_table -> is_empty_bit_vector = new_is_empty_bit_vector;
	fast_table -> items = new_items;


	free(old_is_empty_bit_vector);
	free(old_items);

	
	return 0;
}




// returns 0 on success, -1 on error

// does memcopiess of key and value into the table array
// assumes the content of the key cannot be 0 of size key_size_bytes
int insert_fast_table(Fast_Table * fast_table, void * key, void * value) {


	uint64_t size = fast_table -> size;
	uint64_t cnt = fast_table -> cnt;

	// should only happen when cnt = max_size
	// otherwise we would have grown the table after the 
	// prior triggering insertion
	if (unlikely(cnt == size)){
		return -1;
	}


	// 1.) Lookup where to place this item in the table

	// acutally compute the hash index
	uint64_t hash_ind = (fast_table -> config -> hash_func)(key, fast_table -> size);

	// we already saw cnt != size so we are guaranteed for this to succeed
	uint64_t insert_ind = get_next_ind_fast_table(fast_table -> is_empty_bit_vector, fast_table -> size, hash_ind, false);
	
	uint64_t key_size_bytes = fast_table -> config -> key_size_bytes;
	uint64_t value_size_bytes = fast_table -> config -> value_size_bytes;


	// Now we want to insert into the table by copying key and value 
	// into the appropriate insert ind and then setting the 
	// is_empty bit to 0 within the bit vector


	// 2.) Copy the key and value into the table 
	//		(memory has already been allocated for them within the table)

	void * items = fast_table -> items;

	// setting the position for the key in the table
	// this is based on the insert_index that was returned to us
	void * key_pos = (void *) (((uint64_t) items) + (insert_ind * (key_size_bytes + value_size_bytes)));
	// advance passed the key we will insert
	void * value_pos = (void *) (((uint64_t) key_pos) + key_size_bytes);

	// Actually place in table
	memcpy(key_pos, key, key_size_bytes);
	memcpy(value_pos, value, value_size_bytes);


	// 3.) Update bookkeeping values

	cnt += 1;
	fast_table -> cnt = cnt;


	// clearing the entry for this insert_ind in the bit vector

	// the bucket's upper bits represent index into the bit vector elements
	// and the low order 6 bits represent offset into element. Set to 0 
	(fast_table -> is_empty_bit_vector)[insert_ind >> 6] &= ~(1 << (insert_ind & (0xFF >> 2)));


	// 4.) Potentially resize

	// check if we exceed load and are below max cap
	// if so, grow
	float load_factor = fast_table -> config -> load_factor;
	uint64_t max_size = fast_table -> config -> max_size;
	// make sure types are correct when multiplying uint64_t by float
	uint64_t load_cap = (uint64_t) (size * load_factor);
	if ((size < max_size) && (cnt > load_cap)){
		// casting from float to uint64 is fine
		size = (uint64_t) (size * (1.0f / load_factor));
		if (size > max_size){
			size = max_size;
		}
		int ret = resize_fast_table(fast_table, size);
		// check if there was an error growing table
		// indicate that there was an error by being able to insert

		// might want a different error message here because this is fatal
		if (unlikely(ret == -1)){
			return -1;
		}
	}

	return 0;


}


// Returns the index in the table and returns
// the index at which it was found
// Returns fast_table -> max_size if not found

// A copy of the value assoicated with key in the table
// Assumes that memory of value_sized_bytes as already been allocated to ret_val
// And so a memory copy will succeed
uint64_t find_fast_table(Fast_Table * fast_table, void * key, bool to_copy_value, void * ret_value){

	uint64_t size = fast_table -> size;
	uint64_t hash_ind = (fast_table -> config -> hash_func)(key, size);


	// get the next null value and search up to that point
	// because we are using linear probing if we haven't found the key
	// by this point then we can terminate and return that we didn't find anything

	// we could just walk along and check the bit vector as we go, but this is easily
	// (at albeit potential performance hit if table is very full and we do wasted work)
	uint64_t * is_empty_bit_vector = fast_table -> is_empty_bit_vector;
	uint64_t next_empty = get_next_ind_fast_table(is_empty_bit_vector, size, hash_ind, false);

	uint64_t cur_ind = hash_ind;


	uint64_t key_size_bytes = fast_table -> config -> key_size_bytes;
	uint64_t value_size_bytes = fast_table -> config -> value_size_bytes;

	void * cur_table_key = (void *) (((uint64_t) fast_table -> items) + (cur_ind * (key_size_bytes + value_size_bytes)));

	int key_cmp;

	while (cur_ind < next_empty) {

		// compare the key
		key_cmp = memcmp(key, cur_table_key, key_size_bytes);
		// if we found the key
		if (key_cmp == 0){

			// if we want the key, we want the value immediately after, so we add key_size_bytes
			// to the current key
			void * table_value = (void *) (((uint64_t) cur_table_key) + key_size_bytes);


			// now we want to copy the value and then can return
			if (to_copy_value) {
				memcpy(ret_value, table_value, value_size_bytes);
			}
			else{
				ret_value = table_value;
			}
			
			return cur_ind;
		}

		// update the next key position which will be just 1 element higher so we can add the size of 1 item

		// being explicity about type casting for readability...
		cur_table_key = (void *) (((uint64_t) cur_table_key) + key_size_bytes + value_size_bytes);

		// next empty might have a returned a value that wrapped around
		// if the whole table
		cur_ind = (cur_ind + 1) % size;
	}
	
	// We didn't find the element

	// now can set the return value to null and return not found as -1
	if (to_copy_value){
		ret_value = NULL;
	}
	
	

	return fast_table -> config -> max_size;
}

// returns 0 upon successfully removing, -1 on error finding. 

// Note: Might want to have different return value
// from function to indicate a fatal error that could have occurred within resized (in the gap of freeing larger
// table and allocating new, smaller one)

// if copy_val is set to true then copy back the item
int remove_fast_table(Fast_Table * fast_table, void * key, bool to_copy_value, void * ret_value) {


	// remove is equivalent to find, except we need to also:
	//	a.) mark the empty bit
	//	b.) decrease cnt
	//	c.) potentially shrink


	// 1.) Search for item!

	// if the item existed this will handle copying if we wanted to
	uint64_t found_ind = find_fast_table(fast_table, key, to_copy_value, ret_value);

	// item didn't exist so we immediately return
	if (found_ind == (fast_table -> config -> max_size)){
		return -1;
	}


	// 2.) If item was found, do proper bookkeeping

	// otherwise we need to update
	fast_table -> cnt -= 1;

	// clearing the entry for this insert_ind in the bit vector

	// the bucket's upper bits represent index into the bit vector elements
	// and the low order 6 bits represent offset into element. 

	// Set to 1 to indicate this bucket is now free 
	(fast_table -> is_empty_bit_vector)[found_ind >> 6] |= (1 << (found_ind & (0xFF >> 2)));


	// 3.) Check if this removal triggered 


	// check if we should shrink

	uint64_t size = fast_table -> size;
	// now this is updated afer we decremented
	uint64_t cnt = fast_table -> cnt;


	float shrink_factor = fast_table -> config -> shrink_factor;
	uint64_t min_size = fast_table -> config -> min_size;
	// make sure types are correct when multiplying uint64_t by float
	uint64_t shrink_cap = (uint64_t) (size * shrink_factor);
	if ((size > min_size) && (cnt < shrink_cap)) {
		size = (uint64_t) (size * (1 - shrink_factor));
		if (size < min_size){
			size = min_size;
		}
		int ret = resize_fast_table(fast_table, size);
		// check if there was an error growing table
		// fatal error here
		if (unlikely(ret == -1)){
			return -1;
		}
	}

	return 0;

}