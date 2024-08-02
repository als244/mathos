#include "ring_buffer.h"


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

	void * items = (void *) malloc(capacity * item_size_bytes);

	if (items == NULL){
		fprintf(stderr, "Error: malloc failed for ring buffer items\n");
		return NULL;
	}

	ring_buffer -> items = items;
	return ring_buffer;

}


int is_empty(Ring_Buffer * ring_buffer){
	return ring_buffer -> write_ind == ring_buffer -> read_ind;
}

int is_full(Ring_Buffer * ring_buffer){
	return (ring_buffer -> write_ind + 1) % ring_buffer -> capacity == ring_buffer -> read_ind;
}

uint64_t get_write_addr(Ring_Buffer * ring_buffer, uint64_t n_advance){
	if (is_full(ring_buffer)){
		return 0;
	}
	void * items = ring_buffer -> items;
	uint64_t index = (ring_buffer -> write_ind + n_advance) % (ring_buffer -> capacity);
	uint64_t write_addr = (uint64_t) (items + index);
	return write_addr;
}


int insert_item_ring(Ring_Buffer * ring_buffer, void * item) {
	if (is_full(ring_buffer)){
		return -1;
	}

	void * items = ring_buffer -> items;

	uint64_t index = ring_buffer -> write_ind;

	memcpy(items + index, item, ring_buffer -> item_size_bytes);

	ring_buffer -> write_ind = (index + 1) % (ring_buffer -> capacity);

	return 0;
}


int remove_item_ring(Ring_Buffer * ring_buffer, void * ret_item) {

	if (is_empty(ring_buffer)){
		return -1;
	}

	void * items = ring_buffer -> items;

	uint64_t index = ring_buffer -> read_ind;

	memcpy(ret_item, items + index, ring_buffer -> item_size_bytes);

	ring_buffer -> read_ind = (index + 1) % (ring_buffer -> capacity);

	return 0;
}

