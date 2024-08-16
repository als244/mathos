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
	// This represents the starting value for item
	// 0 in the bit_vector. It defines the unique
	// path in the tree for all keys in a 256 element range

	// Base repesents the upper (64 - tree bits) 
	// bits of the children (in this case leaf)
	uint64_t base: 56;
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
	uint64_t bit_vector[4];
	// this only get's populated
	// with non-null values

	// When inserting into outward
	// trees or the original insert
	// was null, propate a null value
	Fast_Table values;
	// this is a deque item 
	// whose pointer is to self
	Deque_Item leaf;
};

struct fast_tree_8 {
	// This represents the starting value for item
	// 0 in the bit_vector. It defines the unique
	// path in the tree for all keys in a 256 element range
	uint64_t base: 48;
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
	Fast_Table children;
	Fast_Tree_Leaf siblings;
	Fast_Tree_16 * parent;
};

struct fast_tree_16 {
	uint32_t base;
 	uint16_t cnt;
 	uint16_t min;
 	uint16_t max;
 	Fast_Table inward;
	Fast_Tree_8 outward;
 };

 struct fast_tree_32 {
 	// the base of all 32-bit trees is the
 	// root
	uint16_t cnt;
	uint32_t min;
	uint32_t max;
	Fast_Table inward;
	Fast_Tree_16 outward;
 };


struct fast_tree {
	uint64_t cnt;
	uint64_t min;
	uint64_t max;
	
	// hash table of index segments
	// the key within this table is the 
	// a uint32_t index ahd the value will be a 
	// Fast_Tree_16, which will represent
	// A tree searching for the offset
	Fast_Table inward;
	// This represents a Fast_Tree_32 seraching
	// for the uint32_t index of the original elmeent
	Fast_Tree_32 outward;
	// doubly linked list of non-null
	// leaves of the tree, each of which
	// can contain 256 key-value pairs within
	// a contiguous range
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


int insert_fast_tree(Fast_Tree * fast_tree, uint64_t key, void * value)














#endif