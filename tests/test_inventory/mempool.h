#ifndef MEMPOOL_H
#define MEMPOOL_H

#include "common.h"
#include "deque.h"

typedef struct phys_chunk {
	// BACKEND-SPECFIC
	void * phys_mem_handle;
} Phys_Chunk;


typedef struct mempool {
	uint64_t pool_id;
	// equal to chunk_size * num_chunks
	uint64_t capacity_bytes;
	uint64_t chunk_size;
	uint64_t num_chunks;
	// should be an array of size num_chunks 
	// each entry refers to a chunk that was allocated with physical memory
	// and then mapped to a single large virtual address range
	// The entries into this array are populated by the memory backend upon intialization!
	Phys_Chunk ** phys_chunks;
	// upon initialization the entire mempool is mapped
	// and registered with ib_reg_mr or ib_reg_dmabuf_mr
	uint64_t va_start_addr;
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