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
// Only the leaves for the all inward paths
typedef struct fast_tree_leaf Fast_Tree_Leaf;


// CONSERVING MEMORY BY NOT STORING UNNECESSARY INFO IN AUX-STRUCTURE

typedef struct fast_tree_outward_32 Fast_Tree_Outward_32;
typedef struct fast_tree_outward_16 Fast_Tree_Outward_16;
typedef struct fast_tree_outward_8 Fast_Tree_Outward_8;
typedef struct fast_tree_outward_leaf Fast_Tree_Outward_Leaf;

// Could further conserve memory by not storing count's 
// for any tree except the all inward path. For the 
// main tree is is important for querying, but has 
// no use in any other path. For now not doing this as 
// extra memory optimization.


// These are the leaves for the auxiliary structures
// They will not be queried in the same way as normal tree,
// so we can conserve memory
struct fast_tree_outward_leaf {
	uint64_t bit_vector[4];
};

 struct fast_tree_outward_8 {
	// Table of 8-bit keys => fast_tree_outward_leaf
	Fast_Table inward;
	Fast_Tree_Outward_Leaf outward;
};

struct fast_tree_outward_16 {
	// table of 8-bit keys => fast_tree_8
 	Fast_Table inward;
	Fast_Tree_Outward_8 outward;
};

struct fast_tree_outward_32 {
// table of 16 bit keys => fast_tree_16
	Fast_Table inward;
	Fast_Tree_Outward_16 outward;
};


typedef enum fast_tree_search_type {
	FAST_TREE_EQUAL,
	FAST_TREE_NEXT,
	FAST_TREE_EQUAL_OR_NEXT,
	FAST_TREE_PREV,
	FAST_TREE_EQUAL_OR_PREV
} FastTreeSearchType;


// THIS LEAF ONLY EXISTS FOR THE ALL INWARD PATH
// (i.e. for all fast_tree_8 tables, except the all inward path
// contain fast_tree_outward_leaf as their value)

// The all inward path has fast_table containing
// these leaves which store tables

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
	uint64_t bit_vector[4];
	// Table of 8-bit keys => value of size
	// specified in root
	Fast_Table values;
	// this is a deque item 
	// whose pointer is to self
	Deque_Item leaf;
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
};

struct fast_tree_8 {
	// Table of 8-bit keys => fast_tree_leaf
	Fast_Table inward;
	Fast_Tree_Outward_Leaf outward;
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
};

struct fast_tree_16 {
	// table of 8-bit keys => fast_tree_8
 	Fast_Table inward;
	Fast_Tree_Outward_8 outward;
 	uint16_t cnt;
 	uint16_t min;
 	uint16_t max;
 };


 struct fast_tree_32 {
	// table of 16 bit keys => fast_tree_16
	Fast_Table inward;
	Fast_Tree_Outward_16 outward;
	// the base of all 32-bit trees is 0
 	// (because its parent is root)
	uint32_t cnt;
	uint32_t min;
	uint32_t max;
 };




struct fast_tree {
	// configurations for all the tables
	// only saved once in memory meaning
	// enormous memory savings when we have
	// tons of tables
	Fast_Table_Config * table_config_32;
	Fast_Table_Config * table_config_16;
	Fast_Table_Config * table_config_8;
	// all of the leaves except the all inward
	// leaves will use this table
	Fast_Table_Config * table_config_outward_leaf;

	// For leaves tied to the core tree (all inward path)
	Fast_Table_Config * table_config_main_leaf;

	// For the table within the main leaf that actually
	// stores the values that are inserted
	Fast_Table_Config * table_config_value;
	

	// hash table of index segments
	// the key within this table is the 
	// a uint32_t index ahd the value will be a 
	// Fast_Tree_16, which will represent
	// A tree searching for the offset
	Fast_Table inward;
	// This represents a Fast_Tree_32 seraching
	// for the uint32_t index of the original elmeent
	Fast_Tree_Outward_32 outward;
	
	// if this fast tree will be containing
	// entries within the leaves of the tree.

	// If this is set to 0, then any value pointer
	// passed into the insert function is ignored
	// and any value returned from the find/remove value
	// functions are set to null
	uint64_t cnt;
	uint64_t min;
	uint64_t max;
	// doubly linked list of non-null
	// leaves of the tree, each of which
	// can contain 256 key-value pairs within
	// a contiguous range
	Deque * ordered_leaves;
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