#ifndef FIFO_SYSV_H
#define FIFO_SYSV_H

#include "common.h"

typedef struct fifo {
	uint64_t max_items;
	uint64_t item_size_bytes;
	// the index at which to place the next item produced
	uint64_t produce_ind;
	// the index to consume the next item
	uint64_t consume_ind;
	// a bit redudant but a nice field to have
	uint64_t item_cnt;
	// lock to be used during producing/consuming
	pthread_mutex_t fifo_lock;
	// initialized to max_items
	int empty_slots_sem_id;
	// intialized to 0
	int full_slots_sem_id;
	// actually contains the items
	void * buffer;
} Fifo;


// Initializes fifo struct and allocates memory for buffer
Fifo * init_fifo(uint64_t max_items, uint64_t item_size_bytes);

// places the item at the back
// returns the index as which the item was inserted
// BLOCKING!
uint64_t produce_fifo(Fifo * fifo, void * item);

// returns the next item to be consume
// unless fifo is NULL or cannot allocate memory for item => returns NULL
// BLOCKING!
void consume_fifo(Fifo * fifo, void * ret_item);


// convert a combination of fifo buffer and index into a memory reference
void * get_buffer_addr(Fifo * fifo, uint64_t ind);




#endif