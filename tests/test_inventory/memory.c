#include "memory.h"


Fast_List_Node * add_free_mem_range(Mempool * mempool, uint64_t start_chunk_id, uint64_t range_size){

	int ret;

	// check if the range size exists or not

	Fast_List * range_list = NULL;
	find_fast_table(mempool -> range_lists_table, &range_size, false, (void **) &range_list);

	if (!range_list){

		// now we need to create a new list and add it to the free_mem_ranges tree

		// we know this list can contain at most max_elements
		// but we can just use default setting for list buffer
		// size to prevent excess memory usage
		// uint64_t max_elements = mempool -> num_chunks / range_size;

		range_list = init_fast_list(MEMORY_RANGE_LIST_DEFAULT_BUFFER_CAPACITY);
		if (unlikely(!range_list)){
			fprintf(stderr, "Error: failure to initialize range list for size: %lu\n", range_size);
			return NULL;
		}

		ret = insert_fast_table(mempool -> range_lists_table, &range_size, &range_list);
		if (unlikely(ret)){
			fprintf(stderr, "Error: failure to insert range list into range list table\n");
			return NULL;
		}


		Fast_List * prev_list = NULL;
		ret = insert_fast_tree(mempool -> free_mem_ranges, range_size, range_list, false, (void **) &prev_list);
		
		// should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: could not insert into free mem ranges list. Had a previous value of: %p\n", (void *) prev_list);
			return NULL;
		}
	}

	Fast_List_Node * start_chunk_id_ref = insert_fast_list(range_list, start_chunk_id);
	if (unlikely(!start_chunk_id_ref)){
		fprintf(stderr, "Error: failure to insert start chunk id into range list\n");
	}

	// now insert new starting which will be used for merging

	Mem_Range mem_range;
	mem_range.range_size = range_size;
	mem_range.start_chunk_id_ref = start_chunk_id_ref;

	ret = insert_fast_table(mempool -> free_endpoints, &start_chunk_id, &mem_range);
	if (ret){
		fprintf(stderr, "Error: failure to insert into free endpoints\n");
		return NULL;
	}

	// the end_chunk_id endpoint is modified within reserve_reservation
	// to match the correct size now

	// at intialization the end_chunk_id of num_chunks - 1 is inserted, all
	// other times will just be modifying others


	return start_chunk_id_ref;
}


