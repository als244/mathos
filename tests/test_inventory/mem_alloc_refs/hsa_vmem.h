#ifndef HSA_MEMORY_H
#define HSA_MEMORY_H


#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#include "common.h"

#define MAX_HSA_AGENTS 33

#define HSA_CPU_AGENT_IND 0

#define MAX_MEMPOOLS_PER_HSA_AGENT 1
#define HSA_MAIN_MEMPOOL_IND 0

typedef struct hsa_user_page_table {
	// This is using our systems lingo
	// Num devices does not include CPU
	int num_devices;
	// Each of these are array of length num_devices
	uint64_t * num_chunks;
	uint64_t * chunk_size;
	// This is an array where outer index is number of devices
	// Inner index is number of chunks
	hsa_amd_vmem_alloc_handle_t ** phys_chunks;

	// outer index is the device number (length num_devices)
	// inner index is the starting VA for the fully mapped
	// of all phys chunks
	void *** virt_memories;
	// Per device dma bufs
	//	They are exports of the phys chunks which have been mapped
	//	to a contiguous va space
	int ** dmabuf_fds;
	// array of length num_devices 
	//indicated offset within dmabuf of each
	// starting VA per 
	uint64_t ** dmabuf_offsets;


} Hsa_User_Page_Table;


typedef struct hsa_memory {
	// this includes the CPU as an agent
	int n_agents;
	hsa_agent_t * agents;

	int * mempools_per_agent;
	// Array of length n_agents (outer index)
	//	- inner index are the mempools assoicated with that agent
	//		- however using the 
	// Each agent may have multiple mempools, but choosing
	// the default mempool, which is the 0th mempool returned 
	// from the iterate_mempool callback per agent
	hsa_amd_memory_pool_t ** mempools;
	Hsa_User_Page_Table * user_page_table;
} Hsa_Memory;


Hsa_Memory * hsa_init_memory();


int hsa_add_device_memory(Hsa_Memory * hsa_memory, int device_id, uint64_t num_chunks, uint64_t chunk_size);

int hsa_copy_to_host_memory(Hsa_Memory * hsa_memory, int src_device_id, void * src_addr, uint64_t length, void ** ret_contents);

void * hsa_reserve_memory(Hsa_Memory * hsa_memory, int device_id, uint64_t size_bytes);



#endif