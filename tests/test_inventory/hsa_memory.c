#include "hsa_memory.h"



hsa_status_t my_agent_callback(hsa_agent_t agent, void * data){
	Hsa_Memory * hsa_memory = (Hsa_Memory *) data;
	hsa_memory -> agents[hsa_memory -> n_agents] = agent;
	hsa_memory -> n_agents++;
	return HSA_STATUS_SUCCESS;
}


hsa_status_t my_mempool_callback(hsa_amd_memory_pool_t mem_pool, void * data){
	Hsa_Memory * hsa_memory = (Hsa_Memory *) data;
	int cur_agent_mempool_num = hsa_memory -> mempools_per_agent[hsa_memory -> n_agents];
	if (cur_agent_mempool_num < MAX_MEMPOOLS_PER_HSA_AGENT){
		(hsa_memory -> mempools[hsa_memory -> n_agents])[cur_agent_mempool_num] = mem_pool;
		hsa_memory -> mempools_per_agent[hsa_memory -> n_agents] = cur_agent_mempool_num + 1;
	}
	return HSA_STATUS_SUCCESS;
}


// Where max_agents is CPU + # of devices
Hsa_Memory * hsa_init_memory() {


	hsa_status_t hsa_status;
	const char * err;

	hsa_status = hsa_init();
	if (hsa_status != HSA_STATUS_SUCCESS){
		fprintf(stderr, "Error: hsa_init failed\n");
		return NULL;
	}

	Hsa_Memory * hsa_memory = (Hsa_Memory *) malloc(sizeof(Hsa_Memory));
	hsa_memory -> n_agents = 0;

	if (hsa_memory == NULL){
		fprintf(stderr, "Error: malloc failed to allocate hsa_memory container\n");
		return NULL;
	}

	hsa_memory -> agents = (hsa_agent_t *) malloc(MAX_HSA_AGENTS * sizeof(hsa_agent_t));
	if (hsa_memory -> agents == NULL){
		fprintf(stderr, "Error: malloc failed to allocate hsa agents container\n");
		return NULL;
	}

	hsa_status = hsa_iterate_agents(&my_agent_callback, (void *) hsa_memory);
	if (hsa_status != HSA_STATUS_SUCCESS){
		hsa_status_string(hsa_status, &err);
		fprintf(stderr, "Error iterating agents: %s\n", err);
		return NULL;
	}

	int total_agents = hsa_memory -> n_agents;

	hsa_memory -> mempools = (hsa_amd_memory_pool_t **) malloc(total_agents * sizeof(hsa_amd_memory_pool_t *));
	if (hsa_memory -> mempools == NULL){
		fprintf(stderr, "Error: malloc failed to allocate hsa mempools container\n");
		return NULL;
	}
	
	hsa_memory -> mempools_per_agent = (int *) malloc(total_agents * sizeof(int));
	if (hsa_memory -> mempools_per_agent == NULL){
		fprintf(stderr, "Error: malloc failed to allocate mempools per agent container\n");
		return NULL;
	}

	// reset n_agents for easy indexing within callback
	hsa_memory -> n_agents = 0;
	hsa_agent_t cur_agent;
	for (int i = 0; i < total_agents; i++){
		cur_agent = (hsa_memory -> agents)[i];
		(hsa_memory -> mempools)[i] = (hsa_amd_memory_pool_t *) malloc(MAX_MEMPOOLS_PER_HSA_AGENT * sizeof(hsa_amd_memory_pool_t));
		if ((hsa_memory -> mempools)[i] == NULL){
			fprintf(stderr, "Error: malloc failed to allocate mempool array for agent: %d\n", i);
			return NULL;
		}
		hsa_status = hsa_amd_agent_iterate_memory_pools(cur_agent, &my_mempool_callback, (void *) hsa_memory);
		if (hsa_status != HSA_STATUS_SUCCESS){
			hsa_status_string(hsa_status, &err);
			fprintf(stderr, "Error iterating memory pools: %s\n", err);
			return NULL;
		}
		hsa_memory -> n_agents++;
	}
	
	return hsa_memory;

}