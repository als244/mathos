#include "memory.h"


// Skiplist key comparisons

// Using skiplist item instead of key so we can deal with comparing against NULL
// to make skiplist search implementation a bit cleaner

// Probably should change this for readability though...
int mem_range_skiplist_item_key_cmp(void * mem_range_skiplist_item, void * other_mem_range_skiplist_item) {
	if ((mem_range_skiplist_item == NULL) && (other_mem_range_skiplist_item == NULL)){
		return 0;
	}
	else if (mem_range_skiplist_item == NULL){
		return 1;
	}
	else if (other_mem_range_skiplist_item == NULL){
		return -1;
	}
	
	uint64_t num_chunks = ((Mem_Range *) (((Skiplist_Item *) mem_range_skiplist_item) -> key)) -> num_chunks;
	uint64_t other_num_chunks = ((Mem_Range *) (((Skiplist_Item *) other_mem_range_skiplist_item) -> key)) -> num_chunks;
	if (num_chunks == other_num_chunks){
		return 0;
	}
	else if (num_chunks > other_num_chunks){
		return 1;
	}
	else{
		return -1;
	}
}

// Skiplist_Item -> value_list deque comparisons
int mem_range_val_cmp(void * mem_range, void * other_mem_range) {
	uint64_t start_chunk_id = ((Mem_Range *) mem_range) -> start_chunk_id;
	uint64_t other_start_chunk_id = ((Mem_Range *) other_mem_range) -> start_chunk_id;
	if (start_chunk_id == other_start_chunk_id){
		return 0;
	}
	else if (start_chunk_id > other_start_chunk_id){
		return 1;
	}
	else{
		return -1;
	}
}



// returns 0 upon success, otherwise error
// Allocates a mem_reservation, determines number of chunks, acquires free_list lock, 
// checks to see if enough chunks if available (otherwise error), if so dequeues chunks from free list
// and puts them in the mem_reservation chunk_ids list
// Then populates ret_mem_reservation (assumes memory was already allocated for this struct, most liklely from stack and pointer ref)
int reserve_memory(Memory * memory, Mem_Reservation * mem_reservation){

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

	Skiplist * free_mem_ranges = mempool.free_mem_ranges;

	// Take greater or equal to the size
	Mem_Range target_range;
	target_range.num_chunks = req_chunks;

	Mem_Range * mem_range = NULL;
	int i = 0;
	
	// there may be race conditions (particularly at system startup time)
	// where an excess reservation is taken and another thread tries to reserve
	// but the excess as been re-inserted. Having a lock is too degredating for performance
	// so retry is better.

	// Different option would be to a set a maximum range size, so there is less contention for breaking
	// up large ranges at the beginning before system stabilizes

	// However it is "cleaner" to keep invariant of always merging contiguous ranges (less fragmentation)
	// and ability for maximum-sized allocations
	

	// take_item_skiplist() is thread-safe so we are guaranteed to not give two threads overlapping ranges
	while ((i < MEMORY_RESERVATION_ATTEMPT_CNT) && mem_range == NULL){
		mem_range = take_item_skiplist(free_mem_ranges, GREATER_OR_EQ_SKIPLIST, &target_range, NULL);
		i++;
	}

	// No allocation possible
	if (mem_range == NULL){
		mem_reservation -> num_chunks = 0;
		// no pointer in setting start chunk id here
		mem_reservation -> buffer = NULL;
		return -1;
	}


	// No need to acquire endpoint locks here. Because take_item_skiplist() is thread-safe, we know
	// that if there is a concurrent release that tries to take this range and merge, then it will 
	// return NULL because this range has atomically been acquired by this reservation. Thus the release
	// side will understand what happened and won't modify these endpoints values. 


	// Now split the range we took into the part this reservation needs and excess
	uint64_t reserved_chunks = mem_range -> num_chunks;
	uint64_t excess_chunks = reserved_chunks - req_chunks;

	uint64_t start_chunk_id = mem_range -> start_chunk_id;
	// unmark the start_chunk_id (which was previously free to now be reserved)
	// assert(mempool.endpoint_range_size[start_chunk_id] == reserved_chunks)
	mempool.endpoint_range_size[start_chunk_id] = 0;

	// now the last endpoint will have range size of excess_chunks
	// assert(mempool.endpoint_range_size[start_chunk_id + reserved_chunks - 1] == reserved_chunks)
	mempool.endpoint_range_size[start_chunk_id + reserved_chunks - 1] = excess_chunks;


	int ret = 0;

	// if there is excess we need to mark the spliced endpoint as 
	// associated to a free range and re-insert and 
	if (excess_chunks > 0){
		uint64_t excess_range_start_chunk_id = start_chunk_id + req_chunks;
		
		// mark new endpoint
		// assert(mempool.endpoint_range_size[excess_range_start_chunk_id] == 0)
		mempool.endpoint_range_size[excess_range_start_chunk_id] = excess_chunks;

		// resuse the allocated mem_range that was returned from taking and re-insert
		mem_range -> num_chunks = excess_chunks;
		mem_range -> start_chunk_id = excess_range_start_chunk_id;

		// should only fail on a OOM caused by allocating deque item
		// would be a fatal error
		ret = insert_item_skiplist(free_mem_ranges, mem_range, mem_range);
	}
	else{
		// We didn't reuse this range so we can free it now
		// TODO: have a slab allocator to deal with mem_ranges
		// this free (and the release's free upon both sides of merging) 
		// & the release's allocate (if no merging happens) go hand-in-hand
		// and could be wasteful
		free(mem_range);
	}


	mem_reservation -> num_chunks = req_chunks;
	mem_reservation -> start_chunk_id = start_chunk_id;
	// This is a pointer that can now be used by the caller
	mem_reservation -> buffer = (void *) (mempool.va_start_addr + start_chunk_id * mempool.chunk_size);

	return ret;
}

