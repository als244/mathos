#ifndef DEQUE_H
#define DEQUE_H

#include "common.h"

// DEFINING INTERFACE FUNCTIONS FOR BUILD PROCESS

// Structures to export 

// self referencing within struct def, so putting typedef here for readability
typedef struct deque_item Deque_Item;

struct deque_item {
	uint64_t id;
	Deque_Item * prev;
	Deque_Item * next;
};

typedef struct deque {
	uint64_t cnt;
	Deque_Item * head;
	Deque_Item * tail;
} Deque;


// Functions to export:

Deque * init_deque();
void destroy_deque(Deque * deque);
int enqueue(Deque * deque, uint64_t item);
int enqueue_front(Deque * deque, uint64_t item);
int dequeue(Deque * deque, uint64_t * ret_item);
int dequeue_rear(Deque * deque, uint64_t * ret_item);
bool is_deque_empty(Deque * deque);

#endif