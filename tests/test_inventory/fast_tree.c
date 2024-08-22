#include "fast_tree.h"


// Note: that the hash function within each table in the tree is the simple modulus
// of the table size. If we assume uniform distribution of keys across the key-space
// at each level (32, 16, 8, 8) then this is the best we can do and no need
// to be fancy. It takes care of linearly-clustered regions by default, unless there are unique 
// patterns that exist between levels

uint64_t hash_func_modulus_32(void * key_ref, uint64_t table_size) {
	uint32_t key = *((uint32_t *) key_ref);
	return key % table_size;
}

uint64_t hash_func_modulus_16(void * key_ref, uint64_t table_size){
	uint16_t key = *((uint16_t *) key_ref);
	return key % table_size;
}

uint64_t hash_func_modulus_8(void * key_ref, uint64_t table_size){
	uint8_t key = *((uint8_t *) key_ref);
	return key % table_size;
}



// THE FAST TREE FUNCTIONS!


int fast_tree_leaf_cmp(void * fast_tree_leaf, void * other_fast_tree_leaf){

	uint64_t base = ((Fast_Tree_Leaf *) fast_tree_leaf) -> base;
	uint64_t other_base = ((Fast_Tree_Leaf *) fast_tree_leaf) -> base;
	return base - other_base;

}




