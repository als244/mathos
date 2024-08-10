#include "deque.h"

// Simple LinkedList implementation

Deque * init_deque(Item_Cmp item_cmp) {
	
	int ret;

	Deque * deque = (Deque *) malloc(sizeof(Deque));

	if (deque == NULL){
		fprintf(stderr, "Error: malloc failed in init deque\n");
		return NULL;
	}

	deque -> cnt = 0;
	deque -> head = NULL;
	deque -> tail = NULL;
	deque -> item_cmp = item_cmp;

	ret = pthread_mutex_init(&(deque -> list_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init deque lock\n");
		return NULL;
	}

	return deque;

}


int enqueue(Deque * deque, void * item){
	
	Deque_Item * d_item = (Deque_Item *) malloc(sizeof(Deque_Item));
	if (d_item == NULL){
		fprintf(stderr, "Error: malloc failed in enqueue\n");
		return -1;
	}

	d_item -> item = item;
	d_item -> next = NULL;
	d_item -> prev = deque -> tail;

	if (deque -> cnt == 0){
		deque -> head = d_item;
		deque -> tail = d_item;
		deque -> cnt += 1;
		return 0;	
	}

	deque -> tail -> next = d_item;
	deque -> tail = d_item;
	deque -> cnt += 1;

	return 0;
}

int enqueue_front(Deque * deque, void * item){

	Deque_Item * d_item = (Deque_Item *) malloc(sizeof(Deque_Item));
	if (d_item == NULL){
		fprintf(stderr, "Error: malloc failed in enqueue\n");
		return -1;
	}

	d_item -> item = item;
	d_item -> next = deque -> head;
	d_item -> prev = NULL;

	// if there was nothing also make this the tail
	if (deque -> cnt == 0){
		deque -> head = d_item;
		deque -> tail = d_item;
		deque -> cnt += 1;
		return 0;
	}

	deque -> head -> prev = d_item;
	deque -> head = d_item;
	deque -> cnt += 1;

	return 0;
}

// FOR NOW ASSUME WE NEVER DEQUEUE FROM EMPTY DEQUE
//	- Otherwise need to change return type to indicate error...


// requires lock, because when destroying deque, the lock has already been obtained
//	- so set this to false
void dequeue(Deque * deque, void ** ret_item){

	void * item = deque -> head -> item;
	
	Deque_Item * new_head = deque -> head -> next;
	
	free(deque -> head);

	deque -> head = new_head;
	if (new_head != NULL){
		deque -> head -> prev = NULL;
	}

	deque -> cnt -= 1;
	
	// set return
	*ret_item = item;

	return;
}

void dequeue_rear(Deque * deque, void ** ret_item){

	void * item = deque -> tail -> item;
	Deque_Item * new_tail = deque -> tail -> prev;
	free(deque -> tail);
	deque -> tail = new_tail;
	if (new_tail != NULL){
		deque -> tail -> next = NULL;
	}

	deque -> cnt -= 1;

	// set return
	*ret_item = item;

	return;
}


// Can only destroy empty deque because don't know what to do with exchange items
void destroy_deque(Deque * deque, bool to_free_items) {

	void * item;

	pthread_mutex_lock(&(deque -> list_lock));

	while (deque -> cnt > 0){
		dequeue(deque, &item);
		if (to_free_items){
			free(item);
		}
	}

	// unlock before destroying
	pthread_mutex_unlock(&(deque -> list_lock));
	pthread_mutex_destroy(&(deque -> list_lock));
	free(deque);
	return;
}

uint64_t get_count_deque(Deque * deque) {
	
	pthread_mutex_lock(&(deque -> list_lock));
	uint64_t cnt = deque -> cnt;
	pthread_mutex_unlock(&(deque -> list_lock));

	return cnt;
}

int insert_deque(Deque * deque, DequeEnd insert_end, void * item){

	int ret;

	pthread_mutex_lock(&(deque -> list_lock));

	if (insert_end == FRONT_DEQUE){
		ret = enqueue_front(deque, item);
	}
	else{
		ret = enqueue(deque, item);
	}

	pthread_mutex_unlock(&(deque -> list_lock));

	if (ret != 0){
		fprintf(stderr, "Inserting to deque failed because out of memory to create new deque_item\n");
	}

	return ret;
}


int take_deque(Deque * deque, DequeEnd take_end, void ** ret_item){

	pthread_mutex_lock(&(deque -> list_lock));

	if (deque -> cnt == 0){
		*ret_item = NULL;
		return -1;
	}

	void * item;

	if (take_end == FRONT_DEQUE){
		dequeue(deque, &item);
	}
	else{
		dequeue_rear(deque, &item);
	}

	pthread_mutex_unlock(&(deque -> list_lock));

	*ret_item = item;

	return 0;
}



int take_and_replace_deque(Deque * deque, DequeEnd take_end, DequeEnd replace_end, void ** ret_item){

	int ret;
	void * item;

	pthread_mutex_lock(&(deque -> list_lock));

	if (deque -> cnt == 0){
		*ret_item = NULL;
		return -1;
	}

	// Take Item

	if (take_end == FRONT_DEQUE){
		dequeue(deque, &item);
	}
	else{
		dequeue_rear(deque, &item);
	}

	// Replace Item
	
	if (replace_end == FRONT_DEQUE){
		ret = enqueue_front(deque, item);
	}
	else{
		ret = enqueue(deque, item);
	}

	pthread_mutex_unlock(&(deque -> list_lock));

	if (ret != 0){
		fprintf(stderr, "Take and replace failed because out of memory to create replacement deque_item\n");
	}

	// set return of item
	*ret_item = item;

	return ret;
}


int peek_item_at_index_deque(Deque * deque, DequeEnd start_end, uint64_t index, void ** ret_item){

	void * item;

	pthread_mutex_lock(&(deque -> list_lock));

	// Error checking if we want it

	if (index >= deque -> cnt) {
		fprintf(stderr, "Error: cannot peek at item at index %lu, when deque count is %lu\n", index, deque -> cnt);
		*ret_item = NULL;
		return -1;
	}

	Deque_Item * cur_item;

	if (start_end == FRONT_DEQUE){
		cur_item = deque -> head;
	}
	else{
		cur_item = deque -> tail;
	}

	uint64_t cnt = 0;
	while (cnt < index){

		if (start_end == FRONT_DEQUE){
			cur_item = cur_item -> next;
		}
		else{
			cur_item = cur_item -> prev;
		}
		cnt++;
	}

	item = cur_item -> item;

	pthread_mutex_unlock(&(deque -> list_lock));

	*ret_item = item;

	return 0;
}


uint64_t remove_if_eq_deque(Deque * deque, void * item, bool to_free) {

	pthread_mutex_lock(&(deque -> list_lock));

	uint64_t removed_cnt = 0;
	Deque_Item * cur_deque_item = deque -> head;
	Deque_Item * prev_deque_item;
	Deque_Item * next_deque_item;

	void * cur_item;
	// can break early if we already removed the maximum
	while(cur_deque_item != NULL){

		cur_item = cur_deque_item;
		
		prev_deque_item = cur_deque_item -> prev;
		next_deque_item = cur_deque_item -> next;
		
		// the items are the same so remove
		if ((deque -> item_cmp)(item, cur_item) == 0){

			// now need to remove the current deque item

			// we removed the only item in deque
			// set head and tail to be null, free item and decrement count
			if (prev_deque_item == NULL && next_deque_item == NULL){
				deque -> head = NULL;
				deque -> tail = NULL;
			}
			// we removed head
			else if (prev_deque_item == NULL){
				deque -> head = next_deque_item;
				next_deque_item -> prev = NULL;
			}
			// we removed tail
			else if (next_deque_item == NULL){
				deque -> tail = prev_deque_item;
				prev_deque_item -> next = NULL;
			}
			// we removed middle element
			else{
				prev_deque_item -> next = next_deque_item;
				next_deque_item -> prev = prev_deque_item;
			}

			// update values now that we have removed an element
			deque -> cnt -= 1;
			removed_cnt += 1;
			if (to_free){
				free(cur_item);
			}
			free(cur_deque_item);
		}

		cur_deque_item = next_deque_item;
	}

	pthread_mutex_unlock(&(deque -> list_lock));

	return removed_cnt;
}



// returns the number of items that were removed

// can use max_remove and search_start_end to accelerate removal
//	(i.e. if caller knows there is maximum of 1 copy of item in deque, and wants to remove it
//			they can set max_remove = 1 to break when it is removed. If they have prior of 
//			if the item would be at beginning or end then they can choose where to start searching)

// to_free indicates if the item should be freed upon removal
uint64_t remove_if_eq_accel_deque(Deque * deque, void * item, uint64_t max_remove, DequeEnd search_start_end, bool to_free) {

	pthread_mutex_lock(&(deque -> list_lock));

	uint64_t removed_cnt = 0;
	Deque_Item * cur_deque_item;
	if (search_start_end == FRONT_DEQUE){
		cur_deque_item = deque -> head;
	}
	else{
		cur_deque_item = deque -> tail;
	}

	Deque_Item * prev_deque_item;
	Deque_Item * next_deque_item;

	void * cur_item;
	// can break early if we already removed the maximum
	while((cur_deque_item != NULL) && (removed_cnt < max_remove)){
		cur_item = cur_deque_item;
		prev_deque_item = cur_deque_item -> prev;
		next_deque_item = cur_deque_item -> next;
		// the items are the same so remove
		if ((deque -> item_cmp)(item, cur_item) == 0){

			// now need to remove the current deque item

			// we removed the only item in deque
			// set head and tail to be null, free item and decrement count
			if (prev_deque_item == NULL && next_deque_item == NULL){
				deque -> head = NULL;
				deque -> tail = NULL;
			}
			// we removed head
			else if (prev_deque_item == NULL){
				deque -> head = next_deque_item;
				next_deque_item -> prev = NULL;
			}
			// we removed tail
			else if (next_deque_item == NULL){
				deque -> tail = prev_deque_item;
				prev_deque_item -> next = NULL;
			}
			// we removed middle element
			else{
				prev_deque_item -> next = next_deque_item;
				next_deque_item -> prev = prev_deque_item;
			}

			// update values now that we have removed an element
			deque -> cnt -= 1;
			removed_cnt += 1;
			if (to_free){
				free(cur_item);
			}
			free(cur_deque_item);
		}
		// going forwards means use next, backwards means prev
		if (search_start_end == FRONT_DEQUE){
			cur_deque_item = next_deque_item;
		}
		else{
			cur_deque_item = prev_deque_item;
		}
	}

	pthread_mutex_unlock(&(deque -> list_lock));

	return removed_cnt;
}


uint64_t get_item_count_deque(Deque * deque, void * item) {

	pthread_mutex_lock(&(deque -> list_lock));

	uint64_t seen_cnt = 0;
	
	Deque_Item * cur_deque_item = deque -> head;
	void * cur_item;
	while(cur_deque_item != NULL){
		cur_item = cur_deque_item;
		// the items are the same so remove
		if ((deque -> item_cmp)(item, cur_item) == 0){
			seen_cnt += 1;
		}
		cur_deque_item = cur_deque_item -> next;
	}

	pthread_mutex_unlock(&(deque -> list_lock));

	return seen_cnt;
}



int insert_lockless_deque(Deque * deque, DequeEnd insert_end, void * item){

	int ret;

	if (insert_end == FRONT_DEQUE){
		ret = enqueue_front(deque, item);
	}
	else{
		ret = enqueue(deque, item);
	}

	if (ret != 0){
		fprintf(stderr, "Inserting to deque failed because out of memory to create new deque_item\n");
	}

	return ret;
}


int take_lockless_deque(Deque * deque, DequeEnd take_end, void ** ret_item){

	if (deque -> cnt == 0){
		*ret_item = NULL;
		return -1;
	}

	void * item;

	if (take_end == FRONT_DEQUE){
		dequeue(deque, &item);
	}
	else{
		dequeue_rear(deque, &item);
	}

	*ret_item = item;

	return 0;
}


int take_first_matching_lockless_deque(Deque * deque, DequeEnd search_end, void * search_item, void ** ret_item) {

	// assert(deque -> item_cmp != NULL)

	Deque_Item * cur_deque_item;
	if (search_end == FRONT_DEQUE){
		cur_deque_item = deque -> head;
	}
	else{
		cur_deque_item = deque -> tail;
	}

	Deque_Item * prev_deque_item;
	Deque_Item * next_deque_item;

	void * cur_item;
	void * item = NULL;
	bool found_item = false;
	while(cur_deque_item != NULL){

		cur_item = cur_deque_item -> item;
		prev_deque_item = cur_deque_item -> prev;
		next_deque_item = cur_deque_item -> next;
		// the items are the same so remove
		if ((deque -> item_cmp)(search_item, cur_item) == 0) {
			
			// set the matching item
			item = cur_item;
			found_item = true;

			// now need to remove the current deque item

			// we removed the only item in deque
			// set head and tail to be null, free item and decrement count
			if (prev_deque_item == NULL && next_deque_item == NULL){
				deque -> head = NULL;
				deque -> tail = NULL;
			}
			// we removed head
			else if (prev_deque_item == NULL){
				deque -> head = next_deque_item;
				next_deque_item -> prev = NULL;
			}
			// we removed tail
			else if (next_deque_item == NULL){
				deque -> tail = prev_deque_item;
				prev_deque_item -> next = NULL;
			}
			// we removed middle element
			else{
				prev_deque_item -> next = next_deque_item;
				next_deque_item -> prev = prev_deque_item;
			}

			// update values now that we have removed an element
			deque -> cnt -= 1;

			// free the deque_item
			free(cur_deque_item);
			break;
		}
		// going forwards means use next, backwards means prev
		if (search_end == FRONT_DEQUE){
			cur_deque_item = next_deque_item;
		}
		else{
			cur_deque_item = prev_deque_item;
		}


	}

	*ret_item = item;

	if (!found_item){
		return -1;
	}
	return 0;
}


