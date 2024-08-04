#ifndef HSA_MEMORY_H
#define HSA_MEMORY_H

#include "common.h"

#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>


#define HSA_CPU_AGENT_ID 0
#define HSA_AGENT_MAIN_MEMPOOL_ID 0


typedef struct hsa_memory {
	// this includes the CPU as an agent
	int n_agents;
	hsa_agent_t * agents;
	// FOR NOW forcing a 1-1 relationship between agents
	// and mempools

	// Each agent may have multiple mempools, but choosing
	// the default mempool, denoted by HSA_AGENT_MAIN_MEMPOOL_ID
	hsa_amd_memory_pool_t * mempools;
} Hsa_Memory;

