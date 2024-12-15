#ifndef FAST_TREE_H
#define FAST_TREE_H

#include "common.h"
#include "config.h"
#include "fast_table.h"


// this is a modifier to specify if we want
// only equal key, only >, >=, <, or <= with respect
// to the queried key with respect
// to either a search request or an update request
typedef enum fast_tree_search_modifier {
	FAST_TREE_MIN,
	FAST_TREE_MAX,
	FAST_TREE_PREV,
	FAST_TREE_NEXT,
	FAST_TREE_EQUAL,
	FAST_TREE_EQUAL_OR_PREV,
	FAST_TREE_EQUAL_OR_NEXT
} FastTreeSearchModifier;


// A smart update operation is a combination of a search + 
// insert and/or delete based upon the key that satisfied the
// search query.

// REMOVE_KEY: search + remove_fast_tree()
// SWAP_KEY: search() + remove_fast_tree() + insert_fast_tree()
// SWAP_KEY_VALUE: search + remove_fast_tree() + insert_fast_tree()
// REMOVE_VALUE: only search, but modifies the table in the leaf
// UPDATE_VALUE: only search(), but modifies the table in the leaf
// COPY_VALUE: search() +  insert_fast_tree()


// The overwrite flags require a non-null 
typedef enum fast_tree_update_op_type {
	// This removes key that satified the search query
	// (relative to the search key argument) from the tree 
	// along with its assoicated value in the leaf (if exists)

	// To remove a specific key, use the FAST_TREE_EQUAL
	// search modifier and set search_key = key

	// - Ignores "new_key" and "new_value" within update_op_params
	REMOVE_KEY,
	// this is equivalent to calling copy value, to obtain
	// the value that should be inserted with new key,
	// and then remove_key where key = satified search 
	// result
	// - Ignores the "value" field within op_params

	// Note: if serach key modifier is set to FAST_TREE_EQUAL
	// than this has no effect.
	SWAP_KEY,
	
	// this is equivalent to removing the preivously satisfied
	// key (and value if non-null) relative to the search key 
	// and then inserting the (key, value) from op_params

	// (same effect of REMOVE_KEY using this search key,
	// and then calling insert)
	SWAP_KEY_VALUE,
	// This removes only the value assoicated with the 
	// key that satified search request (meaning it removes
	// the value from the leaf's values table). It leaves
	// the key in the tree.
	// - Ignores the "key" and "value" fields within update_op_params

	// To remove a value from a specific key, use the
	// FAST_TREE_EQUAL search modifier and set search_key = key
	REMOVE_VALUE,

	// this changes the value assoicated
	// with key satisfying search query in the leaf of the tree.
	// - Ignores the "key" field within op_params

	// With search modifier FAST_TREE_EQUAL, equivalent to 
	// insert with key = search_key and overwrite flag = true
	UPDATE_VALUE,
	
	// this is equivalent to searching for the key that satifies
	// request, obtaining the value, and inserting the new key
	// into the tree with the previous value
	// - Ignores the "value" field within op_params
	COPY_VALUE
} FastTreeUpdateOpType;



// NOTE: THE UGLY TYPE SPECIFIC LEVELS IS DONE PURPOSEFULLY FOR PERFORMANCE REASONS!
//			- less memory usage and can define constants within the functions...


typedef struct fast_tree Fast_Tree;
typedef struct fast_tree_32 Fast_Tree_32;
typedef struct fast_tree_16 Fast_Tree_16;
// Only the leaves for the all inward paths
typedef struct fast_tree_leaf Fast_Tree_Leaf;


// CONSERVING MEMORY BY NOT STORING UNNECESSARY INFO IN AUX-STRUCTURE

typedef struct fast_tree_outward_root_32 Fast_Tree_Outward_Root_32;
typedef struct fast_tree_outward_root_16 Fast_Tree_Outward_Root_16;
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
	uint8_t min;
	uint8_t max;
	uint64_t bit_vector[4];
};

struct fast_tree_outward_root_16 {
	// table of 8-bit keys => Fast_tree_Outward_Leaf
 	Fast_Table inward_leaves;
	Fast_Tree_Outward_Leaf outward_leaf;
};

