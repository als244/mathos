#ifndef HSA_MEMORY_H
#define HSA_MEMORY_H


#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#include "common.h"
#include "memory.h"

#define MAX_HSA_AGENTS 33

#define HSA_CPU_AGENT_IND 0

#define MAX_MEMPOOLS_PER_HSA_AGENT 1
#define HSA_MAIN_MEMPOOL_IND 0


// Probably should convert this into system-wide struct (nothing specific to this hsa backend)

// However potentially want to leave opportunity for other backends to manage memory differently (i.e. physical chunks and mappings)
typedef struct hsa_user_page_table {
	// This is using our systems lingo
	// Num devices does not include CPU
	int num_devices;
	// Each of these are array of length num_devices
	uint64_t * num_chunks;
	uint64_t * chunk_size;
	// outer index is the device number (length num_devices)
	// inner index is the starting VA for entire device allocation
	// 	- can reference individual chunks by multiplying chunk_id * chunk_size for device
	void ** virt_memories;
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

// TEMPORARY SOLUTION FOR TESTING!!!!
void * hsa_reserve_memory(Hsa_Memory * hsa_memory, int device_id, uint64_t chunk_id);


// Bridge between backend memory and common interface
// called after all devices have been added
// not responsible for initialzing system mempool
Memory * init_backend_memory(Hsa_Memory * hsa_memory);



#endif