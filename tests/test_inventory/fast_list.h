#ifndef FAST_LIST_H
#define FAST_LIST_H

#include "common.h"

typedef struct fast_list_node Fast_List_Node;

struct fast_list_node {
	uint64_t item;
	Fast_List_Node * prev;
	Fast_List_Node * next;
	// only free nodes not held within buffer
	bool to_free;
};

typedef struct fast_list {
	uint64_t cnt;
	Fast_List_Node * head;
	Fast_List_Node * tail;
	uint64_t node_buffer_capacity;
	uint64_t node_buffer_cnt;
	Fast_List_Node * node_buffer;
} Fast_List;


Fast_List * init_fast_list(uint64_t node_buffer_capacity);

void destroy_fast_list(Fast_List * fast_list);

Fast_List_Node * insert_fast_list(Fast_List * fast_list, uint64_t item);

Fast_List_Node * insert_front_fast_list(Fast_List * fast_list, uint64_t item);

void remove_node_fast_list(Fast_List * fast_list, Fast_List_Node * node);

int take_fast_list(Fast_List * fast_list, uint64_t * ret_item);

int take_back_fast_list(Fast_List * fast_list, uint64_t * ret_item);

#endif