// this intializes a top level fast tree
Fast_Tree * init_fast_tree(uint64_t value_size_bytes) {

	Fast_Tree * fast_tree = (Fast_Tree *) malloc(sizeof(Fast_Tree));
	if (!fast_tree){
		fprintf(stderr, "Error: malloc failed to allocate the fast tree root container\n");
		return NULL;
	}


	// HERE WE ARE STORING THE HYPERPARAMTERS FOR THE TREES AT EACH LEVEL!
	//	- it would be very wasteful to have each table store the same configuration information


	// these parameters are definied within config.h
	// not that the maximum number of keys in this table is 2^32


	// All of these tables fast_table_X_configs
	// correspond to the table's that are within X's parent

	// Thus the root stores a fast_table_32, the fast_table_32's store a fast_table_16, etc...

	Fast_Table_Config * table_config_32 = save_fast_table_config(&hash_func_modulus_32, sizeof(uint32_t), sizeof(Fast_Tree_32), 
						FAST_TREE_32_MIN_TABLE_SIZE, FAST_TREE_32_MAX_TABLE_SIZE, FAST_TREE_32_LOAD_FACTOR, FAST_TREE_32_SHRINK_FACTOR);
	if (!table_config_32){
		fprintf(stderr, "Error: failed to create 32 config\n");
		return NULL;
	}

	Fast_Table_Config * table_config_16 = save_fast_table_config(&hash_func_modulus_16, sizeof(uint16_t), sizeof(Fast_Tree_16), 
						FAST_TREE_16_MIN_TABLE_SIZE, FAST_TREE_16_MAX_TABLE_SIZE, FAST_TREE_16_LOAD_FACTOR, FAST_TREE_16_SHRINK_FACTOR);
	if (!table_config_16){
		fprintf(stderr, "Error: failed to create 16 config\n");
		return NULL;
	}

	Fast_Table_Config * table_config_outward_leaf = save_fast_table_config(&hash_func_modulus_8, sizeof(uint8_t), sizeof(Fast_Tree_Outward_Leaf), 
						FAST_TREE_OUTWARD_LEAF_MIN_TABLE_SIZE, FAST_TREE_OUTWARD_LEAF_MAX_TABLE_SIZE, FAST_TREE_OUTWARD_LEAF_LOAD_FACTOR, FAST_TREE_OUTWARD_LEAF_SHRINK_FACTOR);
	if (!table_config_outward_leaf){
		fprintf(stderr, "Error: failed to create aux leaf config\n");
		return NULL;
	}



	// This is the table for the all vertical path in the main tree (which actually stores all the keys)
	// These leaves will have Fast_Table with values that were inserted into the tree and also have linked list pointers
	// to the other main leaves

	// NOTE THAT THE TYPE IN THIS TABLE IS A POINTER TO A LEAF!
	// We want to save the pointers the leaves because we will have a linked list that connects them.
	// We do not want their addresses to change upon leaf table resizing

	// The fast_tree_8's still part of main tree are responsible for calling create_fast_tree_leaf() if the index of the leaf
	// does not exist in the fast_tree_8's inward table. 

	// This function is responsible for:
	//	 a.) allocating memory for the leaf
	//	 b.) Initializing the leaf's values table (if value_size_bytes > 0)
	//	 c.) Initializing a deque item for itself to be linked to prev and next leaves
	//	 d.) Calling search for keys with base - 1 and base + 256 to get fast tree leaves
	//			for prev and succ (whose deque items can be then be linked)
	//	 e.) If no prev setting root -> ordered_leaves -> head to be this newly created deque item
	//			- and same for tail

	Fast_Table_Config * table_config_main_leaf = save_fast_table_config(&hash_func_modulus_8, sizeof(uint8_t), sizeof(Fast_Tree_Leaf *), 
						FAST_TREE_MAIN_LEAF_MIN_TABLE_SIZE, FAST_TREE_MAIN_LEAF_MAX_TABLE_SIZE, FAST_TREE_MAIN_LEAF_LOAD_FACTOR, FAST_TREE_MAIN_LEAF_SHRINK_FACTOR);
	if (!table_config_main_leaf){
		fprintf(stderr, "Error: failed to create main_leaf config\n");
		return NULL;
	}


	// Notice that the value size bytes for this table is the value that was passed in to this intialization function!!!
	Fast_Table_Config * table_config_value = save_fast_table_config(&hash_func_modulus_8, sizeof(uint8_t), value_size_bytes, 
						FAST_TREE_VALUE_MIN_TABLE_SIZE, FAST_TREE_VALUE_MAX_TABLE_SIZE, FAST_TREE_VALUE_LOAD_FACTOR, FAST_TREE_VALUE_SHRINK_FACTOR);
	if (!table_config_value){
		fprintf(stderr, "Error: failed to create main_leaf config\n");
		return NULL;
	}


	fast_tree -> table_config_32 = table_config_32;
	fast_tree -> table_config_16 = table_config_16;
	fast_tree -> table_config_outward_leaf = table_config_outward_leaf;
	fast_tree -> table_config_main_leaf = table_config_main_leaf;
	fast_tree -> table_config_value = table_config_value;


	fast_tree -> cnt = 0;

	// ensure that the first item will reset min and max appropriately
	fast_tree -> min = 0xFFFFFFFFFFFFFFFF;
	fast_tree -> max = 0;


	// Special case in the very root to initialize table
	// without any contents. 

	// The root is the sole the 64-bit tree.

	// When a tree is created it intializes it's inward table.

	// All other tables are only initialized when the index is not contained
	// with the inward table. In this case the current tree's inward table is 
	// always initialized (with configuration 1 level below current tree) and the offset 
	// (which is half the number of bits of current tree) is inserted into this table. 

	// If this occurs and the inward table of the 
	// outward root has not been intialized (can check if (tree -> outward_root -> inward -> items == NULL, 
	// the current tree also initialzes the inward table of the outward root with the configuration corresponding
	// to 1 level below the current tree's level..

	int ret = init_fast_table(&(fast_tree -> inward), table_config_32);
	if (ret != 0){
		fprintf(stderr, "Error: failure to initialize fast tree root's inward table\n");
		return NULL;
	}


	// Can initialize the root's outward root's inward tree here.
	ret = init_fast_table(&(fast_tree -> outward_root.inward), table_config_32);
	if (ret != 0){
		fprintf(stderr, "Error: failure to initialize fast tree root's outward root's inward table\n");
		return NULL;
	}

	Item_Cmp leaf_cmp = &fast_tree_leaf_cmp;
	fast_tree -> ordered_leaves = init_deque(leaf_cmp);
	if (!(fast_tree -> ordered_leaves)){
		fprintf(stderr, "Error: failure to initialize the ordered_leaves deque\n");
		return NULL;
	}


	return fast_tree;

}

// ASSUMING length 4 bitvector

// returns -1 if exists, otherwise 0
int set_bitvector(uint64_t * bitvector, uint8_t key){

	uint8_t vec_ind = (key & 0xC0) >> 6;
	uint8_t bit_ind = (key & 0x3F);

	// check if already set
	if ((bitvector[vec_ind] & (1 << bit_ind)) >> bit_ind){
		return -1;
	}

	bitvector[vec_ind] |= (1 << bit_ind);
	return 0;
}


// This is only called upon leaves part of the main tree
// It is responsible for inserting the value originally passed
// in to the leaf's value table.

	// This function is responsible for:
	//	 a.) allocating memory for the leaf
	//	 b.) Initializing the leaf's values table (if value_size_bytes > 0)
	//	 c.) Initializing a deque item for itself to be linked to prev and next leaves
	//	 d.) Calling search for keys with base - 1 and base + 256 to get fast tree leaves
	//			for prev and succ (whose deque items can be then be linked)
	//	 e.) If no prev setting root -> ordered_leaves -> head to be this newly created deque item
	//			- and same for tail
