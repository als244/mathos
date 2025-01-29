#include "fast_list.h"


Fast_List * init_fast_list(uint64_t node_buffer_capacity){

	Fast_List * fast_list = (Fast_List *) malloc(sizeof(Fast_List));
	if (!fast_list){
		fprintf(stderr, "Error: malloc failed to allocate fast_list\n");
		return NULL;
	}

	fast_list -> cnt = 0;
	fast_list -> head = NULL;
	fast_list -> tail = NULL;

	return fast_list;
}


void destroy_fast_list(Fast_List * fast_list){

	// assert cnt == 0
	free(fast_list);
	return;
}


Fast_List_Node * create_fast_list_node(Fast_List * fast_list){

	Fast_List_Node * new_node;

	new_node = malloc(sizeof(Fast_List_Node));
	if (!new_node){
		fprintf(stderr, "Error: malloc failed to allocate new fast list node\n");
		return NULL;
	}
	new_node -> item = 0;
	new_node -> prev = NULL;
	new_node -> next = NULL;

	return new_node;
}

void destroy_fast_list_node(Fast_List * fast_list, Fast_List_Node * fast_list_node){
	free(fast_list_node);
}


Fast_List_Node * insert_fast_list(Fast_List * fast_list, uint64_t item){

	Fast_List_Node * new_node = create_fast_list_node(fast_list);
	if (!new_node){
		fprintf(stderr, "Error: unable to create new fast list node\n");
		return NULL;
	}

	new_node -> next = NULL;
	new_node -> item = item;	

	if (fast_list -> tail){
		fast_list -> tail -> next = new_node;
		new_node -> prev = fast_list -> tail;
		fast_list -> tail = new_node;
	}
	// first insert
	else{
		new_node -> prev = NULL;
		fast_list -> head = new_node;
		fast_list -> tail = new_node;
	}

	fast_list -> cnt += 1;

	return new_node;
}

Fast_List_Node * insert_front_fast_list(Fast_List * fast_list, uint64_t item) {

	Fast_List_Node * new_node = create_fast_list_node(fast_list);
	if (!new_node){
		fprintf(stderr, "Error: unable to create new fast list node\n");
		return NULL;
	}

	new_node -> prev = NULL;
	new_node -> item = item;

	if (fast_list -> head){
		fast_list -> head -> prev = new_node;
		new_node -> next = fast_list -> head;
		fast_list -> head = new_node;
	}
	// first insert
	else{
		new_node -> next = NULL;
		fast_list -> head = new_node;
		fast_list -> tail = new_node;
	}

	fast_list -> cnt += 1;

	return new_node;
}


void remove_node_fast_list(Fast_List * fast_list, Fast_List_Node * node){

	Fast_List_Node * prev_node = node -> prev;
	Fast_List_Node * next_node = node -> next;

	if (prev_node && next_node){
		prev_node -> next = next_node;
		next_node -> prev = prev_node;
	}
	else if (prev_node){
		prev_node -> next = NULL;
		fast_list -> tail = prev_node;
	}
	else if (next_node){
		next_node -> prev = NULL;
		fast_list -> head = next_node;
	}
	else{
		fast_list -> head = NULL;
		fast_list -> tail = NULL;
	}

	destroy_fast_list_node(fast_list, node);

	fast_list -> cnt -= 1;
}

int take_fast_list(Fast_List * fast_list, uint64_t * ret_item) {

	if (!fast_list -> head){
		return -1;
	}

	*ret_item = fast_list -> head -> item;

	Fast_List_Node * new_head = fast_list -> head -> next;

	destroy_fast_list_node(fast_list, fast_list -> head);

	fast_list -> head = new_head;

	if (fast_list -> head){
		new_head -> prev = NULL;
	}
	else{
		fast_list -> tail = NULL;
	}

	fast_list -> cnt -= 1;

	return 0;
}

int take_back_fast_list(Fast_List * fast_list, uint64_t * ret_item) {

	if (!fast_list -> tail){
		return -1;
	}

	*ret_item = fast_list -> tail -> item;

	Fast_List_Node * new_tail = fast_list -> tail -> prev;

	destroy_fast_list_node(fast_list, fast_list -> tail);

	fast_list -> tail = new_tail;

	if (fast_list -> tail){
		new_tail -> next = NULL;
	}
	else{
		fast_list -> head = NULL;
	}

	fast_list -> cnt -= 1;

	return 0;
}