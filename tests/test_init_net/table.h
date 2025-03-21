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
	// when cnt > size * load_factor
	// set new size to be size * (1 / load_factor) and grow table
	float load_factor;
	// when cnt < size * shrink_factor
	// set new size to be size * (1 - shrink_factor) and shrink table 
	float shrink_factor;

	// lock to prevent inserting/removing during a re-size 
	// and to do "atomic" find_items without worrying about re-size
	pthread_mutex_t size_lock;
	// lock to read/write each slot
	pthread_mutex_t * slot_locks;

	pthread_mutex_t cnt_lock;
	
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
int insert_item_get_index_table(Table * table, void * item, uint64_t * ret_index);
void * find_item_table(Table * table, void * item);
int find_item_index_table(Table * table, void * item, uint64_t * ret_index);
void * remove_item_table(Table * table, void * item);
int remove_item_at_index_table(Table * table, void * item, uint64_t index);
int remove_random_item(Table * table, void ** ret_item, uint64_t * ret_index);


// Notes: 
//	- Returns the count and populated the 2nd argument with a view of all the items
//		- DO NOT FREE THE ITEMS WITHIN THIS ARRAY, BUT SHOULD FREE THE RETURNED ARRAY!
// 	- These functions acquire size & count locks for duration 
//		- (i.e. completely block out other functions until completition)
// It allocates a container array for all items, but doesn't copy item

// to_start_rand means retrieving items starting at a random index
// to_sort means calling the Table -> Item_Cmp function on the all_items container before returning
int get_all_items_table(Table * table, bool to_start_rand, bool to_sort, uint64_t * ret_cnt, void *** ret_all_items);

uint64_t get_count(Table * table);

#endif