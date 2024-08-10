#ifndef DEQUE_H
#define DEQUE_H

#include "common.h"

typedef enum deque_end {
	FRONT_DEQUE,
	BACK_DEQUE
} DequeEnd;

// DEFINING INTERFACE FUNCTIONS FOR BUILD PROCESS

// Structures to export 

// self referencing within struct def, so putting typedef here for readability
typedef struct deque_item Deque_Item;

struct deque_item {
	void * item;
	Deque_Item * prev;
	Deque_Item * next;
};

typedef struct deque {
	uint64_t cnt;
	Deque_Item * head;
	Deque_Item * tail;
	Item_Cmp item_cmp;
	// TODO: Can make more efficient by having
	// a head lock and tail lock!!
	pthread_mutex_t list_lock;
} Deque;


// Functions to export:

Deque * init_deque(Item_Cmp item_cmp);
void destroy_deque(Deque * deque, bool to_free_items);

// THESE ARE THREAD SAFE!
uint64_t get_count_deque(Deque * deque);

int insert_deque(Deque * deque, DequeEnd insert_end, void * item);
int take_deque(Deque * deque, DequeEnd take_end, void ** ret_item);


// Obtains the lock, dequeues, and then enqueues before relesing lock
int take_and_replace_deque(Deque * deque, DequeEnd take_end, DequeEnd replace_end, void ** ret_item);

int peek_item_at_index_deque(Deque * deque, DequeEnd start_end, uint64_t index, void ** ret_item);


// returns the number of items that were removed
// to_free indicates if the item should be freed upon removal
// simpler API compared to below
//		- removes all occurrences of item
uint64_t remove_if_eq_deque(Deque * deque, void * item, bool to_free);

// can use max_remove and search_start_end to accelerate removal
//	(i.e. if caller knows there is maximum of 1 copy of item in deque, and wants to remove it
//			they can set max_remove = 1 to break when it is removed. If they have prior of 
//			if the item would be at beginning or end then they can choose where to start searching)
uint64_t remove_if_eq_accel_deque(Deque * deque, void * item, uint64_t max_remove, DequeEnd search_start_end, bool to_free);


uint64_t get_item_count_deque(Deque * deque, void * item);


// Non-locked variants
//	- USE WITH CAUTION!
//		- either single-threaded or high-level lock protecting the deque
int insert_lockless_deque(Deque * deque, DequeEnd insert_end, void * item);
int take_lockless_deque(Deque * deque, DequeEnd take_end, void ** ret_item);



#endif