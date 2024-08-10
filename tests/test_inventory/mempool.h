#ifndef MEMPOOL_H
#define MEMPOOL_H

#include "common.h"
#include "skiplist.h"


typedef struct mempool {
	uint64_t pool_id;
	uint64_t capacity_bytes;
	uint64_t chunk_size;
	uint64_t num_chunks;
	uint64_t va_start_addr;
	
	Skiplist * free_ranges;



	// contains the number
	pthread_mutex_t free_lock;
	uint64_t free_cnt;
	// contains the chunk ids (index within phys_chunks array)
	// that have been marked free
	Deque * free_list;
	// The data QPs that are assoicated with transfering in/out of this pool	
} Mempool;


typedef struct mem_reservation {
	uint64_t pool_id;
	// what the system requested. not necessarily a multiple of num_chunks
	uint64_t size_bytes;
	uint64_t num_chunks;
	uint64_t * chunk_ids;
} Mem_Reservation;


// returns 0 upon success, otherwise error
// Allocates a mem_reservation, determines number of chunks, acquires free_list lock, 
// checks to see if enough chunks if available (otherwise error), if so dequeues chunks from free list
// and puts them in the mem_reservation chunk_ids list
// Then populates ret_mem_reservation
int reserve_memory(Mempool * mempool, uint64_t size_bytes, Mem_Reservation * ret_mem_reservation);

// returns 0 upon success, otherwise error
// Acquires free list lock, iterates over all chunks in mem_reservation and enqueues them
// then frees the mem_reservation struct
int release_memory(Mempool * mempool, Mem_Reservation * mem_reservation);

// used for decision making about what pools to reserve on
// acquires free_lock and returns mempool -> free_cnt
int query_num_free_chunks(Mempool * mempool);

#endif