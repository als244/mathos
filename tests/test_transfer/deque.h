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
	pthread_mutex_t list_lock;
} Deque;


// Functions to export:

Deque * init_deque();
void destroy_deque(Deque * deque, bool to_free_items);

// THESE ARE THREAD SAFE!
int get_count_deque(Deque * deque);
int take_deque(Deque * deque, DequeEnd take_end, void ** ret_item);
int insert_deque(Deque * deque, DequeEnd insert_end, void * item);
// Obtains the lock, dequeues, and then enqueues before relesing lock
int take_and_replace_deque(Deque * deque, DequeEnd take_end, DequeEnd replace_end, void ** ret_item);


#endif