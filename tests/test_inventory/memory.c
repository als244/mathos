#include "memory.h"


Fast_List_Node * insert_free_mem_range(Mempool * mempool, uint64_t start_chunk_id, uint64_t range_size){

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


Fast_List * remove_free_mem_range(Mempool * mempool, Mem_Range * mem_range, Fast_List * known_fast_list){

	int ret;

	Fast_Table * range_lists_table = mempool -> range_lists_table;
	Fast_Tree * free_mem_ranges = mempool -> free_mem_ranges;

	uint64_t range_size = mem_range -> range_size;
	
	Fast_List * range_list = NULL;

	// can optionally pass in the known list to avoid lookup
	// this can happen if left merge size == right merge size and we want to look up both
	if (unlikely(known_fast_list)){
		range_list = known_fast_list;
	}
	else{
		find_fast_table(range_lists_table, &range_size, false, (void **) &range_list);

		// should never happen
		if (unlikely(!range_list)){
			fprintf(stderr, "Error: couldn't find range list when trying to remove\n");
			return NULL;
		}
	}

	
	Fast_List_Node * old_range_ref = mem_range -> start_chunk_id_ref;
	remove_node_fast_list(range_list, old_range_ref);

	// if this list is empty now, we need to remove the list and also remove from the tree
	if (range_list -> cnt == 0){

		

		// a.) remove from tree
		Fast_List * tree_prev_list = NULL;
		ret = remove_fast_tree(free_mem_ranges, &range_size, &tree_prev_list);

		// should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: could not remove range size from fast tree after becoming empty\n");
			return NULL;
		}

		// assert treve_prev_list == range_list

		// b.) remove from range lists table
		Fast_List * table_prev_list = NULL;
		
		ret = remove_fast_table(range_lists_table, &range_size, (void **) &table_prev_list);

		// should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: could not remove left merged range size from fast table after becoming empty\n");
			return NULL;
		}

		// assert table_prev_list == tree_prev_list == range_list

		// c.) destroy the list
		destroy_fast_list(range_list);

		// d.) return null
		return NULL;
	}

	return range_list;
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

		ret = remove_fast_table(mempool.range_lists_table, &reserved_chunks, (void **) &table_prev_list);
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
		excess_range_ref = insert_free_mem_range(&mempool, excess_range_start_chunk_id, excess_chunks);
		if (unlikely(!excess_range_ref)){
			fprintf(stderr, "Error: unable to add excess memory range of start id: %lu, size: %lu\n", excess_range_start_chunk_id, excess_chunks);
			return -1;
		}

		// now need to modify the original endpoint's mem_range

		// we are modifying directly in the endpoint table

		Mem_Range * mem_range_ref = NULL;

		uint64_t reserved_end_chunk_id = start_chunk_id + reserved_chunks - 1;

		find_fast_table(free_endpoints, &reserved_end_chunk_id, false, (void **) &mem_range_ref);

		// this should never happen
		if (unlikely(!mem_range_ref)){
			fprintf(stderr, "Error: unable to find the end_chunk_id endpoint of %lu that needs to be modified after reservation\n", reserved_end_chunk_id);
			return -1;
		}

		mem_range_ref -> range_size = excess_chunks;
		mem_range_ref -> start_chunk_id_ref = excess_range_ref;
	}

	




	// now need to remove the starting endpoint
	// (and the end chunk id endpoint if there are no excess chunks)


	Mem_Range mem_range;
	ret = remove_fast_table(free_endpoints, &start_chunk_id, (void **) &mem_range);
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

		ret = remove_fast_table(free_endpoints, &end_chunk_id, (void **) &mem_range);

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

	uint64_t start_chunk_id = mem_reservation -> start_chunk_id;

	uint64_t range_size = mem_reservation -> num_chunks;

	uint64_t end_chunk_id = start_chunk_id + range_size- 1;


	Fast_Table * free_endpoints = mempool.free_endpoints;

	Mem_Range left_mem_range;
	Mem_Range right_mem_range;

	Mem_Range * left_merge = NULL;
	Mem_Range * right_merge = NULL;
	// If the surrounding -1 and +1 endpoints are free, we want to remove
	// them and merge

	if (start_chunk_id > 0){
		uint64_t left_endpoint = start_chunk_id - 1;
		ret = remove_fast_table(free_endpoints, &left_endpoint, (void **) &left_mem_range);
		if (ret == 0){
			left_merge = &left_mem_range;
		}
	}

	if (end_chunk_id < mempool.num_chunks - 1){
		uint64_t right_endpoint = end_chunk_id + 1;
		ret = remove_fast_table(free_endpoints, &right_endpoint, (void **) &right_mem_range);
		if (ret == 0){
			right_merge = &right_mem_range;
		}
	}


	// If we need to merge, then 
	// we need get new start_id and new size
	// and modify endpoint (if right merge exists)
	// or add endpoint (if just left endpoint or no merge)

	uint64_t new_size, new_start_id;
	uint64_t left_size = 0;
	uint64_t right_size = 0;
	if (left_merge && right_merge){
		left_size = left_merge -> range_size;
		right_size = right_merge -> range_size;
		new_size = left_size + range_size + right_size;
		new_start_id = left_merge -> start_chunk_id_ref -> item;
	}
	else if (left_merge){
		left_size = left_merge -> range_size;
		new_size = left_size + range_size;
		new_start_id = left_merge -> start_chunk_id_ref -> item;
	}
	else if (right_merge){
		right_size = right_merge -> range_size;
		new_size = range_size + right_size;
		new_start_id = start_chunk_id;
	}
	else{
		new_size = range_size;
		new_start_id = start_chunk_id;
	}


	// Now need to deal with removing the prior ranges
	Fast_Table * range_lists_table = mempool.range_lists_table;
	Fast_Tree * free_mem_ranges = mempool.free_mem_ranges;

	Fast_List * left_range_list = NULL;
	Fast_List * right_range_list = NULL;

	Fast_List_Node * old_range_ref;


	if (left_merge){

		left_range_list = remove_free_mem_range(&mempool, left_merge, NULL);

		// when we add free mem range it will insert the start id to free_endpoints
		// table, but if we had a left merge with size > 1 then this entry will already
		// exist and we need to remove it (if size 1 then we already removed it above)
		if (left_size > 1){
			ret = remove_fast_table(free_endpoints, &new_start_id, (void **) &left_mem_range);
			// should never happen
			if (unlikely(ret)){
				fprintf(stderr, "Error: failure to remove left endpoint from table\n");
				return -1;
			}

		}

		// if left_range_list == NULL => 
		//	we removed left_merge -> range_size from range_lists_table + free_mem_ranges
		//		and destroyed the list
	}

	if (right_merge){

		// accelerate by not looking up if left was same size as right
		// this case implies the left_range_list has not been deleted because there
		// was another element of that size (the right range) when the left's was removed from list
		if (unlikely(left_merge && (right_size == left_size))){
			right_range_list = remove_free_mem_range(&mempool, right_merge, left_range_list);
		}
		else{
			right_range_list = remove_free_mem_range(&mempool, right_merge, NULL);	
		}

		// if right_range_list == NULL => 
		//	we removed right_merge -> range_size from range_lists_table + free_mem_ranges
		//		and destroyed the list
	}



	// Inserting free mem range causes:
	//	a.) Adds new_start_id to the range_list associated with new_size:
	//			- if this list did not exist, then creates list and adds to free_mem_ranges tree
	//	b.) Adds new_start_id to free_endpoints table
	// 	c.) Returns the pointer to the node within the range_list that we can then use when modifying
	//		the right endpoint 

	Fast_List_Node * merged_range_ref = insert_free_mem_range(&mempool, new_start_id, new_size);
	// should never happen
	if (unlikely(!merged_range_ref)){
		fprintf(stderr, "Error: failure to add merged range\n");
		return -1;
	}

	// now need to modify the right endpoint to point to this reference

	Mem_Range merged_range;
	merged_range.range_size = new_size;
	merged_range.start_chunk_id_ref = merged_range_ref;


	uint64_t new_right_endpoint = new_start_id + new_size - 1;

	// if we didn't merge to the right we need to add right endpoint
	if (!right_merge){

		ret = insert_fast_table(free_endpoints, &new_right_endpoint, &merged_range);

		// should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: failure to insert new right endpoint\n");
			return -1;
		}
	}
	else{

		// if the right merged endpoint was size 1 then we already removed 
		// it originally and need to add it back
		if (right_size == 1){

			ret = insert_fast_table(free_endpoints, &new_right_endpoint, &merged_range);
			
			// should never happen
			if (unlikely(ret)){
				fprintf(stderr, "Error: failure to insert new right endpoint\n");
				return -1;
			}
		}
		// otherwise we just need to modify it 
		else{

			Mem_Range * mem_range_ref = NULL;
			
			find_fast_table(free_endpoints, &new_right_endpoint, false, (void **) &mem_range_ref);

			// should never happen
			if (unlikely(!mem_range_ref)){
				fprintf(stderr, "Error: failure to find right endpoint in table when expected\n");
				return -1;
			}

			// modifying the previous range to have merged values
			mem_range_ref -> range_size = new_size;
			mem_range_ref -> start_chunk_id_ref = merged_range_ref;

		}


	}

	return 0;
}

