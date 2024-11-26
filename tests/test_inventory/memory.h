#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"
#include "config.h"
#include "utils.h"

#include "fifo.h"
#include "fast_list.h"
#include "fast_table.h"
#include "fast_tree.h"


typedef enum backend_memory_type {
	HSA_MEMORY,
	CUDA_MEMORY
} BackendMemoryType;



typedef enum mem_op_type {
	MEMORY_RESERVATION,
	MEMORY_RELEASE
} MemOpType;

typedef enum mem_op_status {
	MEMORY_SUCCESS,
	MEMORY_POOL_OOM,
	MEMORY_SYSTEM_ERROR,
	MEMORY_INVALID_OP
} MemOpStatus;

typedef struct mem_range {
	uint64_t range_size;
	// this is a pointer to the node
	// which contains the start_chunk_id
	Fast_List_Node * start_chunk_id_ref;
} Mem_Range;

typedef struct mempool_stats {
	uint64_t num_reservations;
	uint64_t num_releases;
	uint64_t num_oom_seen;
} Mempool_Stats;

typedef struct mempool {
	// corresponds to device id or -1 for system memory 
	int pool_id;
	uint64_t chunk_size;
	uint64_t num_chunks;
	// equivalent to chunk_size * num_chunks
	uint64_t capacity_bytes;
	uint64_t va_start_addr;
	// the total number of free chunks (could be discontiguous)
	uint64_t total_free_chunks;
	// keeping track of the total number of reservations and releases
	// for good bookkeeping/analysis
	Mempool_Stats op_stats;
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
	void * backend_memory;
	int num_devices;
	// array of num_devices
	Mempool * device_mempools;
	// has pool id SYSTEM_MEMPOOL_ID
	Mempool system_mempool;
	// this is to reference each unique lkey for
	// the different rdma devices referencing
	// the same device pool
	int num_ib_devices;
	// All threads will be submitting
	// requests to this fifo handled by single thread
	// across system
	// If this becomes a bottleneck, can change the intial
	// range configuration to be distributed across different
	// threads so that all threads can have copies of memory
	// struct that can work independently

	// This gets intialized when system is starting
	// The items in this fifo are Mem_Op * that are 
	// waiting to be populated (for reservations), or handled (for releasing)

	// There is a fifo per mempool so that we can have multiple memory threads
	// if we wanted and also to prevent producers from contending on a single fifo
	// lock even though their actions can happen concurrently
	// This is an array of fifos of size num_devices + 1
	// system memory fifo is at index num_devices and referred to as pool id = -1
	Fifo ** mem_op_fifos;
} Memory;

typedef struct mem_reservation {
	// just for bookkeeping / debugging purposes if many different
	// threads are making memory reservations/releases to be able to keep track
	int mem_client_id;
	// Either device id or -1 for system memory
	int pool_id;
	// if request cannot be satisfied on pool id
	// will retry on backup pool in priority order
	// specified by the ordering placing in backup_pool_ids
	int num_backup_pools;
	int backup_pool_ids[MEMORY_MAX_BACKUP_POOLS];

	// if reservation can be satisfied this gets populated with the correct pool
	int fulfilled_pool_id;
	// what the system requested. not necessarily a multiple of num_chunks
	uint64_t size_bytes;
	// these values obtaining by taking a range from the free_mem_ranges skiplist
	uint64_t num_chunks;
	uint64_t start_chunk_id;
	// mempool -> va_start_addr + (start_chunk_id * mempool -> chunk_size)
	void * buffer;
} Mem_Reservation;


// If we want to track statistics and benchmark...
typedef struct mem_op_timestamps {
	// THe client sets this upon receiving a memory op submission
	uint64_t submitted;
	// The client sets this value after being unblocked from producing
	uint64_t queued;
	// When the server starts processing sets this
	uint64_t start_op;
	// When the server finishes processing sets this
	// (right before setting is_complete)
	uint64_t finish_op;
} Mem_Op_Timestamps;

typedef struct mem_op {
	// this contains the meaningful data for operation
	Mem_Reservation * mem_reservation;
	// specifying reservation/release
	MemOpType type;
	// in case we want to have the server set error code
	MemOpStatus status;
	// If we want to track statistics and benchmark...
	Mem_Op_Timestamps timestamps;
	// The client will spin on this value
	// If we wanted we could make this a semaphore
	// to conserve wasted cycles, but probably perfer 
	// lowest latency possible
	bool is_complete;
} Mem_Op;






// SHOULD HAVE THIS HERE BUT FOR NOW FOR EASY BUILD
// PURPOSES LEAVING THIS WITHIN THE BACKEND_MEMORY FILE ITSELF!

// Memory * init_backend_memory(void * backend_memory_ref, BackendMemoryType backend_memory_type);

Fast_List_Node * insert_free_mem_range(Mempool * mempool, uint64_t start_chunk_id, uint64_t range_size);
Fast_List * remove_free_mem_range(Mempool * mempool, Mem_Range * mem_range, Fast_List * known_range_list);

// returns 0 upon success, otherwise error

// Assumes mem_reservation input has pool_id and size_bytes populated!
// Then populates mem_reservation with num_chunks, start_chunk_id and buffer
MemOpStatus do_reserve_memory(Mempool * mempool, Mem_Reservation * mem_reservation);

// returns 0 upon success, otherwise error
MemOpStatus do_release_memory(Mempool * mempool, Mem_Reservation * mem_reservation);


Memory * init_memory(uint64_t sys_mem_num_chunks, uint64_t sys_mem_chunk_size);

int init_mempool(Mempool * mempool, int pool_id, void * backing_memory, uint64_t num_chunks, uint64_t chunk_size);


#endif