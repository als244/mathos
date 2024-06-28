#include "deque.h"

// Simple LinkedList implementation

Deque * init_deque() {
	Deque * deque = (Deque *) malloc(sizeof(Deque));

	if (deque == NULL){
		fprintf(stderr, "Error: malloc failed in init deque\n");
		return NULL;
	}

	deque -> cnt = 0;
	deque -> head = NULL;
	deque -> tail = NULL;

	return deque;

}

// Can only destroy empty deque because don't know what to do with exchange items
int destroy_deque(Deque * deque) {
	if (!is_deque_empty(deque)){
		fprintf(stderr, "Error: trying to destroy non-empty deque\n");
		return -1;
	}
	free(deque);
	return 0;
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

int dequeue(Deque * deque, void ** ret_item){
	
	if (is_deque_empty(deque)){
		fprintf(stderr, "Error: trying to dequeue from an empty deque\n");
		return -1;
	}

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

	return 0;
}

int dequeue_rear(Deque * deque, void ** ret_item){
	
	if (is_deque_empty(deque)){
		fprintf(stderr, "Error: trying to dequeue from an empty deque\n");
		return -1;
	}

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

	return 0;
}

bool is_deque_empty(Deque * deque){
	return deque -> cnt == 0;
}