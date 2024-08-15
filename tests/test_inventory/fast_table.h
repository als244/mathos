#ifndef FAST_TABLE_H
#define FAST_TABLE_H

#include "common.h"


// NOTE: Assumes SINGLE-THREADED and that this table will be responsible for the memory
//			of key's and values inserted. It memcopies the arguments passed in because
//			we will be creating values on the stack and passing those references into
//			these functions. ** Special case: For very large sized keys/values (in bytes), then we would
//			be careful to not have stack-overflow and would dynamically allocate and then
//			copy again and free the original.


// already defined in other table


typedef struct fast_table {
	uint64_t cnt;
	uint64_t size;
	uint64_t min_size;
	uint64_t max_size;
	// This variables only matter if resizable == true
	// when cnt > size * load_factor
	// set new size to be size * (1 / load_factor) and grow table
	float load_factor;
	// when cnt < size * shrink_factor
	// set new size to be size * (1 - shrink_factor) and shrink table 
	float shrink_factor;
	Hash_Func hash_func;
	// will be used to advance in the hash table
	uint64_t key_size_bytes;
	// to know how much room to allocate
	uint64_t value_size_bytes;

	// a bit vector of capacity size >> 6 uint64_t's
	// upon an insert an item's current index is checked
	// against this vector to be inserted

	// initialized to all ones. when something get's inserted
	// it flips bit to 0. 

	// will use __builtin_ffsll() to get bit position of least-significant
	// 1 in order to determine the next empty slot
	uint64_t * is_empty_bit_vector;
	// array that is sized
	// (key_size_bytes + value_size_bytes * size
	// the indicies are implied by the total size
	// Assumes all items inserted have the first
	// key_size_bytes of the entry representing
	// the key for fast comparisons
	void * items;
} Fast_Table;



// Assumes memory has already been allocated for fast_table container
int init_fast_table(Fast_Table * fast_table, Hash_Func hash_func, uint64_t key_size_bytes, uint64_t value_size_bytes, 
						uint64_t min_table_size, uint64_t max_table_size, float load_factor, float shrink_factor);

// all it does is free fast_table -> items
void destroy_fast_table(Fast_Table * fast_table);



// returns 0 on success, -1 on error

// does memcopiess of key and value into the table array
// assumes the content of the key cannot be 0 of size key_size_bytes
int insert_fast_table(Fast_Table * fast_table, void * key, void * value);



// Returns the index at which item was found on success, fast_table -> max_size on not found
//	- returning the index makes remove easy (assuming single threaded)

// A copy of the value assoicated with key in the table
// Assumes that memory of value_sized_bytes as already been allocated to ret_val
// And so a memory copy will succeed

// If to_copy_value is set the copy back the the item. If no item exists and this flag is set, ret_value is set to NULL
uint64_t find_fast_table(Fast_Table * fast_table, void * key, bool to_copy_value, void * ret_value);

// returns 0 upon success, -1 upon error
// if copy_val is set to true then copy back the item. If no item exists and this flag is set, ret_value is set to NULL
int remove_fast_table(Fast_Table * fast_table, void * key, bool to_copy_value, void * ret_value);




#endif