#ifndef FAST_TREE_H
#define FAST_TREE_H

#include "common.h"
#include "deque.h"
#include "fast_table.h"
#include "table.h"


// NOTE: THE SOMEWHAT UGLY TYPE SPECIFIC LEVELS IS DONE PURPOSEFULLY FOR PERFORMANCE REASONS!

typedef struct fast_tree_leaf {
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
	uint64_t bit_vector[4];
	// this is a deque item 
	// whose pointer is to self
	Deque_Item leaf;
} Fast_Tree_Leaf;

typedef struct fast_tree_8 {
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
	Fast_Table vertical;
	Fast_Tree_Leaf horizontal;
} Fast_Tree_8;

 typedef struct fast_tree_16 {
 	uint16_t cnt;
 	uint16_t min;
 	uint16_t max;
 	Fast_Table vertical;
	Fast_Tree_8 horizontal;
 } Fast_Tree_16;

 typedef struct fast_tree_32 {
	uint16_t cnt;
	uint32_t min;
	uint32_t max;
	Fast_Table vertical;
	Fast_Tree_16 horizontal;
 } Fast_Tree_32;


typedef struct fast_tree {
	uint64_t cnt;
	uint64_t min;
	uint64_t max;
	
	// hash table of index segments
	// pointing to a Fast_Tree_32 for search
	// on the remainer.
	// The key is 32 bits so using 32-bit key'd
	// fast table
	Fast_Table vertical;
	// if segment not found as this
	// level uses the seg ind for 
	// search in this tree
	Fast_Tree_32 horizontal;
	// doubly linked list of non-null
	// leaves of the tree
	Deque * ordered_leaves;
} Fast_Tree;
















#endif