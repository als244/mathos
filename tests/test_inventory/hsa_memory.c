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


Hsa_User_Page_Table * hsa_init_user_page_table(int num_devices){

	Hsa_User_Page_Table * hsa_user_page_table = (Hsa_User_Page_Table *) malloc(sizeof(Hsa_User_Page_Table));
	if (hsa_user_page_table == NULL){
		fprintf(stderr, "Error: malloc failed to allocate hsa_user_page_table container\n");
		return NULL;
	}

	hsa_user_page_table -> num_devices = num_devices;

	hsa_user_page_table -> num_chunks = (uint64_t *) calloc(num_devices, sizeof(uint64_t));
	hsa_user_page_table -> chunk_size = (uint64_t *) calloc(num_devices, sizeof(uint64_t));
	hsa_user_page_table -> virt_memories = (void **) calloc(num_devices, sizeof(void *));

	if ((!hsa_user_page_table -> num_chunks) || (!hsa_user_page_table -> chunk_size)
			|| (!hsa_user_page_table -> virt_memories)){
		fprintf(stderr, "Error: malloc failed to allocate containers within hsa_user_page_table\n");
		return NULL;
	}

	return hsa_user_page_table;
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
	
	hsa_memory -> mempools_per_agent = (int *) calloc(total_agents, sizeof(int));
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

	Hsa_User_Page_Table * hsa_user_page_table = hsa_init_user_page_table(total_agents - 1);
	if (hsa_user_page_table == NULL){
		fprintf(stderr, "Error: failed to initialize hsa user page table\n");
		return NULL;
	}

	hsa_memory -> user_page_table = hsa_user_page_table;
	
	return hsa_memory;
}

// This should only be called on GPUs (i.e. agend_ids > 0)
int hsa_add_device_memory(Hsa_Memory * hsa_memory, int device_id, uint64_t num_chunks, uint64_t chunk_size) {

	hsa_status_t hsa_status;
	const char * err;

	// IN OUR SYSTEM'S TERMS, CPU WILL HAVE DEVCICE ID -1 and WONT BE IN USER_PAGE_TABLE
	int agent_id = device_id + 1;

	if ((agent_id < 0) || (agent_id >= hsa_memory -> n_agents)){
		fprintf(stderr, "Error: invalid device id of %d (= agent_id of %d)\n", device_id, agent_id);
		return -1;
	}

	hsa_amd_memory_pool_t hsa_mempool = (hsa_memory -> mempools)[agent_id][HSA_MAIN_MEMPOOL_IND];

	Hsa_User_Page_Table * hsa_user_page_table = hsa_memory -> user_page_table;

	(hsa_user_page_table -> num_chunks)[device_id] = num_chunks;
	(hsa_user_page_table -> chunk_size)[device_id] = chunk_size;



	// 1.) Allocate device memory

	printf("Allocating device memory...\n\tAgent ID: %d\n\tPool ID: %d\n\tNum Chunks %lu\n\tChunk Size: %lu\n\n",
				agent_id, device_id, num_chunks, chunk_size);


	// MUST BE A MULTIPLE OF ALLOC SIZE (either 4KB, 64KB, or 2MB)!!
	uint64_t total_mem_size_bytes = num_chunks * chunk_size;

	void * device_memory;


	// Flag options: 

	// Ref: https://github.com/ROCm/ROCR-Runtime/blob/master/src/inc/hsa_ext_amd.h

	//	- HSA_AMD_MEMORY_POOL_STANDARD: 
	//		standard HSA memory consistency model
	//	- HSA_AMD_MEMORY_POOL_PCIE_FLAG:
	//		fine grain memory type where ordering is per point-to-point connection
	//		atomic memory operations on these memory buffers are not guaranteed to be visible at system scope
	//	- HSA_AMD_MEMORY_POOL_CONTIGUOUS
	//		allocates physically contiguous memory


	// Use rocminfo CLI to see details easily


	uint32_t mempool_alloc_flags = HSA_AMD_MEMORY_POOL_STANDARD_FLAG;

	hsa_status = hsa_amd_memory_pool_allocate(hsa_mempool, total_mem_size_bytes, mempool_alloc_flags, &device_memory);
	if (hsa_status != HSA_STATUS_SUCCESS){
		hsa_status_string(hsa_status, &err);
		fprintf(stderr, "Error allocating device memory: %s\n", err);
		return NULL;
	}


	hsa_amd_memory_type_t mem_type = MEMORY_TYPE_PINNED;
	// CURRENTLY UNSUPPORTED
	uint64_t create_flags = 0;
	

	// 2.) Allow access to this memory for all other agents

	uint32_t num_agents = hsa_memory -> n_agents;
	hsa_agent_t * agents = hsa_memory -> agents;

	// currently reserved and must be null
	uint32_t * allow_agent_flags = NULL;

	hsa_status = hsa_amd_agents_allow_access(num_agents, agents, allow_agent_flags, device_memory);
	if (hsa_status != HSA_STATUS_SUCCESS){
		hsa_status_string(hsa_status, &err);
		fprintf(stderr, "Error allowing access: %s\n", err);
		return NULL;
	}


	(hsa_user_page_table -> virt_memories)[device_id] = device_memory;

	return 0;

}


