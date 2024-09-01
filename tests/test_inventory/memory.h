#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"
#include "config.h"
#include "utils.h"

#include "fast_list.h"
#include "fast_table.h"
#include "fast_tree.h"


#define SYSTEM_MEMPOOL_ID -1


typedef enum backend_memory_type {
	HSA_MEMORY,
	CUDA_MEMORY
} BackendMemoryType;

typedef struct mem_range {
	uint64_t range_size;
	// this is a pointer to the node
	// which contains the start_chunk_id
	Fast_List_Node * start_chunk_id_ref;
} Mem_Range;



typedef struct mempool {
	// corresponds to device id or -1 for system memory 
	int pool_id;
	uint64_t chunk_size;
	uint64_t num_chunks;
	// equivalent to chunk_size * num_chunks
	uint64_t capacity_bytes;
	uint64_t va_start_addr;
	// mapping from range size => Fast_Lists of all mem_ranges of specific size
	// each fast list will contain starting chunk id's with that assoicated
	// size
	// There is one list per range size and this gets inserted
	// into the free_mem_ranges fast tree
	Fast_Table * range_lists_table;
	// mapping from requested range_size => equal or successor Fast_List
	// wil be using FAST_TREE_EQUAL_OR_NEXT for searching this tree
	Fast_Tree * free_mem_ranges;
	// mapping from chunk id => mem_range
	// used during release_memory() to determine if the released
	// range should be merged
	// Each mem_range with size > 1 chunk will get inserted
	// into this table twice
	Fast_Table * free_endpoints;

	// each mempool gets a unique MR for all the ib_devices
	// on system because needs seperate PDs for each device
	// they all reference same [va_start_addr, va_start_addr + capacity_bytes]
	// region but have unique mr -> lkey

	// this is an array of size memory -> num_ib_devices
	// this is allocated/initialized after the intial memory intiialzation
	// and after the network intialization
	struct ibv_mr ** ib_dev_mrs;
} Mempool;


typedef struct memory {
	int num_devices;
	// array of num_devices
	Mempool * device_mempools;
	// has pool id SYSTEM_MEMPOOL_ID
	Mempool system_mempool;
	// this is to reference each unique lkey for
	// the different rdma devices referencing
	// the same device pool
	int num_ib_devices;
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



// SHOULD HAVE THIS HERE BUT FOR NOW FOR EASY BUILD
// PURPOSES LEAVING THIS WITHIN THE BACKEND_MEMORY FILE ITSELF!

// Memory * init_backend_memory(void * backend_memory_ref, BackendMemoryType backend_memory_type);

Fast_List_Node * insert_free_mem_range(Mempool * mempool, uint64_t start_chunk_id, uint64_t range_size);
Fast_List * remove_free_mem_range(Mempool * mempool, Mem_Range * mem_range, Fast_List * known_range_list);

// returns 0 upon success, otherwise error

// Assumes mem_reservation input has pool_id and size_bytes populated!
// Then populates mem_reservation with num_chunks, start_chunk_id and buffer
int reserve_memory(Memory * memory, Mem_Reservation * mem_reservation);

// returns 0 upon success, otherwise error
int release_memory(Memory * memory, Mem_Reservation * mem_reservation);


#endif