// returns 0 upon success, otherwise error
int release_memory(Memory * memory, Mem_Reservation * mem_reservation) {

	
	int pool_id = mem_reservation -> pool_id;

	Mempool mempool;
	
	if (pool_id == SYSTEM_MEMPOOL_ID){
		mempool = memory -> system_mempool;
	}
	else{
		mempool = (memory -> device_mempools)[pool_id];
	}

	Skiplist * free_mem_ranges = mempool.free_mem_ranges;


	uint64_t start_chunk_id = mem_reservation -> start_chunk_id;
	uint64_t num_chunks = mem_reservation -> num_chunks;
	uint64_t end_chunk_id = start_chunk_id + num_chunks - 1;




	// This is just a preliminary check. It might be the case that
	// we thought a merge was possible, but there was a race and a different thread
	// reserved that previously free range. This is OK, because when we call
	// take_skiplist_item() (which is thread-safe) it will return null in this 
	// scenario (because a different thread already grabbed that range) so we just won't merge 

	uint64_t left_merge_num_chunks = 0;
	uint64_t right_merge_num_chunks = 0;

	// There may be race conditions with concurrent releases which both release ranges that should be contiguous.
	// We should lock these values to maintain the invariant of contiguous ranges. Otherwise we wouldn't attempt 
	// a merge when we really should have. 

	// The race would occur when one thread's start_chunk_id - 1, could be equal to a different thread's end_chunk_id + 1
	// (And could happen on both sides / string of these races)

	// What we want in this scenario is for circular-advancement (each thread finishes before the next one can go)

	// Not strictly necessary to employ this (somewhat complex) locking scheme
	// (The temporary discontiguous ranges would eventually stabilize to a contiguous range),
	// BUT always maintaining the contiguous range invariant keeps the system "clean" and better for debugging. 
	// Very little perf overhead because of the fine-grained locks that should rarely have contention (but does consume more system memory
	// in proportion to the number of chunks in mempool).
	// In the rare cases of contention we need to be very careful about not causing a deadlock and destroying the entire system. 


	// NOTE: This is a variant of the classic dining-philospher's problem. 
	// The solution described below is similar to the "Resource Heirarchy" solution of dining-philosphers
	//	https://en.wikipedia.org/wiki/Dining_philosophers_problem


	// Lock aquisition order matters!


	// Acquire the locks in left to right order and release in reciprocal order at the end of function
	//	- However there is a problem. We want to lock the starting chunk id for the left endpoint (as we may alter this value), 
	//		but we don't know this value without first looking up immediately left, but in order to look up immediate left, we must acquire lock for it
	//	- Thus the order is reversed for left and self

	// Thus the order of acquiring is:
	// 1.) left neighbor end_chunk_id
	// 1b.) If (left neighbor range > 1):
	//		- left neightbor start_chunk_id
	// 2.) self end_chunk_id
	// 2b.) If (self range > 1):
	//		- self start_chunk_id
	// 3.) Right neighbor start chunk id
	// 3b.) If (right neighbor range > 1)
	//		- right neighbor end_chunk_id 


	// And will choose to release in exactly reciprocal order, though I believe this does not matter besides for systematic cleanliness
	// (the release order can help encourage total ordering even though not guaranteed) 

	// The lock acquisition order above enourages "higher" ranges to proceed before lower ranges


	// HAND-WAVEY PROOF:

	// Imagine what happens if there are three ranges concurrently releasing labeled/ordered 1, 2, 3
	// where 1 doesn't have a left neighbor and 3 doesn't have a right neighbor 
	//	(The logic still holds if they do and the string of concurrency is larger) 
	// and that all ranges > 1 chunk
	//	(logic still holds if == 1 because that is a subset of the more general case) 


	// 1 doesn't have a left neighbor so it first acquires it's end id
	// 2 first tries to acquire it's left neighbor end_chunk_id (which is 1's end id)
	// 3 first tries to acquire it's left neighbor end_chunk_id (which is 2's end id)



	// Is there potential deadlock?

	// I will try to demonstrate that given the acquisition order labeled above that this is deadlock-free.

	// Either 1 or 2 will advance and the other won't continue until after the completion of other 

	// 3 vs. 2 case:

	// After 2 acquires 1's locks, it is racing against 3 now, both trying to acquire 2's locks. 

	// a.) If 3 wins this race: 
	
	// 2 locked out from acquiring self because 3 grabbed it
	// Now 3 will acquire its self lock which is available because 2 is locked out. 3 doesn't have a right neighbor
	// so it is done and will finish. 
	// After it finishes then it will release it's self locks then 2's locks. Remember 2 is still holding onto 1's locks, so it needs to finish before 1. 
	// While 1 is still locked out 2 will grab its own locks and then 3's and finish. 
	// When two finishes then then it will release the 1's locks so then 1 can continue and finish properly

	// b.) If 2 wins this race:
	
	// 3 locked out from acquiring 2 (left neighbor).
	// Now 2 can advance and acquire 3's locks. When two finishes, it first releases 3's locks, then self locks, then 1's locks.
	// At this point there is potential for 1 to grab it's own locks and then there is race of 3 vs. 1 to grab 2's locks. 



	// 3 vs. 1 case:

	// After 1 acquires it's self locks, it will be racing against 3 to acquire 2's locks (while 2 is locked out)
	
	// a.) If 3 wins this race:

	// 3 will finish and then release it's locks and 2's locks. Remember 1 is still holding on to self locks and 2 is locked out
	// so it needs to finish before 2 can start. After 3 releases 2's locks 1 will grab them and then will finish. Then 1 will release the locks
	// and 2 can advance and finish

	// b.) If 1 wins this race:

	// 1 will now grab 3's locks, while 3 is locked out 




	if (start_chunk_id != 0){
		pthread_mutex_lock(&(mempool.endpoint_locks[start_chunk_id - 1]));
		left_merge_num_chunks = mempool.endpoint_range_size[start_chunk_id - 1];
		if (left_merge_num_chunks > 1){
			pthread_mutex_lock(&(mempool.endpoint_locks[start_chunk_id - left_merge_num_chunks]));
		}
	}

	// need to lock ourselves
	pthread_mutex_lock(&(mempool.endpoint_locks[start_chunk_id]));
	// for num_chunks == 1 we shouldn't try to acquire same lock twice
	if (num_chunks > 1){
		pthread_mutex_lock(&(mempool.endpoint_locks[end_chunk_id]));
	}

	if (end_chunk_id != mempool.num_chunks - 1){
		pthread_mutex_lock(&(mempool.endpoint_locks[end_chunk_id + 1]));
		right_merge_num_chunks = mempool.endpoint_range_size[end_chunk_id + 1];
		if (right_merge_num_chunks > 1){
			pthread_mutex_lock(&(mempool.endpoint_locks[end_chunk_id + right_merge_num_chunks]));
		}
	}


	// We have acquired all the necessary locks to attempt a clean merge, while neighbors won't be able 
	// to merge at the same time now


	Mem_Range * left_range = NULL;
	Mem_Range * right_range = NULL;
	
	// Attempt to merge 
	//	(race condition with reservation function is OK/desired, so these take_items might return NULL)
	if (left_merge_num_chunks != 0){
		Mem_Range target_left_range;
		target_left_range.num_chunks = left_merge_num_chunks;
		// The starting chunk number for the left range is this ranges starting id - size of the left range
		target_left_range.start_chunk_id = start_chunk_id - left_merge_num_chunks;
		left_range = take_item_skiplist(free_mem_ranges, EQ_SKIPLIST, &target_left_range, &target_left_range);
	}
	if (right_merge_num_chunks != 0){
		Mem_Range target_right_range;
		target_right_range.num_chunks = right_merge_num_chunks;
		target_right_range.start_chunk_id = end_chunk_id + 1;
		right_range = take_item_skiplist(free_mem_ranges, EQ_SKIPLIST, &target_right_range, &target_right_range);
	}


	uint64_t merged_range_num_chunks;
	uint64_t merged_range_start_chunk_id;

	Mem_Range * merged_range;

	int ret;

	// we are merging on both sides
	if ((left_range != NULL) && (right_range != NULL)){
		merged_range_num_chunks = left_merge_num_chunks + num_chunks + right_merge_num_chunks;
		merged_range_start_chunk_id = start_chunk_id - left_merge_num_chunks;

		// removing middle endpoints of previously free ranges
		mempool.endpoint_range_size[start_chunk_id - 1] = 0;
		mempool.endpoint_range_size[end_chunk_id + 1] = 0;

		// reusing one of the returned ranges and preparing to insert
		merged_range = left_range;

		// can free the right range now
		free(right_range);

	}
	// merging on the left
	else if (left_range != NULL){
		merged_range_num_chunks = left_merge_num_chunks + num_chunks;
		merged_range_start_chunk_id = start_chunk_id - left_merge_num_chunks;

		// removing the previous endpoint that is now in the middle
		mempool.endpoint_range_size[start_chunk_id - 1] = 0;

		// reusing one of the returned ranges and preparing to insert
		merged_range = left_range;


	}
	// merging on the right
	else if (right_range != NULL){
		
		merged_range_num_chunks = num_chunks + right_merge_num_chunks;
		merged_range_start_chunk_id = start_chunk_id;

		// removing the previous endpoint that is now in the middle
		mempool.endpoint_range_size[end_chunk_id + 1] = 0;

		// reusing one of the returned ranges and preparing to insert
		merged_range = right_range;
	}
	// no merging
	else{
		merged_range_num_chunks = num_chunks;
		merged_range_start_chunk_id = start_chunk_id;

		// updating new endpoints
		mempool.endpoint_range_size[start_chunk_id] = num_chunks;
		mempool.endpoint_range_size[end_chunk_id] = num_chunks;

		merged_range = (Mem_Range *) malloc(sizeof(Mem_Range));
		// would be a fatal error
		if (merged_range == NULL){
			fprintf(stderr, "Error: malloc failed to allocate a mem_range during releasing\n");
			ret = -1;
		}
	}

	// updating new endpoints
	mempool.endpoint_range_size[merged_range_start_chunk_id] = merged_range_num_chunks;
	mempool.endpoint_range_size[merged_range_start_chunk_id + merged_range_num_chunks - 1] = merged_range_num_chunks;


	// setting the values for merged range and inserting
	
	// would only be null if malloc failure so should be fatal but for cleanliness
	// still going to release locks and not immediately fail over
	if (merged_range != NULL){
		merged_range -> num_chunks = merged_range_num_chunks;
		merged_range -> start_chunk_id = merged_range_start_chunk_id;

		// should only fail on a OOM caused by allocating deque item
		// would be a fatal error
		ret = insert_item_skiplist(free_mem_ranges, merged_range, merged_range);
		if (ret != 0){
			fprintf(stderr, "Error: inserting merged range failed\n");
		}
	}


	// now release locks
	//	- doing it in reciprocal order for cleanliness but i don't think this should matter

	if (end_chunk_id != mempool.num_chunks - 1){
		if (right_merge_num_chunks > 1){
			pthread_mutex_unlock(&(mempool.endpoint_locks[end_chunk_id + right_merge_num_chunks]));
		}
		pthread_mutex_unlock(&(mempool.endpoint_locks[end_chunk_id + 1]));
	}

	if (num_chunks > 1){
		pthread_mutex_unlock(&(mempool.endpoint_locks[end_chunk_id]));
	}
	pthread_mutex_unlock(&(mempool.endpoint_locks[start_chunk_id]));

	if (start_chunk_id != 0){
		if (left_merge_num_chunks > 1){
			pthread_mutex_unlock(&(mempool.endpoint_locks[start_chunk_id - left_merge_num_chunks]));
		}
		pthread_mutex_unlock(&(mempool.endpoint_locks[start_chunk_id - 1]));
	}


	return ret;
}

