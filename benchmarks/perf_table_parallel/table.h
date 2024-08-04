#ifndef TABLE_H
#define TABLE_H

#include "common.h"

typedef uint64_t (*Hash_Func)(void * item, uint64_t table_size);
typedef int (*Item_Cmp)(void * item, void * other_item);

typedef struct table {
	// number of objects in table
	uint64_t cnt;
	// amount of allocated space (slots) in table
	uint64_t size;
	// never shrink smaller than min_size
	uint64_t min_size;
	// setting an upper limit on size to prevent out of memory growing
	uint64_t max_size;
	// if min_size != max_size
	// if not resizeable then we can avoid acquiring the size lock
	bool resizable;

	
	// This variables only matter if resizable == true
	// when cnt > size * load_factor
	// set new size to be size * (1 / load_factor) and grow table
	float load_factor;
	// when cnt < size * shrink_factor
	// set new size to be size * (1 - shrink_factor) and shrink table 
	float shrink_factor;

	// lock to read/write each slot
	pthread_mutex_t * slot_locks;

	// Not allowed to do INSERT or FIND while a removal is taking place!
	// Because upon successful removal, the function needs to maintain
	// invariant of finding first NULL slot means item doesn't exist
	// when searching
	// Can't do insert because not sure what slot is going to be NULL
	// Can't do find because the item we are searching for could be
	// swapped at the same time of find thread and the find thread 
	// already passed over that index

	pthread_mutex_t op_lock;
	pthread_cond_t removal_cv;
	// the number of concurrent removals
	uint64_t num_removals;
	// For same reason, cannot do a removal while an insert/find is occurring
	pthread_cond_t insert_cv;
	// the number of concurrent inserts
	uint64_t num_inserts;
	// the number of concurrent finds
	// 	- could have just incremented num_inserts instead of having num_finds,
	//		but better for readability
	uint64_t num_finds;

	// set this bool when a resize is triggered to prevent
	// new functions from attempting to start
	bool resizing;
	pthread_cond_t resizing_cv;

	// TODO: ADD LOCK RATIO SO NOT EVERY SLOT NEEDS A LOCK!
	
	// Function pointer to retrieve the key of objects inserted into table
	//	- which is then passed the hash function
	Hash_Func hash_func;
	// Function to compare item, returns 0 if equal
	Item_Cmp item_cmp;
	void ** table;
} Table;

// Functions to export:
Table * init_table(uint64_t min_size, uint64_t max_size, float load_factor, float shrink_factor, Hash_Func hash_func, Item_Cmp item_cmp);
void destroy_table(Table * table);


// enforce the caller supplied item_key because don't want to defrence void * 
int insert_item_table(Table * table, void * item);
void * find_item_table(Table * table, void * item);
void * remove_item_table(Table * table, void * item);


// Notes: 
//	- Returns the count and populated the 2nd argument with a view of all the items
//		- DO NOT FREE THE ITEMS WITHIN THIS ARRAY, BUT SHOULD FREE THE RETURNED ARRAY!
// 	- These functions acquire size & count locks for duration 
//		- (i.e. completely block out other functions until completition)
// It allocates a container array for all items, but doesn't copy item

// to_start_rand means retrieving items starting at a random index
// to_sort means calling the Table -> Item_Cmp function on the all_items container before returning
int get_all_items_table(Table * table, bool to_start_rand, bool to_sort, uint64_t * ret_cnt, void *** ret_all_items);

uint64_t get_count_table(Table * table, bool to_wait_pending);

#endif