Fast_Tree_Leaf * create_and_link_fast_tree_leaf(Fast_Tree * root, uint8_t key, void * value, bool to_overwrite, void * prev_value, uint64_t base){

	Fast_Tree_Leaf * fast_tree_leaf = (Fast_Tree_Leaf *) malloc(sizeof(Fast_Tree_Leaf));
	if (unlikely(!fast_tree_leaf)){
		fprintf(stderr, "Error: malloc failed to allocate a fast tree leaf\n");
		return NULL;
	}

	fast_tree_leaf -> base = base;
	fast_tree_leaf -> cnt = 1;
	fast_tree_leaf -> max = key;
	fast_tree_leaf -> min = key;

	// set bit vector
	memset(fast_tree_leaf -> bit_vector, 0, 4 * sizeof(uint64_t));
	set_bitvector(fast_tree_leaf -> bit_vector, key);

	// deal with inserting value
	int ret;
	if (root -> value_size_bytes > 0){
		ret = init_fast_table(&(fast_tree_leaf -> values), root -> table_config_value);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to init value table in leaf\n");
			return NULL;
		}

		if (value != NULL){
			uint8_t value_key = key;
			ret = insert_fast_table(&(fast_tree_leaf -> values), &value_key, value);
			if (unlikely(ret != 0)){
				fprintf(stderr, "Error: failure to insert value into the table in leaf\n");
				return NULL;
			}
		}
	}
	

	// need to initialize deque item
	// neee to call search to get prev and next leaves and link this leaf in between
	//	- potentially changing the head/tail of root -> ordered_leaves deque



	return fast_tree_leaf;

}

// If is_main_tree and the key is not in inward table, call create_and_link_fast_tree_leaf
// and then insert the resulting pointer to this tree's inward table

// If is_main_tree and key already is in the inward table, then insert value into the leaf's value table

// If not in main tree and key doesn't exist in inward table, responsible for intializing a outward_leaf (can do so on stack)
// and setting the appropriate bit position corresponding to key within the outward_leaf's bit vector. Then will insert
// this newly created outward leaf (that only contains the bit vector) in the table (which will memcpy the contents)

// If not in main tree and key exists (using find_fast_table with to_copy_value set to false)
// the returned value will be a pointer to an outward leaf (which is just 4 uint64_t's) which has been allocated within
// the table -> items array. Now we can modify this returned value inplace.


int insert_fast_tree_16(Fast_Tree * root, Fast_Tree_16 * fast_tree, uint16_t key, void * value, bool to_overwrite, void * prev_value, uint64_t base){

	if (key < fast_tree -> min){
		fast_tree -> min = key;
	}
	if (key > fast_tree -> max){
		fast_tree -> max = key;
	}

	fast_tree -> cnt += 1;

	uint8_t ind_8 = (key & 0xFF00) >> 8;
	uint8_t off_8 = (key & 0x00FF);

	// this is part of the main tree
	Fast_Tree_Leaf * inward_leaf;
	Fast_Tree_Outward_Leaf outward_leaf = fast_tree -> outward_leaf;

	uint64_t ret = find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, &inward_leaf);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (ret == fast_tree -> inward_leaves.config -> max_size){

		if (fast_tree -> inward_leaves.items == NULL){
			init_fast_table(&(fast_tree -> inward_leaves), root -> table_config_main_leaf);
		}

		// SHOULD CREATE AND LINK LEAVES HERE!

		inward_leaf = create_and_link_fast_tree_leaf(root, off_8, value, to_overwrite, prev_value, base + (uint64_t) (ind_8 << 8));
		if (!inward_leaf){
			fprintf(stderr, "Error: failure to create and link fast tree leaf\n");
			return -1;
		}

		ret = insert_fast_table(&(fast_tree -> inward_leaves), &off_8, &inward_leaf);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table\n");
			return -1;
		}


		set_bitvector(outward_leaf.bit_vector, ind_8);


		// we know there was no previous value if there was no leaf corresponding to this key before
		prev_value = NULL;
		
	}
	else{


		// insert off_8 into the inward leaf with value and return prev value
		int in_bitvector = set_bitvector(inward_leaf -> bit_vector, off_8);
		
		void * prev_value_table;

		// this value was already in table
		if (in_bitvector != 0){
			
			ret = find_fast_table(&(inward_leaf -> values), &off_8, false, (void *) &prev_value_table);
			// if there was a value corresponding to this key before
			if (ret != inward_leaf -> values.config -> max_size){
				if (root -> value_size_bytes > 0){
					memcpy(prev_value, prev_value_table, root -> value_size_bytes);
				}
				if (!to_overwrite){
					fprintf(stderr, "Error: item already existed in bitvector\n");
					return -1;
				}
				else{
					if ((root -> value_size_bytes > 0) && (value != NULL)){
						memcpy(prev_value_table, value, root -> value_size_bytes);
					}
					else {
						remove_fast_table(&(inward_leaf -> values), &off_8, false, NULL);
					}
				}
			}
		}
		else{
			if ((root -> value_size_bytes > 0) && (value != NULL)){
				ret = insert_fast_table(&(inward_leaf -> values), &off_8, value);
				if (unlikely(ret != 0)){
					fprintf(stderr, "Error: failure to insert value in the leaf's value table\n");
					return -1;
				}
			}
		}

		// if ind_8 leaf was alreay created we know that it was inserted into the outward_leaf already
	}
	return 0;
}

