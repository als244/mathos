#include "fifo.h"

// returns the sem_id 
int init_semaphore(int init_val){
	int sem_id = semget(IPC_PRIVATE, 1, IPC_CREAT | IPC_EXCL | S_IRUSR |
        S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (sem_id < 0){
		fprintf(stderr, "Error: semget failed\n");
		return -1;
	}

	union semun sem_setval;
	sem_setval.val = init_val;

	int ret = semctl(sem_id, 0, SETVAL, sem_setval);

	if (ret != 0){
		fprintf(stderr, "Error: failure to set semaphore value to %d\n", init_val);
		return -1;
	}

	return sem_id;
}

int do_semop(int sem_id, int val){

	struct sembuf sem_op_buf;

	sem_op_buf.sem_num = 0;
	sem_op_buf.sem_op = val;
	sem_op_buf.sem_flg = SEM_UNDO;

	int ret = semop(sem_id, &sem_op_buf, 1);
	if (ret != 0){
		fprintf(stderr, "Error: semop failed\n");
		return -1;
	}
	return 0;
}


int semaphore_post(int sem_id, uint32_t items){
	return do_semop(sem_id, (int) items);
}

int semaphore_wait(int sem_id, uint32_t items){
	int items_neg = -1 * ((int) items);
	return do_semop(sem_id, items_neg);
}



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

	ret = pthread_mutex_init(&(fifo -> fifo_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init table lock\n");
		return NULL;
	}

	int empty_slots_sem_id = init_semaphore(max_items);
	if (empty_slots_sem_id < 0){
		fprintf(stderr, "Error: could not initialize empty slots semaphore\n");
		return NULL;
	}

	int full_slots_sem_id = init_semaphore(0);
	if (empty_slots_sem_id < 0){
		fprintf(stderr, "Error: could not initialize full slots semaphore\n");
		return NULL;
	}

	fifo -> empty_slots_sem_id  = empty_slots_sem_id;
	fifo -> full_slots_sem_id = full_slots_sem_id;

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


// COPIES CONTENTS OF ITEM SO CAN FREE ITEM AFTER
// BLOCKING!
uint64_t produce_fifo(Fifo * fifo, void * item) {

	// Error Check if we want

	/*
	if (unlikely(fifo == NULL)){
		fprintf(stderr, "Error: produce_fifo failed because fifo is null\n");
		return -1;
	}
	*/

	// 1.) Wait until there is more room in the buffer
	semaphore_wait(fifo -> empty_slots_sem_id, 1);

	// 2.) Wait until the consumer has completed removing an item from buffer
	pthread_mutex_lock(&(fifo -> fifo_lock));

	// 3.) Actually insert item
	//		- copies the contents => FREE AFTER PRODUCING
	void * insert_start_addr = get_buffer_addr(fifo, fifo -> produce_ind);

	// NOTE:
	//	- for posting receives item will be null and it will get populated by NIC
	//	- for posting sends, item will have contents that need to be copied from non-registered memory and sent out
	if (item != NULL){
		memcpy(insert_start_addr, item, fifo -> item_size_bytes);
	}
	
	// 4.) Set the index which we inserted the item at
	uint64_t ret_ind = fifo -> produce_ind;

	// 5.) Increment the item count
	fifo -> item_cnt += 1;

	// 6.) Increment the producer ind (circular)
	fifo -> produce_ind = (fifo -> produce_ind + 1) % fifo -> max_items;

	// 7.) Indicate that we have finished producing
	pthread_mutex_unlock(&(fifo -> fifo_lock));

	// 8.) Indicate that there is a new item in buffer
	semaphore_post(fifo -> full_slots_sem_id, 1);

	return ret_ind;
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

	// 1.) Wait until there is an item to consume
	semaphore_wait(fifo -> full_slots_sem_id, 1);

	// 2.) Wait until the producer has finished producing
	pthread_mutex_lock(&(fifo -> fifo_lock));

	// 3.) Actually consume item
	//		- return a copy to item => FREE AFTER CONSUMING!
	void * remove_start_addr = get_buffer_addr(fifo, fifo -> consume_ind);
	memcpy(ret_item, remove_start_addr, fifo -> item_size_bytes);

	// 4.) Decrement the item count
	fifo -> item_cnt -= 1;

	// 5.) Increment the consumer ind (circular)
	fifo -> consume_ind = (fifo -> consume_ind + 1) % fifo -> max_items;

	// 6.) Indicate that the consumer has finished consuming
	pthread_mutex_unlock(&(fifo -> fifo_lock));

	// 7.) Now add can signal there is a new empty spot
	semaphore_post(fifo -> empty_slots_sem_id, 1);

	return;
}