struct fast_tree_outward_root_32 {
// table of 16 bit keys => fast_tree_16
	Fast_Table inward;
	Fast_Tree_Outward_Root_16 outward_root;
};



// the user will pass in this struct with populated
// values that correspond to the update type they 
// requested.
typedef struct fast_tree_smart_update_params {
	FastTreeUpdateOpType op_type;
	uint64_t new_key;
	void * new_value;
	// To_overwrite is relelvant for update operations of type:
	//	- SWAP_KEY
	//	- SWAP_KEY_VALUE
	//	- COPY_VALUE

	// These update types perform an insert on a key that
	// is different from the key corresponding to result 
	// of search query, so there is the possiblility
	// that the "new_key" passed in
	bool to_overwrite_new_key;
} Fast_Tree_Smart_Update_Params;


// This struct is set in both search() and smart_update(), but 
// can take on slightly different meanings.
typedef struct fast_tree_result {

	// If this result is returned by a search() or smart_update() and no
	// key satisfied the query then the functions returns -1
	// and leaf and value are set to NULL in this struct.
	// In this case the value of the "key" field should be ignored.


	// If this result is returned by an update operation
	// with type of:

	//	- REMOVE_KEY
	//	- SWAP_KEY
	//	- SWAP_KEY_VALUE

	// Then there is a chance that the leaf corresponding to the key
	// that satisfied the search query has been removed from the tree. 
	// This occurs when the key that satisisfied search
	// query was the only remaining key within that leaf. In these
	// scenarios this pointer is set to NULL. Key and value are 
	// still set according, though.
	Fast_Tree_Leaf * fast_tree_leaf;
	// Returns the key that satisfied that
	// search query (same meaning)
	uint64_t key;
	// For regular searches value is set to the current value
	// corresponding the key that satisfied search query

	// For update operations, value is set the value corresponding
	// to the search key BEFORE the update was performed.
	void * value;


	// "new_key_prev_value" is only relevant
	// if this result is returned by an update operation
	// with one of the following types: 

	//	- SWAP_KEY
	//	- SWAP_KEY_VALUE
	//	- COPY_VALUE

	// If "new_key" already existed, then
	// this field is set the previous value of "new_key" (not
	// the key that satisfied the satisfied search result)

	// If this result is returned by a search() or a smart_update()
	// not of one of the above types, or if it was one of the above
	// types and "new_key" did not exist, then this field is set to NULL
	void * new_key_prev_value;
} Fast_Tree_Result;





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

	// NOTE: here the .inward[i].inward involves:
	//	- Finding key i within the tree's inward table, which then returns
	//		a Fast_Tree at a lower level
	//	- If the fast_tree_8 function still has is_main_tree flag set to true
	//		then it inserts this leaf type into it's table. Otherwise

	// Becaues root.inward[base & 0xFFFF] is a Fast_Tree_16, then get's to a Fast_Tree_8
	// and then gets to this leaf

	// Once we traverse to the leaf (or start there from the linked-list) we can utilize
	// this base value to modify/query ancestors from the root. Conserving memory by not 
	// storing pointers

	// the lower 8 bits of base should be 0...
	uint64_t base;
	uint64_t bit_vector[4];
	// Table of 8-bit keys => value of size
	// specified in root
	Fast_Table values;
	Fast_Tree_Leaf * prev;
	Fast_Tree_Leaf * next;
	uint8_t cnt;
	uint8_t min;
	uint8_t max;
};


struct fast_tree_16 {
	// table of 8-bit keys => fast_tree_8
 	Fast_Table inward_leaves;
	Fast_Tree_Outward_Leaf outward_leaf;
 	uint16_t cnt;
 	uint16_t min;
 	uint16_t max;
 };


 struct fast_tree_32 {
	// table of 16 bit keys => fast_tree_16
	Fast_Table inward;
	Fast_Tree_Outward_Root_16 outward_root;
	// the base of all 32-bit trees is 0
 	// (because its parent is root)
	uint32_t cnt;
	uint32_t min;
	uint32_t max;
 };