int insert_fast_tree_nonmain_16(Fast_Tree * root, Fast_Tree_16 * fast_tree, uint16_t key){

	if (key < fast_tree -> min){
		fast_tree -> min = key;
	}
	if (key > fast_tree -> max){
		fast_tree -> max = key;
	}

	fast_tree -> cnt += 1;

	uint8_t ind_8 = (key & 0xFF00) >> 8;
	uint8_t off_8 = (key & 0x00FF);

	// if the inward leaves on non-main trees are just outward leaves
	Fast_Tree_Outward_Leaf inward_leaf;
	Fast_Tree_Outward_Leaf outward_leaf = fast_tree -> outward_leaf;

	uint64_t ret = find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, &inward_leaf);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (ret == fast_tree -> inward_leaves.config -> max_size){
		if (fast_tree -> inward_leaves.items == NULL){
			init_fast_table(&(fast_tree -> inward_leaves), root -> table_config_outward_leaf);
		}


		// need to modify the inward_leaf to initialize the inward leaf bitvector
		// and then add to table


		// need to modify the outward_leaf to initialize the outward_leaf bitvector
		// and add ind_8

		
	}
	else{

		// insert off_8 into the inward leaf with value

		// insert ind_8 into the fast_tree -> outward_leaf bitvector


	}
	return ret;
}

int insert_fast_tree_outward_16(Fast_Tree * root, Fast_Tree_Outward_Root_16 * fast_tree, uint16_t key) {

   	
	uint8_t ind_8 = (key & 0xFF00) >> 8;
	uint8_t off_8 = (key & 0x00FF);

	// The inward leaf of an outward root is still and outward_leaf type
	Fast_Tree_Outward_Leaf inward_leaf;
	Fast_Tree_Outward_Leaf outward_leaf = fast_tree -> outward_leaf;

	uint64_t ret = find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, &inward_leaf);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (ret == fast_tree -> inward_leaves.config -> max_size){
		ret = insert_fast_table(&(fast_tree -> inward_leaves), &ind_8, &inward_leaf);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table\n");
			return -1;
		}


		// need to modify the outward_leaf to initialize the outward_leaf bitvector
		// and add ind_8
	}
	else{
		// insert off_8 into the inward leaf
	}
	return ret;

}

int insert_fast_tree_32(Fast_Tree * root, Fast_Tree_32 * fast_tree, uint32_t key, void * value, bool to_overwrite, void * prev_value, uint64_t base){

	if (key < fast_tree -> min){
		fast_tree -> min = key;
	}
	if (key > fast_tree -> max){
		fast_tree -> max = key;
	}

	fast_tree -> cnt += 1;

	uint16_t ind_16 = (key & 0xFFFF0000) >> 16;
	uint16_t off_16 = (key & 0x0000FFFF);

	Fast_Tree_16 inward_tree_16;

	uint64_t ret = find_fast_table(&(fast_tree -> inward), &ind_16, false, &inward_tree_16);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (ret == fast_tree -> inward.config -> max_size){
		if (fast_tree -> inward.items == NULL){
			init_fast_table(&(fast_tree -> inward), root -> table_config_16);
		}
		ret = insert_fast_table(&(fast_tree -> inward), &ind_16, &inward_tree_16);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table\n");
			return -1;
		}
		if (fast_tree -> outward_root.inward_leaves.items == NULL){
			init_fast_table(&(fast_tree -> outward_root.inward_leaves), root -> table_config_outward_leaf);
		}
		ret = insert_fast_tree_outward_16(root, &(fast_tree -> outward_root), ind_16);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert into outward tree\n");
			return -1;
		}
	}
	

	ret = insert_fast_tree_16(root, &inward_tree_16, off_16, value, to_overwrite, prev_value, base + (uint64_t) ((ind_16) << 16));
	return ret;

}

