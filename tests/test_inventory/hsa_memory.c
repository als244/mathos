#include "hsa_memory.h"


Agents * init_agents(int max_agents){
    Agents * agents = (Agents *) malloc(sizeof(Agents));
    agents -> n_agents = 0;
    agents -> agents = (hsa_agent_t *) malloc(max_agents * sizeof(hsa_agent_t));
    return agents;
}

hsa_status_t my_agent_callback(hsa_agent_t agent, void * data){
    Agents * agents = (Agents *) data;
    (agents -> agents)[agents -> n_agents] = agent;
    agents -> n_agents++;
    return HSA_STATUS_SUCCESS;
}

MemPools * init_mem_pools(int max_mem_pools){
	MemPools * mem_pools = (MemPools *) malloc(sizeof(MemPools));
	mem_pools -> n_mem_pools = 0;
	mem_pools -> mem_pools = (hsa_amd_memory_pool_t *) malloc(max_mem_pools * sizeof(hsa_amd_memory_pool_t));
	return mem_pools;
}

hsa_status_t my_mem_pool_callback(hsa_amd_memory_pool_t mem_pool, void * data){
	MemPools * mem_pools = (MemPools *) data;
	(mem_pools -> mem_pools)[mem_pools -> n_mem_pools] = mem_pool;
	mem_pools -> n_mem_pools++;
	return HSA_STATUS_SUCCESS;
}


Hsa_Memory * hsa_init_memory(){


	hsa_status_t hsa_status;
	const char * err;

	Hsa_Memory * hsa_memory = (Hsa_Memory *) malloc(sizeof(Hsa_Memory));

	if (hip_context == NULL){
		fprintf(stderr, "Error: malloc failed in init_hip_context\n");
		return NULL;
	}


	int max_agents = 2;
	Agents * agents = init_agents(max_agents);
	hsa_status = hsa_iterate_agents(&my_agent_callback, (void *) agents);
    if (hsa_status != HSA_STATUS_SUCCESS){
        hsa_status_string(hsa_status, &err);
        fprintf(stderr, "Error iterating agents: %s\n", err);
    }

    // HARDCODING BASED ON ROCMINFO
    
    hsa_agent_t gpu_agent = (agents -> agents)[1];
    hip_context -> agent = gpu_agent;
    hsa_agent_t cpu_agent = (agents -> agents)[0];
    hip_context -> cpu_agent = cpu_agent;


    int max_mem_pools = 3;
    MemPools * mem_pools = init_mem_pools(max_mem_pools);
    hsa_status = hsa_amd_agent_iterate_memory_pools(gpu_agent, &my_mem_pool_callback, (void *) mem_pools);
    if (hsa_status != HSA_STATUS_SUCCESS){
        hsa_status_string(hsa_status, &err);
        fprintf(stderr, "Error iterating memory pools: %s\n", err);
    }

    // HARDCODING BASEED ON ROCMINFO
    hip_context -> mem_pool = (mem_pools -> mem_pools)[0];

    // reset back to 0
    mem_pools -> n_mem_pools = 0;
    hsa_status = hsa_amd_agent_iterate_memory_pools(cpu_agent, &my_mem_pool_callback, (void *) mem_pools);
    if (hsa_status != HSA_STATUS_SUCCESS){
        hsa_status_string(hsa_status, &err);
        fprintf(stderr, "Error iterating memory pools: %s\n", err);
    }
    hip_context -> cpu_mem_pool = (mem_pools -> mem_pools)[0];

