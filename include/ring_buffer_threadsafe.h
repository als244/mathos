#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "common.h"

typedef struct ring_buffer {
	uint64_t capacity;
	uint64_t item_size_bytes;
	uint64_t read_ind;
	uint64_t write_ind;
	pthread_mutex_t index_lock;
	pthread_mutex_t * slot_locks;
	void * items;
} Ring_Buffer;


Ring_Buffer * init_ring_buffer(uint64_t capacity, uint64_t item_size_bytes);

uint64_t get_write_addr(Ring_Buffer * ring_buffer, uint64_t n_advance);

int insert_item_ring(Ring_Buffer * ring_buffer, void * item);

int remove_item_ring(Ring_Buffer * ring_buffer, void * ret_item);

#endif
