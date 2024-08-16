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
typedef struct fast_tree_outward_leaf Fast_Tree_Outward_Leaf;
typedef struct fast_tree_leaf Fast_Tree_Leaf;

typedef enum fast_tree_search_type {
	FAST_TREE_EQUAL,
	FAST_TREE_NEXT,
	FAST_TREE_EQUAL_OR_NEXT,
	FAST_TREE_PREV,
	FAST_TREE_EQUAL_OR_PREV
} FastTreeSearchType;


struct fast_tree_leaf {
	// This represents the starting value for item
	// 0 in the bit_vector. It defines the unique
	// path in the tree from the root 
	// for keys in a 256 element contiguous range
	// starting at this base

	// Base repesents the upper (64 - tree bits) 
	// bits of the children (in this case leaf)

	// The base implicity implies the path to get
	// to this leaf

	// This leaf = root.inward[base & 0xFFFF].inward[base & 0xFF].inward[base & 0xF]
	// Becaues root.inward[base & 0xFFFF] is a Fast_Tree_16, then get's to a Fast_Tree_8
	// and then gets to this leaf

	// Once we traverse to the leaf (or start there from the linked-list) we can utilize
	// this base value to modify/query ancestors from the root. Conserving memory by not 
	// storing pointers
	uint64_t base: 56;
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
	uint64_t bit_vector[4];
	// the leaf stores pointers
	// the ancestors leading to this path
	// Because ancestors are shared and leaves
	// are sparsely populated, storing pointers
	// at leaves and reverseing search upward 
	// can be useful, especially because the root
	// has an ordered linked-list of all leaves
	// this only get's populated
	// with non-null values

	// When inserting into outward
	// trees or the original insert
	// was null, propate a null value

	// Table of 8-bit keys => value of size
	// specified in root
	Fast_Table values;
	// this is a deque item 
	// whose pointer is to self
	Deque_Item leaf;
};


// These are the leaves for the auxiliary structures
// They will not be queried in the same way as normal tree,
// so we can conserve memory
struct fast_tree_outward_leaf {
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
	uint64_t bit_vector[4];
};

struct fast_tree_8 {
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
	// Table of 8-bit keys => fast_tree_leaf
	Fast_Table children;
	Fast_Tree_Leaf inward;
};

struct fast_tree_16 {
 	uint16_t cnt;
 	uint16_t min;
 	uint16_t max;
 	// table of 16-bit keys => fast_tree_8
 	Fast_Table inward;
	Fast_Tree_8 outward;
 };

 struct fast_tree_32 {
 	// the base of all 32-bit trees is 0
 	// (because its parent is root)
	uint16_t cnt;
	uint32_t min;
	uint32_t max;
	// table of 32 bit keys => fast_tree_16
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
	// if this fast tree will be containing
	// entries within the leaves of the tree.

	// If this is set to 0, then any value pointer
	// passed into the insert function is ignored
	// and any value returned from the find/remove value
	// functions are set to null
	uint64_t value_size_bytes;
};


// SOME HASH FUNCTIONS

uint64_t hash_func_64(void * key_ref, uint64_t table_size);
uint64_t hash_func_32(void * key_ref, uint64_t table_size);
uint64_t hash_func_16(void * key_ref, uint64_t table_size);
uint64_t hash_func_8(void * key_ref, uint64_t table_size);


// Initializes a root level fast tree where
// keys will initially be inserted

// If we want to store value's corresponding to the uint64_t key
// then set the value size upon initialization

// If the use case for this tree is only for ordered uint64_t lookups
// then can set value_size_bytes to 0. In this case, any value pointer
// passed into the insert function is ignored
// and any value returned from the find/remove value
// functions are set to null


Fast_Tree * init_fast_tree(uint64_t value_size_bytes);

// Internal functions

Fast_Tree_Leaf * get_fast_tree_leaf(Fast_Tree * fast_tree, uint64_t search_key, FastTreeSearchType search_type);





// Exposed functions

// Can insert any key in the range (0)
int insert_fast_tree(Fast_Tree * fast_tree, uint64_t key, void * value);

// If we only care the value satisfying the query for a given key and search type

// return 0 upon success and populates ret_key
// return -1 if not key satisfied search
int search_key_fast_tree(Fast_Tree * fast_tree, uint64_t search_key, FastTreeSearchType search_type, uint64_t * ret_key);

// If we want the key and value satisfying the request

// returns 0 upon success if there existed a key that satified request
//	if a key existed without a value, then ret_value is set to NULL and the user should check
//	if a key exsited that satified request and had a value, then the contents from teh 

// returns -1 if no key existed to s
// assumes that the user wants the value to by copied into ret_value
int search_fast_tree(Fast_Tree * fast_tree, uint64_t search_key, FastTreeSearchType search_type, uint64_t * ret_key, void * ret_value);












#endif