int insert_fast_tree_outward_32(Fast_Tree * root, Fast_Tree_Outward_Root_32 * fast_tree, uint32_t key){

	uint16_t ind_16 = (key & 0xFFFF0000) >> 16;
	uint16_t off_16 = (key & 0x0000FFFF);

	Fast_Tree_16 inward_tree_16;

	uint64_t ret = find_fast_table(&(fast_tree -> inward), &ind_16, false, &inward_tree_16);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (ret == fast_tree -> inward.config -> max_size){
		ret = insert_fast_table(&(fast_tree -> inward), &ind_16, &inward_tree_16);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table\n");
			return -1;
		}
		ret = insert_fast_tree_outward_16(root, &(fast_tree -> outward_root), ind_16);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert into outward tree\n");
			return -1;
		}
	}
	
	ret = insert_fast_tree_nonmain_16(root, &inward_tree_16, off_16);
	return ret;


}

// returns 0 on success -1 on error
// fails is key is already in the tree and overwrite set to false
// if key was already in the tree and had a non-null value, then copies the previous value into prev_value
int insert_fast_tree(Fast_Tree * fast_tree, uint64_t key, void * value, bool to_overwrite, void * prev_value) {

	// SHOULD CHECK IF KEY EXISTS FIRST

	if (key < fast_tree -> min){
		fast_tree -> min = key;
	}
	if (key > fast_tree -> max){
		fast_tree -> max = key;
	}

	fast_tree -> cnt += 1;

	uint32_t ind_32 = (key & 0xFFFFFFFF00000000) >> 32;
	uint32_t off_32 = (key & 0x00000000FFFFFFFF);

	Fast_Tree_32 inward_tree_32;

	uint64_t ret = find_fast_table(&(fast_tree -> inward), &ind_32, false, &inward_tree_32);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (ret == fast_tree -> inward.config -> max_size){
		ret = insert_fast_table(&(fast_tree -> inward), &ind_32, &inward_tree_32);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table\n");
			return -1;
		}
		ret = insert_fast_tree_outward_32(fast_tree, &(fast_tree -> outward_root), ind_32);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert into outward tree\n");
			return -1;
		}
	}
	
	ret = insert_fast_tree_32(fast_tree, &inward_tree_32, off_32, value, to_overwrite, prev_value, (uint64_t) ind_32 << 32);
	return ret;



}





































// reutnrs 0 on success -1 if no satisfying search result
// sets the search result
int search_fast_tree(Fast_Tree * fast_tree, uint64_t search_key, FastTreeSearchModifier search_type, Fast_Tree_Result * ret_search_result) {
	fprintf(stderr, "Unimplemented Error: search_fast_tree\n");
	return -1;

}


// returns 0 on success -1 on error
// fails is key is not in the tree
int remove_fast_tree(Fast_Tree * fast_tree, uint64_t key, void * prev_value) {
	fprintf(stderr, "Unimplemented Error: remove_fast_tree\n");
	return -1;
}


// This provides a single function interface for a very large set of possible behaviors
// See FastTreeUpdateOpType enum definition in fast_tree.h for a description of 
// each operation. 

// In general, a smart update operation is a combination of:

// a.) A search  
// b.) One or more of the following:
//		- A value modification 
//		- An insert_fast_tree() call
//		- A remove_fast_tree() call

// REMOVE_KEY: search + remove_fast_tree()
// SWAP_KEY: search() + remove_fast_tree() + insert_fast_tree()
// SWAP_KEY_VALUE: search + remove_fast_tree() + insert_fast_tree()
// REMOVE_VALUE: only search, but modifies the table in the leaf
// UPDATE_VALUE: only search(), but modifies the table in the leaf
// COPY_VALUE: search() +  insert_fast_tree()

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
								Fast_Tree_Smart_Update_Params smart_update_params, Fast_Tree_Result * ret_smart_update_result){


	fprintf(stderr, "Unimplemented Error: smart_update_fast_tree\n");
	return -1;

}

