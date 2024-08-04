#ifndef HSA_MEMORY_H
#define HSA_MEMORY_H

#include "common.h"

#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#define MAX_HSA_AGENTS 33
#define MAX_MEMPOOLS_PER_HSA_AGENT 1
#define HSA_CPU_AGENT_ID 0



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
} Hsa_Memory;


Hsa_Memory * hsa_init_memory();

#endif