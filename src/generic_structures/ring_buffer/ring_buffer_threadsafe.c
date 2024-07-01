#include "ring_buffer.h"


// All methods require ther ring_buffer lock


Ring_Buffer * init_ring_buffer(uint64_t capacity, uint64_t item_size_bytes) {
	Ring_Buffer * ring_buffer = (Ring_Buffer *) malloc(sizeof(Ring_Buffer));
	if (ring_buffer == NULL){
		fprintf(stderr, "Error: malloc failed for ring buffer\n");
		return NULL;
	}

	ring_buffer -> capacity = capacity;
	ring_buffer -> item_size_bytes = item_size_bytes;
	ring_buffer -> read_ind = 0;
	ring_buffer -> write_ind = 0;

	void * items = (void *) calloc(capacity, item_size_bytes);

	if (items == NULL){
		fprintf(stderr, "Error: malloc failed for ring buffer items\n");
		return NULL;
	}

	ring_buffer -> items = items;

	pthread_mutex_init(&(ring_buffer -> index_lock), NULL);

	pthread_mutex_t * slot_locks = (pthread_mutex_t *) malloc(capacity * sizeof(pthread_mutex_t));
	for (uint64_t i = 0; i < capacity; i++){
		pthread_mutex_init(&(slot_locks[i]), NULL);
	}

	return ring_buffer;

}


int is_empty(Ring_Buffer * ring_buffer){
	pthread_mutex_lock(&(ring_buffer -> index_lock));
	int is_empty = ring_buffer -> write_ind == ring_buffer -> read_ind;
	pthread_mutex_unlock(&(ring_buffer -> index_lock));
	return is_empty;
}

int is_full(Ring_Buffer * ring_buffer){
	pthread_mutex_lock(&(ring_buffer -> index_lock));
	int is_full = (ring_buffer -> write_ind + 1) % ring_buffer -> capacity == ring_buffer -> read_ind;
	pthread_mutex_unlock(&(ring_buffer -> index_lock));
	return ;
}

uint64_t get_write_addr(Ring_Buffer * ring_buffer, uint64_t n_advance){
	if (is_full(ring_buffer)){
		return 0;
	}
	void * items = ring_buffer -> items;
	pthread_mutex_lock(&(ring_buffer -> index_lock));
	uint64_t index = (ring_buffer -> write_ind + n_advance) % (ring_buffer -> capacity);
	pthread_mutex_unlock(&(ring_buffer -> index_lock));
	uint64_t write_addr = (uint64_t) (items + index);
	return write_addr;
}


int insert_item_ring(Ring_Buffer * ring_buffer, void * item) {
	if (is_full(ring_buffer)){
		return -1;
	}

	void * items = ring_buffer -> items;

	pthread_mutex_lock(&(ring_buffer -> index_lock));
	uint64_t index = ring_buffer -> write_ind;
	ring_buffer -> write_ind = (index + 1) % (ring_buffer -> capacity);
	pthread_mutex_unlock(&(ring_buffer -> index_lock));

	pthread_mutex_lock(&((ring_buffer -> slot_locks)[index]));
	memcpy(items + index, item, ring_buffer -> item_size_bytes);
	pthread_mutex_unlock(&((ring_buffer -> slot_locks)[index]));

	return 0;
}


int get_next_free_slot_ring(Ring_Buffer * ring_buffer, void * ret_item) {

	if (is_empty(ring_buffer)){
		return -1;
	}

	void * items = ring_buffer -> items;

	uint64_t index = ring_buffer -> read_ind;
	ring_buffer -> read_ind = (index + 1) % (ring_buffer -> capacity);

	pthread_mutex_lock(&((ring_buffer -> slot_locks)[index]));
	// copy contents to removed
	memcpy(ret_item, items + index, ring_buffer -> item_size_bytes);
	// optionally reset contents to be null
	//memset(items + index, 0, ring_buffer -> item_size_bytes);
	pthread_mutex_unlock(&((ring_buffer -> slot_locks)[index]));

	return 0;
}



int remove_item_ring(Ring_Buffer * ring_buffer, void * ret_item) {

	if (is_empty(ring_buffer)){
		return -1;
	}

	void * items = ring_buffer -> items;

	uint64_t index = ring_buffer -> read_ind;
	ring_buffer -> read_ind = (index + 1) % (ring_buffer -> capacity);

	pthread_mutex_lock(&((ring_buffer -> slot_locks)[index]));
	// copy contents to removed
	memcpy(ret_item, items + index, ring_buffer -> item_size_bytes);
	// optionally reset contents to be null
	//memset(items + index, 0, ring_buffer -> item_size_bytes);
	pthread_mutex_unlock(&((ring_buffer -> slot_locks)[index]));

	return 0;
}





