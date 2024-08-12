#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"
#include "config.h"
#include "skiplist.h"
#include "utils.h"

#define SYSTEM_MEMPOOL_ID -1

typedef struct mem_range {
	uint64_t num_chunks;
	uint64_t start_chunk_id;
} Mem_Range;



typedef struct mempool {
	// corresponds to device id or -1 for system memory 
	int pool_id;
	uint64_t chunk_size;
	uint64_t num_chunks;
	// equivalent to chunk_size * num_chunks
	uint64_t capacity_bytes;
	uint64_t va_start_addr;
	Skiplist * free_mem_ranges;
	// size of num chunks
	// need this so when releasing memory & 
	// trying to merge contiguous free ranges
	// can search skiplist with this key
	// (only if num_chunks != 0 => endpoint of free range)
	uint64_t * endpoint_range_size;
	// acquired during releases to maintain contiguous range
	// invariant within skiplists
	// otherwise might not attempt to merge when it should have
	pthread_mutex_t * endpoint_locks;
} Mempool;


typedef struct memory {
	int num_devices;
	// array of num_devices
	Mempool * device_mempools;
	// has pool id SYSTEM_MEMPOOL_ID
	Mempool system_mempool;
} Memory;

typedef struct mem_reservation {
	int pool_id;
	// what the system requested. not necessarily a multiple of num_chunks
	uint64_t size_bytes;
	// these values obtaining by taking a range from the free_mem_ranges skiplist
	uint64_t num_chunks;
	uint64_t start_chunk_id;
	// mempool -> va_start_addr + (start_chunk_id * mempool -> chunk_size)
	void * buffer;
} Mem_Reservation;


// USED TO INITIALIZE SKIPLISTS

int mem_range_skiplist_item_key_cmp(void * mem_range_skiplist_item, void * other_mem_range_skiplist_item);
int mem_range_val_cmp(void * mem_range, void * other_mem_range);



// returns 0 upon success, otherwise error

// Assumes mem_reservation input has pool_id and size_bytes populated!
// Then populates mem_reservation with num_chunks, start_chunk_id and buffer
int reserve_memory(Memory * memory, Mem_Reservation * mem_reservation);

// returns 0 upon success, otherwise error
int release_memory(Memory * memory, Mem_Reservation * mem_reservation);


#endif