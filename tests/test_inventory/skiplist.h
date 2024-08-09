#ifndef SKIPLIST_H
#define SKIPLIST_H

#include "common.h"
#include "deque.h"


// Implementation from "Concurrent Maintenance of Skip Lists, Pugh 1990"
//	- https://15721.courses.cs.cmu.edu/spring2016/papers/pugh-skiplists1990.pdf

typedef struct skiplist_item Skiplist_Item;

typedef struct skiplist_item {
	void * key;
	// can have multiple values for same key
	Deque * value_list;
	uint8_t level;
	// array of size level_num
	Skiplist_Item ** forward;
	// array of size level_num
	pthread_mutex_t * forward_locks;
} Skiplist_Item;


typedef struct level_range {
	uint8_t level;
	float start;
	// exclusive
	float stop;
} Level_Range;

typedef struct skiplist {
	uint8_t max_levels;
	// between 0-1
	// level_factor = 0.5 means that
	// the probability of a node
	// being at level i == (level_factor)^i
	float level_factor;
	Level_Range * level_ranges;
	// this takes on the value of 
	// 
	float rand_level_upper_bound;
	Item_Cmp key_cmp;
	// Of size max_levels
	// Points the head of the list
	// if item exists at that level,
	// otherwise NULL
	// All levels end in NULL terminator
	// (item_cmp deals with comparing against
	// NULL as cmp(key, NULL) < 0 => key is smaller,
	// this is used for the terminating case.
	// the starting point case (needs to be handled
	// manually, i.e. ensuring that the list has an item)
	Skiplist_Item ** level_lists;
	// When doing an insert/delete
	// the level at which the insert/delete
	// is occurring (skiplist_item -> level)
	// needs to be locked
	pthread_mutex_t * level_locks;
	// Used to maintain a hint for the maximum current
	// level (the highest non-null level)
	uint8_t cur_max_level_hint;
	uint64_t gc_cap;
	uint64_t cur_gc_cnt;
	pthread_mutex_t gc_cnt_lock;
	// the current number of active operations
	uint64_t num_active_ops;
	// lock upon num_active_ops
	pthread_mutex_t num_active_ops_lock;
	// used to indicate when number of active ops == 0
	// this is used for waiting when the gc reaches the cap
	// and needs to wait for all ongoing operations to finish
	// before freeing
	pthread_cond_t gc_cv;
	// an array of size gc_cap
	// upon filling up this array
	// (after a delete occurs)
	// wait upon all current operations to finish
	// the free the items in this array;
	void ** delete_bin;
} Skiplist;


Skiplist * init_skiplist(Item_Cmp key_cmp, uint8_t max_levels, float level_factor, uint64_t gc_cap);

// Appends value to the skiplist_item -> value_list that matches key. If no key exists, creates a skiplist_item
// and intializes a deque with value
// returns 0 on success, -1 on error
// only error is OOM
int insert_item_skiplist(Skiplist * skiplist, void * key, void * value);

// Removes element from skiplist_item -> value_list where the item is the minimum key >= key
// if no skiplist item (== value_list is NULL) then return NULL
void * take_closest_item_skiplist(Skiplist * skiplist, void * key);


#endif