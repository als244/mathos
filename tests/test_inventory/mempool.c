#include "mempool.h"


// returns 0 upon success, otherwise error
// Allocates a mem_reservation, determines number of chunks, acquires free_list lock, 
// checks to see if enough chunks if available (otherwise error), if so dequeues chunks from free list
// and puts them in the mem_reservation chunk_ids list
// Then populates ret_mem_reservation
int reserve_memory(Mempool * mempool, uint64_t size_bytes, Mem_Reservation * ret_mem_reservation) {

	fprintf(stderr, "Unimplmented error: reserve_memory\n");
	return -1;

}

// returns 0 upon success, otherwise error
// Acquires free list lock, iterates over all chunks in mem_reservation and enqueues them
// then frees the mem_reservation struct
int release_memory(Mempool * mempool, Mem_Reservation * mem_reservation) {

	fprintf(stderr, "Unimplmented error: release_memory\n");
	return -1;

}


// used for decision making about what pools to reserve on
// acquires free_lock and returns mempool -> free_cnt
int query_num_free_chunks(Mempool * mempool) {

	fprintf(stderr, "Unimplmented error: query_num_free_chunks\n");
	return -1;
}