typedef struct fast_tree_stats {
	uint32_t num_trees_32;
	uint32_t num_trees_16;
	uint64_t num_leaves;
	uint32_t num_nonmain_trees_16;
	uint64_t num_outward_leaves;
} Fast_Tree_Stats;



struct fast_tree {
	// configurations for all the tables
	// only saved once in memory meaning
	// enormous memory savings when we have
	// tons of tables
	Fast_Table_Config * table_config_32;
	Fast_Table_Config * table_config_16;
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
	Fast_Tree_Outward_Root_32 outward_root;
	
	// if this fast tree will be containing
	// entries within the leaves of the tree.

	// If this is set to 0, then any value pointer
	// passed into the insert function is ignored
	// and any value returned from the find/remove value
	// functions are set to null
	uint64_t cnt;
	uint64_t min;
	uint64_t max;
	Fast_Tree_Stats tree_stats;
	// doubly linked list of non-null
	// leaves of the tree, each of which
	// can contain 256 key-value pairs within
	// a contiguous range
	Fast_Tree_Leaf * min_leaf;
	Fast_Tree_Leaf * max_leaf;
};


// These hash functions cast key to the apprporiate width
// and then take modulus with table_size

// Note: that the hash function within each table in the tree is the simple modulus
// of the table size. If we assume uniform distribution of keys across the key-space
// at each level (32, 16, 8, 8) then this is the best we can do and no need
// to be fancy. It takes care of linearly-clustered regions by default, unless there are unique 
// patterns that exist between levels
uint64_t hash_func_modulus_64(void * key_ref, uint64_t table_size);
uint64_t hash_func_modulus_32(void * key_ref, uint64_t table_size);
uint64_t hash_func_modulus_16(void * key_ref, uint64_t table_size);
uint64_t hash_func_modulus_8(void * key_ref, uint64_t table_size);


// Initializes a root level fast tree where
// keys will initially be inserted

// If we want to store value's corresponding to the uint64_t key
// then set the value size upon initialization

// If the use case for this tree is only for ordered uint64_t lookups
// then can set value_size_bytes to 0. In this case, any value pointer
// passed into the insert function is ignored
// and any value returned from the find/remove value
// functions are set to null


Fast_Tree * init_fast_tree();


// ONLY TEMPORARILY EXPOSING THIS FUNCTION!
Fast_Tree_Leaf * get_leaf(Fast_Tree * fast_tree, uint64_t key);


// Exposed functions

// Only exposing the top level functions that operate on 64-bit keys


// reutnrs 0 on success -1 if no satisfying search result
// sets the search result
int search_fast_tree(Fast_Tree * fast_tree, uint64_t search_key, FastTreeSearchModifier search_type, Fast_Tree_Result * ret_search_result);


// returns 0 on success -1 on error
// fails is key is already in the tree and overwrite set to false
// if key was already in the tree and had a non-null value, then copies the previous value into prev_value
int insert_fast_tree(Fast_Tree * fast_tree, uint64_t key, void * value, bool to_overwrite, void * prev_value);


// returns 0 on success -1 on error
// fails is key is not in the tree
int remove_fast_tree(Fast_Tree * fast_tree, uint64_t key, void * prev_value);




// This provides a single API for a very large set of possible behaviors

// It always conducts a search and then performs an action conditional on the
// returned search result.

// Returns 0 upon "success", otherwise -1 on error

// Success is almost always defined as the success of the search query.

// The only exception is in the case that:
//	a.) to_overwrite = false, 
// 	b.) update is of one the following types:
//		- SWAP_KEY
//		- SWAP_KEY_VALUE
//		- COPY_VALUE
//	c.) "new_key" already existed in the tree

// In this case the insertion would fail so the update returns failure, but 
// still conducts and returns the result from searching and sets "new_key_prev_val"
// within the Fast_Tree_Result accordingly (even though no changes were made to the tree
// the caller might want to know the previous value). 
int smart_update_fast_tree(Fast_Tree * fast_tree, uint64_t search_key, FastTreeSearchModifier search_type, 
								Fast_Tree_Smart_Update_Params smart_update_params, Fast_Tree_Result * ret_smart_update_result);








#endif
