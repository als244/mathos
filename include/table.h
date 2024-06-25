#ifndef TABLE_H
#define TABLE_H

#include "common.h"

typedef uint64_t (*Hash_Func)(void * item, uint64_t table_size);
typedef int (*Item_Cmp)(void * item, void * other_item);

typedef struct table {
	// number of objects in table
	uint64_t cnt;
	// amount of allocated space in table
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
	pthread_mutex_t table_lock;
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
int insert_item(Table * table, void * item);
void * find_item(Table * table, void * item);
void * remove_item(Table * table, void * item);

#endif