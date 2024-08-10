#ifndef SKIPLIST_H
#define SKIPLIST_H

#include "common.h"
#include "deque.h"


// Implementation from "Concurrent Maintenance of Skip Lists, Pugh 1990"
//	- https://15721.courses.cs.cmu.edu/spring2016/papers/pugh-skiplists1990.pdf

typedef struct skiplist_item Skiplist_Item;

struct skiplist_item {
	void * key;
	// can have multiple values for same key
	Deque * value_list;
	// Using the lockless deque functions for value_list,
	// and instead using this lock to protect
	// each insert/take
	pthread_mutex_t value_cnt_lock;
	uint64_t value_cnt;
	uint8_t level;
	// array of size level_num
	Skiplist_Item ** forward;
	// array of size level_num
	pthread_mutex_t * forward_locks;

	// This gets set when taking an item
	// triggers value_cnt == 0
	// Redundant to value_cnt == 0, but here for readability 

	// When a reap occurs, only skiplist_items with this 
	// marked (or equivalently with value_cnt == 0) will actually
	// be deleted. It is possible for revival if an insert occurs
	// before a reap is triggered 
	bool is_zombie;
};


typedef struct level_range {
	uint8_t level;
	float start;
	// exclusive
	float stop;
} Level_Range;

typedef struct skiplist {
	// Should typically be log(max_keys), but this
	// just impacts performance, not correctness
	uint8_t max_levels;
	// between 0-1
	// level_factor = 0.5 means that
	// the probability of a node
	// being at level i == (level_factor)^i
	float level_factor;
	// Initialzied values based on max_levels & level_factor
	// (level_rangse & rand_level_upper_bound) make it trivially
	// to determine a random range with 1 call to rand() and a bsearch
	// across max_levels (<= 256 levels)
	Level_Range * level_ranges;
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
	// Locks when reassigning the head of the list
	pthread_mutex_t * list_head_locks;
	// Used to maintain a hint for the maximum current
	// level (the highest non-null level)
	uint8_t cur_max_level_hint;
	pthread_mutex_t level_hint_lock;
	pthread_mutex_t cnt_lock;
	// This is the number of unique skiplist_items 
	// because each item can hold multiple values (<= number of inserts, because each)
	uint64_t total_item_cnt;

	// When taking items, the skiplist_item might be left with a value_cnt
	// of zero (so in theory this element should be deleted)

	// However for the main usecases of this skiplist within system it will
	// be common for elements with the same key (memory range sizes + priority within queue) 
	// to be repeatedly inserted and deleted so removing and reinserting is a very large overhead. 
	// Instead we will keep the skiplist_item with count zero in case a new insert comes. 

	// This comes with the downside of increased search time (needing to pass over elements with 0 values),
	// thus we have a parametized zombie-ratio which will trigger a mass cleanup and block out all other operations
	// Because all operations are blocked out, we can very quickly updated all linked lists without any contention
	// or worry for races.

	// only when total_item_cnt has exceeded this value
	// will we consider reaping.
	//	- this enables the skiplist to grow to a large enough size
	//		without continously reaping and blocking out other functions
	uint64_t min_items_to_check_reap;

	// if total_item_cnt >= min_items_to_check_reap and
	// zombie_cnt > total_item_cnt * max_zombie ratio,
	// then we will intialize a reap
	float max_zombie_ratio;
	uint64_t zombie_cnt;
	bool is_reaping;
	// this is simply a boolean that gets set
	// when a reap is prepared to happen. It will
	// indicate for all ongoing operations to signal
	// the zombie cv if their operation caused the num
	// active_ops count to reach 0

	// Even if the ongoing operations decrease the zombie ratio
	// before the reap occurs that doesn't matter and the reap
	// will still continue
	bool at_zombie_cap;
	// used to indicate when number of active ops == 0
	// this is used for waiting when the gc reaches the cap
	// and needs to wait for all ongoing operations to finish
	// before freeing

	// also used to broadcast to all pending operations
	// after a reap completes
	pthread_cond_t reaping_cv;
	
	// a queue containing pointers to items
	// that (at one point) had 0 values
	// When a delete triggers the zombie count to exceed the zombie
	// ratio, a reap is triggered

	// This means waiting to reap until all ongoing operations complete (see below)
	// and then iterating over all items in this deque and deleting them from 
	// linked lists. Note that, some items in this deque might be pointing to NULL
	// if they were revived by an insert before the reap occurred, in which case
	// do not do anything with them
	Deque * zombies;

	// CAN ONLY COLLECT ZOMBIES WHEN NO ONGOING OPERATIONS!
	//	- the pointers are going to be modified without locks
	// 		during zombie reaping, so nothing else can be occuring

	// the current number of active operations
	uint64_t num_active_ops;
	// lock upon num_active_ops
	pthread_mutex_t op_lock;
} Skiplist;


Skiplist * init_skiplist(Item_Cmp key_cmp, uint8_t max_levels, float level_factor, uint64_t min_items_to_check_reap, float max_zombie_ratio);

// Appends value to the skiplist_item -> value_list that matches key. If no key exists, creates a skiplist_item
// and intializes a deque with value
// returns 0 on success, -1 on error
// only error is OOM
int insert_item_skiplist(Skiplist * skiplist, void * key, void * value);

// Removes element from skiplist_item -> value_list where the item is the minimum key >= key
// if no skiplist item (== value_list is NULL) then return NULL
void * take_closest_item_skiplist(Skiplist * skiplist, void * key);


#endif