// returns 0 upon success, otherwise error
// Allocates a mem_reservation, determines number of chunks, acquires free_list lock, 
// checks to see if enough chunks if available (otherwise error), if so dequeues chunks from free list
// and puts them in the mem_reservation chunk_ids list
// Then populates ret_mem_reservation (assumes memory was already allocated for this struct, most liklely from stack and pointer ref)
int reserve_memory(Memory * memory, Mem_Reservation * mem_reservation){

	int ret;

	int pool_id = mem_reservation -> pool_id;
	uint64_t size_bytes = mem_reservation -> size_bytes;

	Mempool mempool;
	
	if (pool_id == SYSTEM_MEMPOOL_ID){
		mempool = memory -> system_mempool;
	}
	else{
		mempool = (memory -> device_mempools)[pool_id];
	}
	
	uint64_t chunk_size = mempool.chunk_size;
	// ensure we request enough chunks to satisfy request
	uint64_t req_chunks = MY_CEIL(size_bytes, chunk_size);

	Fast_Tree * free_mem_ranges = mempool.free_mem_ranges;

	Fast_Tree_Result mem_req_next;

	ret = search_fast_tree(free_mem_ranges, req_chunks, FAST_TREE_EQUAL_OR_NEXT, &mem_req_next);

	if (unlikely(ret)){
		fprintf(stderr, "Error: no range >= %lu chunks\n", req_chunks);
		return -1;
	}

	uint64_t reserved_chunks = mem_req_next.key;

	Fast_List * reserved_chunks_list = mem_req_next.value;

	// should never happen
	if (unlikely(!reserved_chunks_list)){
		fprintf(stderr, "Error: fast tree returned a key of %lu reserved chunks, but no list attached\n", reserved_chunks);
		return -1;
	}



	uint64_t start_chunk_id;
	ret = take_fast_list(reserved_chunks_list, &start_chunk_id);

	// should never happen
	if (unlikely(ret)){
		fprintf(stderr, "Error: expected a value in the fast list after getting result from tree, but empty list\n");
		return -1;
	}

	// if that was the last element in memory range then list will be 
	// of cnt = 0 and we should remove the list from table and tree

	if (reserved_chunks_list -> cnt == 0){

		Fast_List * tree_prev_list = NULL;

		ret = remove_fast_tree(free_mem_ranges, reserved_chunks, (void **) &tree_prev_list);
		// should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: reserved range size was not in the fast tree\n");
			return -1;
		}

		// assert tree_prev_list == reserved_chunks_list


		// also should remove the range lists table
		Fast_List * table_prev_list = NULL;

		ret = remove_fast_table(mempool.range_lists_table, &reserved_chunks, false, (void **) &table_prev_list);

		// should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: reserved range size was not in fast table\n");
			return -1;
		}

		// assert table_prev_list == tree_prev_list == reserved_chunks_list

		destroy_fast_list(reserved_chunks_list);

	}





	Fast_Table * free_endpoints = mempool.free_endpoints;

	uint64_t excess_chunks = reserved_chunks - req_chunks;

	// if there is excess we need to mark the spliced endpoint as 
	// associated to a free range and re-insert and 
	if (excess_chunks > 0){
		
		uint64_t excess_range_start_chunk_id = start_chunk_id + req_chunks;
		
		Fast_List_Node * excess_range_ref;
		excess_range_ref = add_free_mem_range(&mempool, excess_range_start_chunk_id, excess_chunks);
		if (unlikely(!excess_range_ref)){
			fprintf(stderr, "Error: unable to add excess memory range of start id: %lu, size: %lu\n", excess_range_start_chunk_id, excess_chunks);
			return -1;
		}

		// now need to modify the original endpoint's mem_range

		// we are modifying directly in the endpoint table

		Mem_Range * mem_range_ref;

		uint64_t reserved_end_chunk_id = start_chunk_id + reserved_chunks - 1;

		ret = find_fast_table(free_endpoints, &reserved_end_chunk_id, false, (void **) &mem_range_ref);

		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: unable to find the end_chunk_id endpoint of %lu that needs to be modified after reservation\n", reserved_end_chunk_id);
			return -1;
		}

		mem_range_ref -> range_size = excess_chunks;
		mem_range_ref -> start_chunk_id_ref = excess_range_ref;
	}

	




	// now need to remove the starting endpoint
	// (and the end chunk id endpoint if there are no excess chunks)


	Mem_Range * mem_range;

	ret = remove_fast_table(free_endpoints, &start_chunk_id, false, (void **) &mem_range);

	// should never happen
	if (unlikely(ret)){
		fprintf(stderr, "Error: unable to remove starting endpoint of %lu\n", start_chunk_id);
		return -1;
	}

	// assert mem_range -> range_size = reserved_size


	// if the end_chunk_id endpoint is no longer an endpoint (i.e. there were no excess chunks)
	// then we need to remove this too
	
	if ((excess_chunks == 0) && (req_chunks > 1)){

		uint64_t end_chunk_id = start_chunk_id + req_chunks - 1;

		ret = remove_fast_table(free_endpoints, &end_chunk_id, false, (void **) &mem_range);

		// assert mem_range -> range_size = reserved_size == req_size

		// should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: unable to remove ending endpoint of %lu\n", end_chunk_id);
			return -1;
		}


	}

	mem_reservation -> num_chunks = req_chunks;
	mem_reservation -> start_chunk_id = start_chunk_id;
	// This is a pointer that can now be used by the caller
	mem_reservation -> buffer = (void *) (mempool.va_start_addr + start_chunk_id * mempool.chunk_size);

	return 0;





}

// returns 0 upon success, otherwise error
int release_memory(Memory * memory, Mem_Reservation * mem_reservation) {
	
	int ret;

	int pool_id = mem_reservation -> pool_id;

	Mempool mempool;
	
	if (pool_id == SYSTEM_MEMPOOL_ID){
		mempool = memory -> system_mempool;
	}
	else{
		mempool = (memory -> device_mempools)[pool_id];
	}

	Fast_Tree * free_mem_ranges = mempool.free_mem_ranges;


	uint64_t start_chunk_id = mem_reservation -> start_chunk_id;
	uint64_t num_chunks = mem_reservation -> num_chunks;
	uint64_t end_chunk_id = start_chunk_id + num_chunks - 1;


	uint64_t left_merge_num_chunks = 0;
	uint64_t right_merge_num_chunks = 0;


	// LOOKUP ENDPOINT TABLE HERE!



	return ret;
}

