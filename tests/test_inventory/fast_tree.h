#ifndef FAST_TREE_H
#define FAST_TREE_H

#include "common.h"
#include "config.h"
#include "deque.h"
#include "fast_table.h"
#include "table.h"


// NOTE: THE UGLY TYPE SPECIFIC LEVELS IS DONE PURPOSEFULLY FOR PERFORMANCE REASONS!
//			- less memory usage and can define constants within the functions...


typedef struct fast_tree Fast_Tree;
typedef struct fast_tree_32 Fast_Tree_32;
typedef struct fast_tree_16 Fast_Tree_16;
typedef struct fast_tree_8 Fast_Tree_8;
typedef struct fast_tree_leaf Fast_Tree_Leaf;



struct fast_tree_leaf {
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
	uint64_t bit_vector[4];
	// This represents the starting value for item
	// 0 in the bit_vector. It defines the unique
	// path in the tree for all keys in a 256 element range
	uint64_t base;
	Fast_Tree_8 * parent;
	// this is a deque item 
	// whose pointer is to self
	Deque_Item leaf;
};

struct fast_tree_8 {
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
	Fast_Table children;
	Fast_Tree_Leaf siblings;
	Fast_Tree_16 * parent;
};

struct fast_tree_16 {
 	uint16_t cnt;
 	uint16_t min;
 	uint16_t max;
 	Fast_Table children;
	Fast_Tree_8 siblings;
	Fast_Tree_32 * parent;
 };

 struct fast_tree_32 {
	uint16_t cnt;
	uint32_t min;
	uint32_t max;
	Fast_Table children;
	Fast_Tree_16 siblings;
	Fast_Tree * parent;
 };


struct fast_tree {
	uint64_t cnt;
	uint64_t min;
	uint64_t max;
	
	// hash table of index segments
	// pointing to a Fast_Tree_32 for search
	// on the remainer.
	// The key is 32 bits so using 32-bit key'd
	// fast table
	Fast_Table children;
	// if segment not found as this
	// level uses the seg ind for 
	// search in this tree
	Fast_Tree_32 siblings;
	// doubly linked list of non-null
	// leaves of the tree
	Deque * ordered_leaves;
};


// SOME HASH FUNCTIONS

uint64_t hash_func_64(void * key_ref, uint64_t table_size);
uint64_t hash_func_32(void * key_ref, uint64_t table_size);
uint64_t hash_func_16(void * key_ref, uint64_t table_size);
uint64_t hash_func_8(void * key_ref, uint64_t table_size);


// Initializes a root level fast tree where
// keys will initially be inserted
Fast_Tree * init_fast_tree();














#endif