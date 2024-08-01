#include "fifo.h"


// Initializes fifo struct and allocates memory for buffer
Fifo * init_fifo(uint64_t max_items, uint64_t item_size_bytes) {

	int ret;

	Fifo * fifo = (Fifo *) malloc(sizeof(Fifo));
	if (fifo == NULL){
		fprintf(stderr, "Error: malloc failed to allocate fifo container\n");
		return NULL;
	}

	fifo -> max_items = max_items;
	fifo -> item_size_bytes = item_size_bytes;
	fifo -> produce_ind = 0;
	fifo -> consume_ind = 0;
	fifo -> item_cnt = 0;

	ret = pthread_mutex_init(&(fifo -> update_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init fifo lock\n");
		return NULL;
	}

	ret = pthread_cond_init(&(fifo -> produced_cv), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init fifo condition variable\n");
		return NULL;
	}

	ret = pthread_cond_init(&(fifo -> consumed_cv), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init fifo condition variable\n");
		return NULL;
	}

	fifo -> available_items = 0;

	// initialize buffer to place items
	uint64_t buffer_size = max_items * item_size_bytes;
	fifo -> buffer = malloc(buffer_size);
	if (fifo -> buffer == NULL){
		fprintf(stderr, "Error: malloc failed to allocate fifo buffer of size: %lu\n", buffer_size);
		return NULL;
	}

	return fifo;
}

void * get_buffer_addr(Fifo * fifo, uint64_t ind) {
	uint64_t buffer_addr = (uint64_t) fifo -> buffer;
	uint64_t offset = ind * (fifo -> item_size_bytes);
	return (void *) (buffer_addr + offset);
}


// Returns the index of insertion
//	- Needed for receive channels to proply post IB receive work requests

// COPIES CONTENTS OF ITEM
// BLOCKING!
uint64_t produce_fifo(Fifo * fifo, void * item) {

	// Error Check if we want

	/*
	if (unlikely(fifo == NULL)){
		fprintf(stderr, "Error: produce_fifo failed because fifo is null\n");
		return -1;
	}
	*/

	uint64_t insert_ind;

	// 1.) Optimisitically acquire update lock

	pthread_mutex_lock(&(fifo -> update_lock));

	// 2.) Wait until there is space to insert item
	while (fifo -> available_items == fifo -> max_items){
		pthread_cond_wait(&(fifo -> consumed_cv), &(fifo -> update_lock));
	}

	// 3.) Actually insert item

	insert_ind = fifo -> produce_ind;
	void * insert_start_addr;

	// NOTE:
	//	- for posting receives item will be null and it will get populated by NIC
	//	- for posting sends, item will have contents that need to be copied from non-registered memory and sent out
	if (item != NULL){
		insert_start_addr = get_buffer_addr(fifo, insert_ind);
		memcpy(insert_start_addr, item, fifo -> item_size_bytes);
	}

	// 4.) Update the number of items and the next spot to insert
	fifo -> available_items += 1;
	fifo -> produce_ind = (fifo -> produce_ind + 1) % (fifo -> max_items);

	// 5.) Release the update lock
	pthread_mutex_unlock(&(fifo -> update_lock));
	
	// 6.) Indicate that we updated the produced cv to unblock a consumer
	//		- for single item producer (not-batched) can use signal
	//		- deicding to signal after unlock so consumer thread is not blocked on the update lock
	pthread_cond_signal(&(fifo -> produced_cv));

	return insert_ind;
}


// RETRUNS A COPY OF THE ITEM IN THE BUFFER
// ASSUME A DIFFERNT PRODUCER THREAD => Otherwise, deadlock will occur
// BLOCKING!
// Can return NULL if not empty to 

// ASSUME MEMORY FOR RET_ITEM ALREADY ALLOCATED
void consume_fifo(Fifo * fifo, void * ret_item) {

	// Error Check (if we want)

	/*
	if (unlikely(fifo == NULL)){
		fprintf(stderr, "Error: consume_fifo failed because fifo is null\n");
		return NULL;
	}
	*/

	// 1.) Optimisitically acquire the update lock
	pthread_mutex_lock(&(fifo -> update_lock));

	// 2.) Wait until there are items to consume

	while (fifo -> available_items == 0){
		pthread_cond_wait(&(fifo -> produced_cv), &(fifo -> update_lock));
	}

	// 3.) Actually consume item
	void * remove_start_addr = get_buffer_addr(fifo, fifo -> consume_ind);
	memcpy(ret_item, remove_start_addr, fifo -> item_size_bytes);

	// 4.) Update the number of items and the next spot to consume
	fifo -> available_items -= 1;
	fifo -> consume_ind = (fifo -> consume_ind + 1) % (fifo -> max_items);

	// 5.) Release the update lock
	pthread_mutex_unlock(&(fifo -> update_lock));
	
	// 6.) Indicate that we updated the produced cv to unblock a producer
	//		- for single item producer (not-batched) can use signal
	//		- deicding to signal after unlock so producer thread is not blocked on the update lock
	pthread_cond_signal(&(fifo -> consumed_cv));

	return;

}


uint64_t produce_batch_fifo(Fifo * fifo, uint64_t num_items, void * items) {

	// Error Check if we want

	/*
	if (unlikely(fifo == NULL)){
		fprintf(stderr, "Error: produce_fifo failed because fifo is null\n");
		return -1;
	}
	*/

	uint64_t start_insert_ind;

	// 1.) Optimisitically acquire update lock

	pthread_mutex_lock(&(fifo -> update_lock));

	// 2.) Wait until there is space to insert items
	while (fifo -> available_items + num_items > fifo -> max_items){
		pthread_cond_wait(&(fifo -> consumed_cv), &(fifo -> update_lock));
	}

	// 3.) Actually produce items

	start_insert_ind = fifo -> produce_ind;
	uint64_t items_til_end, bytes_til_end, remain_bytes; 
	void * start_insert_addr;

	uint64_t remain_item_cnt = 0;
	// NOTE:
	//	- for posting receives item will be null and it will get populated by NIC
	//	- for posting sends, item will have contents that need to be copied from non-registered memory and sent out
	if (items != NULL){

		if (num_items > (fifo -> max_items - start_insert_ind - 1)){
			remain_item_cnt = num_items - (fifo -> max_items - start_insert_ind - 1);
		}

		items_til_end = num_items - remain_item_cnt;
		bytes_til_end = items_til_end * fifo -> item_size_bytes;

		// copy items to the end of the ring buffer then start at the beginning
		start_insert_addr = get_buffer_addr(fifo, start_insert_ind);
		memcpy(start_insert_addr, items, bytes_til_end);

		// start copying items from beginning of buffer
		void * remain_items = (void *) ((uint64_t) items + bytes_til_end);
		remain_bytes = (num_items - items_til_end) * fifo -> item_size_bytes;
		memcpy(fifo -> buffer, remain_items, remain_bytes); 
	}

	
	// 4.) Update the number of items and the next spot to insert
	fifo -> available_items += num_items;
	fifo -> produce_ind = (fifo -> produce_ind + num_items) % (fifo -> max_items);

	// 5.) Release the update lock
	pthread_mutex_unlock(&(fifo -> update_lock));
	
	// 6.) Indicate that we updated the produced cv to unblock consumers
	//		- now use broadcast because some of the waiting consumers 
	//			could have varying amounts they are trying to consume
	pthread_cond_broadcast(&(fifo -> produced_cv));

	return start_insert_ind;
}


void consume_batch_fifo(Fifo * fifo, uint64_t num_items, void * ret_items) {


	// 1.) Optimisitically acquire update lock
	pthread_mutex_lock(&(fifo -> update_lock));

	// 2.) Wait until there is space to insert items
	while (fifo -> available_items < num_items){
		pthread_cond_wait(&(fifo -> produced_cv), &(fifo -> update_lock));
	}

	// 3.) Actually consume items

	uint64_t items_til_end, bytes_til_end, remain_bytes; 
		
	// calling these "remove" index/address, but really they are copying
	//	- the producer will be over-writing
	//	- could memset to 0 if we wanted to actually remove


	uint64_t start_remove_ind = fifo -> consume_ind;
	void * start_remove_addr = get_buffer_addr(fifo, start_remove_ind);

	uint64_t remain_item_cnt = 0;
	if (num_items > (fifo -> max_items - start_remove_ind - 1)){
		remain_item_cnt = num_items - (fifo -> max_items - start_remove_ind - 1);
	}

	items_til_end = num_items - remain_item_cnt;
	bytes_til_end = items_til_end * fifo -> item_size_bytes;

	// copy items until the end of the ring buffer then start at the beginning
	start_remove_addr = get_buffer_addr(fifo, start_remove_ind);
	memcpy(ret_items, start_remove_addr, bytes_til_end);

	// start copying items from beginning of buffer
	void * remain_items = (void *) ((uint64_t) ret_items + bytes_til_end);
	remain_bytes = (num_items - items_til_end) * fifo -> item_size_bytes;
	memcpy(remain_items, fifo -> buffer, remain_bytes); 

	
	// 4.) Update the number of items and the next spot to insert
	fifo -> available_items -= num_items;
	fifo -> consume_ind = (fifo -> consume_ind + num_items) % (fifo -> max_items);

	// 5.) Release the update lock
	pthread_mutex_unlock(&(fifo -> update_lock));
	
	// 6.) Indicate that we updated the consume cv to unblock producers
	//		- now use broadcast because some of the waiting consumers 
	//			could have varying amounts they are trying to consume
	pthread_cond_broadcast(&(fifo -> consumed_cv));

	return;
}



// Used within control handlers to retrieve number of work completitions and produce empty receives (insert items would be NULL)

// Returns the index of insertion
//	- Needed for receive channels to proply post IB receive work requests
uint64_t consume_and_reproduce_batch_fifo(Fifo * fifo, uint64_t num_items, void * consumed_items, void * reproduced_items) {

	uint64_t start_insert_ind;


	// 1.) Optimisitically acquire update lock
	pthread_mutex_lock(&(fifo -> update_lock));

	// 2.) Wait until there is space to insert items
	while (fifo -> available_items < num_items){
		pthread_cond_wait(&(fifo -> produced_cv), &(fifo -> update_lock));
	}

	// 3.) Actually consume items

	uint64_t items_til_end, bytes_til_end, remain_bytes; 
		
	// calling these "remove" index/address, but really they are copying
	//	- the producer will be over-writing
	//	- could memset to 0 if we wanted to actually remove


	uint64_t start_remove_ind = fifo -> consume_ind;
	void * start_remove_addr = get_buffer_addr(fifo, start_remove_ind);

	uint64_t remain_item_cnt = 0;
	if (num_items > (fifo -> max_items - start_remove_ind - 1)){
		remain_item_cnt = num_items - (fifo -> max_items - start_remove_ind - 1);
	}

	items_til_end = num_items - remain_item_cnt;
	bytes_til_end = items_til_end * fifo -> item_size_bytes;

	// copy items until the end of the ring buffer then start at the beginning
	start_remove_addr = get_buffer_addr(fifo, start_remove_ind);
	memcpy(consumed_items, start_remove_addr, bytes_til_end);

	// start copying items from beginning of buffer
	void * remain_items = (void *) ((uint64_t) consumed_items + bytes_til_end);
	remain_bytes = (num_items - items_til_end) * fifo -> item_size_bytes;
	memcpy(remain_items, fifo -> buffer, remain_bytes); 

	
	// 4.) Actually reproduce items

	start_insert_ind = fifo -> produce_ind;
	void * start_insert_addr;

	// NOTE:
	//	- for posting receives item will be null and it will get populated by NIC
	//	- for posting sends, item will have contents that need to be copied from non-registered memory and sent out
	if (reproduced_items != NULL){

		remain_item_cnt = 0;
		if (num_items > (fifo -> max_items - start_insert_ind - 1)){
			remain_item_cnt = num_items - (fifo -> max_items - start_insert_ind - 1);
		}

		items_til_end = num_items - remain_item_cnt;
		bytes_til_end = items_til_end * fifo -> item_size_bytes;

		// copy items to the end of the ring buffer then start at the beginning
		start_insert_addr = get_buffer_addr(fifo, start_insert_ind);
		memcpy(start_insert_addr, reproduced_items, bytes_til_end);

		// start copying items from beginning of buffer
		remain_items = (void *) ((uint64_t) reproduced_items + bytes_til_end);
		remain_bytes = (num_items - items_til_end) * fifo -> item_size_bytes;
		memcpy(fifo -> buffer, remain_items, remain_bytes); 
	}


	// 5.) Now we don't change the number of available items, but we 
	//		need to update the consume and produce indicies
	fifo -> consume_ind = (fifo -> consume_ind + num_items) % (fifo -> max_items);
	fifo -> produce_ind = (fifo -> produce_ind + num_items) % (fifo -> max_items);

	// 6.) Release the update lock
	pthread_mutex_unlock(&(fifo -> update_lock));
	
	// 7.) Note: We don't need signal/broadcast anyone because available items count did not change
	
	return start_insert_ind;
}