int hsa_copy_to_host_memory(Hsa_Memory * hsa_memory, int src_device_id, void * src_addr, uint64_t length, void ** ret_contents) {


	hsa_status_t hsa_status;
	const char * err;



	hsa_agent_t cpu_agent = (hsa_memory -> agents)[HSA_CPU_AGENT_IND];

	int agent_id = src_device_id + 1;

	hsa_agent_t src_agent = (hsa_memory -> agents)[agent_id];

	// 

	void * contents = calloc(length, 1);

	// set agents to NULL to allow everyone access
	void * hsa_contents;
	hsa_status = hsa_amd_memory_lock(contents, length, NULL, 0, &hsa_contents);
	if (hsa_status != HSA_STATUS_SUCCESS){
		hsa_status_string(hsa_status, &err);
		fprintf(stderr, "Error calling memory lock: %s\n", err);
		return -1;
	}

	// ACTUALLY TRANSFER TO DESTINATION NOW
	// (if src != dest but on same node)
	//	1. create signal to indicate completation of memcpy
	//	2. call memcpy
	//	3. wait for signal to change

	// 1. create signal
	hsa_signal_t signal;

	// THIS WILL GET DECREMENTED ON COMPLETETION OF MEMCPY ASYNC
	// OR SET TO NEGATIVE IF ERROR
	hsa_signal_value_t signal_initial_value = 1;
	// means anyone can consume
	uint32_t num_consumers = 0;
	hsa_agent_t * consumers = NULL;

	hsa_status = hsa_signal_create(signal_initial_value, num_consumers, consumers, &signal);
	if (hsa_status != HSA_STATUS_SUCCESS){
		hsa_status_string(hsa_status, &err);
		fprintf(stderr, "Error creating signal: %s\n", err);
		return -1;
	}

	
	// 2. Now perform memcpy
	//		- Now both src and dest buffers are accessible by src and dest agents
	uint64_t num_dep_signals = 0;
	hsa_signal_t * dep_signals = NULL;
	hsa_signal_t completition_signal = signal;

	// PERFORM MEMCPY
	//	- need to use the hsa_dest_ptr because gpu needs to have access 
	hsa_status = hsa_amd_memory_async_copy(hsa_contents, cpu_agent, src_addr, src_agent, length, num_dep_signals, dep_signals, completition_signal);
	if (hsa_status != HSA_STATUS_SUCCESS){
		hsa_status_string(hsa_status, &err);
		fprintf(stderr, "Error calling memory_async_copy: %s\n", err);
		return -1;
	}

	// // WAIT UNTIL COMPLETETION SIGNAL CHANGES
	hsa_signal_value_t sig_val = signal_initial_value;
	while (sig_val == signal_initial_value){
		sig_val = hsa_signal_load_scacquire(completition_signal);
	}

	// set to negative if error
	if (sig_val < 0){
		fprintf(stderr, "Error: could not do memory copy, negative signal returned.\n");
		return -1;
	}

	*ret_contents = contents;

	return 0;
}



// TEMPORARY SOLUTION FOR TESTING!!!!

void * hsa_reserve_memory(Hsa_Memory * hsa_memory, int device_id, uint64_t chunk_id) {
		
	Hsa_User_Page_Table * hsa_user_page_table = hsa_memory -> user_page_table;

	uint64_t chunk_size = (hsa_user_page_table -> chunk_size)[device_id];

	void * starting_va = (hsa_user_page_table -> virt_memories)[device_id];

	void * chunk_va = (void *) ((uint64_t) starting_va + chunk_size * chunk_id);

	return chunk_va;
}



// Bridge between backend memory and common interface
// called after all devices have been added
// not responsible for initialzing system mempool
Memory * init_backend_memory(Hsa_Memory * hsa_memory) {

	Memory * memory = (Memory *) malloc(sizeof(Memory));
	if (memory == NULL){
		fprintf(stderr, "Error: malloc failed to allocate memory container\n");
		return NULL;
	}

	Hsa_User_Page_Table * hsa_user_page_table = hsa_memory -> user_page_table;

	int num_devices = hsa_user_page_table -> num_devices;

	memory -> num_devices = num_devices;

	Mempool * mempools = (Mempool *) malloc(num_devices * sizeof(Mempool));
	if (mempools == NULL){
		fprintf(stderr, "Error: malloc faile dto allocate mempools container\n");
		return NULL;
	}


	int ret;
	Mem_Range * full_range;
	uint64_t num_chunks;
	for (int i = 0; i < num_devices; i++){
		mempools[i].pool_id = i;

		num_chunks = (hsa_user_page_table -> num_chunks)[i];

		mempools[i].num_chunks = num_chunks;
		mempools[i].chunk_size = (hsa_user_page_table -> chunk_size)[i];
		mempools[i].capacity_bytes = num_chunks * mempools[i].chunk_size;
		// casting void * to uint64_t for convenience on pointer arithmetic
		mempools[i].va_start_addr = (uint64_t) ((hsa_user_page_table -> virt_memories)[i]);

		

		Fast_Table_Config * range_lists_table_config = save_fast_table_config(&hash_func_modulus_64, sizeof(uint64_t), sizeof(Fast_List *), 
												MEMORY_RANGE_LISTS_MIN_TABLE_SIZE, num_chunks, MEMORY_RANGE_LISTS_LOAD_FACTOR, MEMORY_RANGE_LISTS_SHRINK_FACTOR);


		Fast_Table * range_lists_table = (Fast_Table *) malloc(sizeof(Fast_Table));
		if (!range_lists_table){
			fprintf(stderr, "Error: malloc failed to allocate range_lists table container\n");
			return NULL;
		}

		ret = init_fast_table(range_lists_table, range_lists_table_config);
		if (ret){
			fprintf(stderr, "Error: unable to initialize range lists table\n");
			return NULL;
		}

		mempools[i].range_lists_table = range_lists_table;

		mempools[i].free_mem_ranges = init_fast_tree(true);

		if (!mempools[i].free_mem_ranges){
			fprintf(stderr, "Error: failure to initialize memory fast tree for device #%d\n", i);
			// fatal error
			return NULL;
		}

		// Now inserting the endpoints which will be used during memory release and merging

		uint64_t endpoint_table_min_size = MEMORY_ENDPOINT_MIN_TABLE_SIZE;
		if (endpoint_table_min_size > num_chunks){
			endpoint_table_min_size = num_chunks;
		}

		Fast_Table_Config * endpoint_table_config = save_fast_table_config(&hash_func_modulus_64, sizeof(uint64_t), sizeof(Mem_Range), 
												endpoint_table_min_size, num_chunks, MEMORY_ENDPOINT_LOAD_FACTOR, MEMORY_ENDPOINT_SHRINK_FACTOR);

		if (!endpoint_table_config){
			fprintf(stderr, "Error: failure to save memory endpoint table config\n");
			return NULL;
		}

		Fast_Table * endpoint_table = (Fast_Table *) malloc(sizeof(Fast_Table));
		if (!endpoint_table){
			fprintf(stderr, "Error: malloc failed to allocate fast table container for memory endpoint\n");
			return NULL;
		}

		ret = init_fast_table(endpoint_table, endpoint_table_config);
		if (ret){
			fprintf(stderr, "Error: failure to init fast table for memory endpoint\n");
			return NULL;
		}

		mempools[i].free_endpoints = endpoint_table;


		Fast_List_Node * full_range_ref = insert_free_mem_range(&(mempools[i]), 0, num_chunks);

		if (ret){
			fprintf(stderr, "Error: could not add initial free mem range for entire device of %d\n", i);
			return NULL;
		}

		// Need to add the original starting end_chunk_id

		uint64_t final_chunk_id = num_chunks - 1;

		Mem_Range full_mem_range;

		full_mem_range.range_size = num_chunks;
		full_mem_range.start_chunk_id_ref = full_range_ref;

		ret = insert_fast_table(mempools[i].free_endpoints, &final_chunk_id, &full_mem_range);
		if (ret){
			fprintf(stderr, "Error: failure to insert the final endpoint into free endpoints\n");
			return NULL;
		}

	}


	memory -> device_mempools = mempools;

	// Let a different function be responsible for initializating system mempool / cache / filesystem

	return memory;
}