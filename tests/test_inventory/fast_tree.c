#include "fast_tree.h"

#define ALL_ONES_64 0xFFFFFFFFFFFFFFFF
#define TREE_MAX ALL_ONES_64

#define IND_32_MASK 0xFFFFFFFF00000000
#define OFF_32_MASK 0x00000000FFFFFFFF

#define IND_16_MASK 0xFFFF0000
#define OFF_16_MASK 0x0000FFFF

#define IND_8_MASK 0xFF00
#define OFF_8_MASK 0x00FF

// top 2 bits
#define LEAF_VEC_IND_MASK 0xC0
// lower 6 bits
#define LEAF_BIT_POS_MASK 0x3F


// top 56 bits
#define LEAF_BASE_MASK 0xFFFFFFFFFFFFFF00
// lower 8 bits
#define LEAF_KEY_MASK 0x00000000000000FF


uint64_t hash_func_modulus_64(void * key_ref, uint64_t table_size) {
	uint64_t key = *((uint64_t *) key_ref);
	return key % table_size;
}

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
Fast_Tree * init_fast_tree() {

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
	Fast_Table_Config * table_config_value = save_fast_table_config(&hash_func_modulus_8, sizeof(uint8_t), sizeof(void *), 
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
	fast_tree -> min = TREE_MAX;
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


	memset(&(fast_tree -> outward_root), 0, sizeof(Fast_Tree_Outward_Root_32));

	// Can initialize the root's outward root's inward tree here.
	ret = init_fast_table(&(fast_tree -> outward_root.inward), table_config_16);
	if (ret != 0){
		fprintf(stderr, "Error: failure to initialize fast tree root's outward root's inward table\n");
		return NULL;
	}

	fast_tree -> min_leaf = NULL;
	fast_tree -> max_leaf = NULL;


	(fast_tree -> tree_stats).num_trees_32 = 0;
	(fast_tree -> tree_stats).num_trees_16 = 0;
	(fast_tree -> tree_stats).num_leaves = 0;
	(fast_tree -> tree_stats).num_nonmain_trees_16 = 0;
	(fast_tree -> tree_stats).num_outward_leaves = 0;

	return fast_tree;

}

// ASSUMING length 4 bitvector

// returns -1 if exists, otherwise 0
int set_bitvector(uint64_t * bit_vector, uint8_t key){

	// upper 2 bits
	uint8_t vec_ind = (key & LEAF_VEC_IND_MASK) >> 6;
	// lower 6 bits
	uint8_t bit_ind = (key & LEAF_BIT_POS_MASK);

	// check if already set

	// Note: Need to add UL otherwise compiler will assume of type bit_ind
	if ((bit_vector[vec_ind] & (1ULL << bit_ind)) >> bit_ind){
		return -1;
	}

	bit_vector[vec_ind] |= (1ULL << bit_ind);

	return 0;
}

bool check_bitvector(uint64_t * bit_vector, uint8_t key){

	// upper 2 bits
	uint8_t vec_ind = (key & LEAF_VEC_IND_MASK) >> 6;
	// lower 6 bits
	uint8_t bit_ind = (key & LEAF_BIT_POS_MASK);

	// check if already set

	// Note: Need to add UL otherwise compiler will assume of type bit_ind
	if ((bit_vector[vec_ind] & (1ULL << bit_ind)) >> bit_ind){
		return true;
	}

	return false;
}


void clear_bitvector(uint64_t * bit_vector, uint8_t key){

	// upper 2 bits
	uint8_t vec_ind = (key & LEAF_VEC_IND_MASK) >> 6;
	// lower 6 bits
	uint8_t bit_ind = (key & LEAF_BIT_POS_MASK);

	bit_vector[vec_ind] &= ~(1ULL << bit_ind);

	return;
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
Fast_Tree_Leaf * create_fast_tree_leaf(Fast_Tree * root, uint8_t key, void * value, bool to_overwrite, void * prev_value, uint64_t base){

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

	// can create table in case other items have values
	int ret = init_fast_table(&(fast_tree_leaf -> values), root -> table_config_value);
	if (unlikely(ret != 0)){
		fprintf(stderr, "Error: failure to init value table in leaf\n");
		return NULL;
	}

	// deal with inserting value
	if (value){
		uint8_t value_key = key;			
		ret = insert_fast_table(&(fast_tree_leaf -> values), &value_key, &value);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert value into the table in leaf\n");
			return NULL;
		}
	}

	if (prev_value){
		// we just created this leaf so we know there was no previous value
		// can copy null pointer back
		memset(prev_value, 0, sizeof(void *));
	}


	return fast_tree_leaf;

}


void link_fast_tree_leaf(Fast_Tree * root, Fast_Tree_Leaf * fast_tree_leaf){

	int ret;

	uint64_t base = fast_tree_leaf -> base;


	Fast_Tree_Result leaf_search;

	if (base == 0){
		fast_tree_leaf -> prev = NULL;
		if (root -> min_leaf){
			fast_tree_leaf -> next = root -> min_leaf;
			fast_tree_leaf -> next -> prev = fast_tree_leaf;
		}
		else{
			fast_tree_leaf -> next = NULL;
		}
		root -> min_leaf = fast_tree_leaf;
		
	}
	else{
		ret = search_fast_tree(root, base - 1, FAST_TREE_EQUAL_OR_PREV, &leaf_search);
		if (ret){
			fast_tree_leaf -> prev = NULL;
			if (root -> min_leaf){
				fast_tree_leaf -> next = root -> min_leaf;
				fast_tree_leaf -> next -> prev = fast_tree_leaf;
			}
			else{
				fast_tree_leaf -> next = NULL;
			}
			root -> min_leaf = fast_tree_leaf;
		}
		else{
			fast_tree_leaf -> prev = leaf_search.fast_tree_leaf;
			fast_tree_leaf -> next = (leaf_search.fast_tree_leaf) -> next;
			fast_tree_leaf -> prev -> next = fast_tree_leaf;
			if (fast_tree_leaf -> next){
				fast_tree_leaf -> next -> prev = fast_tree_leaf;
			}
		}
	}

	if (!fast_tree_leaf -> next){
		root -> max_leaf = fast_tree_leaf;
	}

	return;

}




// Called by 32 inserting into an Fast_Tree_16 associated with main tree
int insert_fast_tree_16(Fast_Tree * root, Fast_Tree_16 * fast_tree, uint16_t key, void * value, bool to_overwrite, void * prev_value, uint64_t base, bool * element_inserted, Fast_Tree_Leaf ** new_main_leaf){

	int ret;
	
	if (fast_tree -> inward_leaves.items == NULL){
		int init_ret = init_fast_table(&(fast_tree -> inward_leaves), root -> table_config_main_leaf);
		if (unlikely(init_ret != 0)){
			fprintf(stderr, "Error: failure to init inward_leaves table from main_16\n");
			return -1;
		}
		fast_tree -> outward_leaf.min = 0xFF;
		fast_tree -> outward_leaf.max = 0;
		memset(fast_tree -> outward_leaf.bit_vector, 0, 4 * sizeof(uint64_t));
	}

	if (key < fast_tree -> min){
		fast_tree -> min = key;
	}
	if (key > fast_tree -> max){
		fast_tree -> max = key;
	}

	uint8_t ind_8 = (key & IND_8_MASK) >> 8;
	uint8_t off_8 = (key & OFF_8_MASK);

	// Notice the extra pointer indirection because the contents within the table are storing pointers
	Fast_Tree_Leaf ** inward_leaf_ref = NULL;

	Fast_Tree_Outward_Leaf * outward_leaf_ref = &(fast_tree -> outward_leaf);

	// if the table hasn't been created yet, this will immediately return not-found
	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &inward_leaf_ref);
	
	Fast_Tree_Leaf * inward_leaf;

	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (!inward_leaf_ref){


		*element_inserted = true;
		fast_tree -> cnt += 1;


		// SHOULD CREATE AND LINK LEAVES HERE!

		// responsible for allocating memory for leaf, initializing table for values, inserting value, 
		// creating deque item, linking deque item (and potentially linked to root deque -> head/tail)
		inward_leaf = create_fast_tree_leaf(root, off_8, value, to_overwrite, prev_value, base + (uint64_t) (ind_8 << 8));
		if (!inward_leaf){
			fprintf(stderr, "Error: failure to create and link fast tree leaf\n");
			return -1;
		}

		*new_main_leaf = inward_leaf;

		// insert the pointer to the newly created/value added/linked leaf into the table so we will find this leaf again
		ret = insert_fast_table(&(fast_tree -> inward_leaves), &ind_8, &inward_leaf);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table from 16\n");
			return -1;
		}

		(root -> tree_stats).num_leaves += 1;

		// need to add the index to the outward leaf
		set_bitvector(outward_leaf_ref -> bit_vector, ind_8);

		if (ind_8 < outward_leaf_ref -> min){
			outward_leaf_ref -> min = ind_8;
		}

		if (ind_8 > outward_leaf_ref -> max){
			outward_leaf_ref -> max = ind_8;
		}
	}
	else{

		*new_main_leaf = NULL;

		inward_leaf = *inward_leaf_ref;

		// This leaf was already in the table so we need to add the key to leaf and potentially get old value & insert/overwrite value

		// insert off_8 into the inward leaf with value and return prev value
		int in_bitvector = set_bitvector(inward_leaf -> bit_vector, off_8);

		// this Key was Already in the Tree
		if (in_bitvector != 0){

			*element_inserted = false;
				
			// have prev_value_table be a pointer within the table, so we can overwrite it if that
			// flag was set
			void * prev_value_table = NULL;

			ret = find_fast_table(&(inward_leaf -> values), &off_8, false, &prev_value_table);
			// if there was a value corresponding to this key before
			if (ret != inward_leaf -> values.config -> max_size){
				// if there was a previous value and we want to save the previous value
				// we should get the previous pointer (derefecing the table value) and copy it
				// to where we want to save. All values in the tree are void *, so this is OK
				if (prev_value && prev_value_table){
					memcpy(prev_value, *((void **) prev_value_table), sizeof(void *));
				}
				if (!to_overwrite){
					return -1;
				}      
				else{
					// overwriting the old pointer with new pointer
					if (prev_value_table && value){
						memcpy(prev_value_table, &value, sizeof(void *));
					}
					else {
						remove_fast_table(&(inward_leaf -> values), &off_8, NULL);
					}
				}
			}
		}
		else{

			*element_inserted = true;
			fast_tree -> cnt += 1;

			inward_leaf -> cnt += 1;
			if (off_8 < inward_leaf -> min){
				inward_leaf -> min = off_8;
			}
			if (off_8 > inward_leaf -> max){
				inward_leaf -> max = off_8;
			}

			


			if (value){
				// If we need to update the value table
				// if value table does not exist we need to intialize it
				if ((inward_leaf -> values).items == NULL){
					ret = init_fast_table(&(inward_leaf -> values), root -> table_config_value);
					if (unlikely(ret != 0)){
						fprintf(stderr, "Error: failure to init value table in leaf\n");
						return -1;
					}
				}

				ret = insert_fast_table(&(inward_leaf -> values), &off_8, &value);
				if (unlikely(ret != 0)){
					fprintf(stderr, "Error: failure to insert value in the leaf's value table from 16\n");
					return -1;
				}
			}

		}

		// if ind_8 leaf was alreay created we know that it was inserted into the outward_leaf already
	}
	return 0;
}

// Called by outward_32 inserting into a Fast_Tree_16
int insert_fast_tree_nonmain_16(Fast_Tree * root, Fast_Tree_16 * fast_tree, uint16_t key){
	
	int ret;

	if (fast_tree -> inward_leaves.items == NULL){
		int init_ret = init_fast_table(&(fast_tree -> inward_leaves), root -> table_config_outward_leaf);
		if (unlikely(init_ret != 0)){
			fprintf(stderr, "Error: failure to init inward_leaves table from nonmain_16\n");
			return -1;
		}
		fast_tree -> outward_leaf.min = 0xFF;
		fast_tree -> outward_leaf.max = 0;
		memset(fast_tree -> outward_leaf.bit_vector, 0, 4 * sizeof(uint64_t));
	}

	if (key < fast_tree -> min){
		fast_tree -> min = key;
	}
	if (key > fast_tree -> max){
		fast_tree -> max = key;
	}

	fast_tree -> cnt += 1;

	uint8_t ind_8 = (key & IND_8_MASK) >> 8;
	uint8_t off_8 = (key & OFF_8_MASK);

	// if the inward leaves on non-main trees are just outward leaves
	Fast_Tree_Outward_Leaf * inward_leaf_ref = NULL;

	Fast_Tree_Outward_Leaf * outward_leaf_ref = &(fast_tree -> outward_leaf);

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &inward_leaf_ref);
	if (!inward_leaf_ref){

		Fast_Tree_Outward_Leaf new_inward_leaf;

		memset(&new_inward_leaf, 0, sizeof(Fast_Tree_Outward_Leaf));

		set_bitvector(new_inward_leaf.bit_vector, off_8);

		new_inward_leaf.min = off_8;
		new_inward_leaf.max = off_8;

		ret = insert_fast_table(&(fast_tree -> inward_leaves), &ind_8, &new_inward_leaf);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table from nonmain_16\n");
			return -1;
		}

		(root -> tree_stats).num_outward_leaves += 1;
		
		// need to modify the outward_leaf to initialize the outward_leaf bitvector
		// and add ind_8
		set_bitvector(outward_leaf_ref -> bit_vector, ind_8);

		if (ind_8 < outward_leaf_ref -> min){
			outward_leaf_ref -> min = ind_8;
		}

		if (ind_8 > outward_leaf_ref -> max){
			outward_leaf_ref -> max = ind_8;
		}
	}
	else{
		set_bitvector(inward_leaf_ref -> bit_vector, off_8);
		// if ind_8 was in the table it was already added the outward_leaf bitvector

		if (off_8 < inward_leaf_ref -> min){
			inward_leaf_ref -> min = off_8;
		}

		if (off_8 > inward_leaf_ref -> max){
			inward_leaf_ref -> max = off_8;
		}

	}
	return 0;
}

// Called by 32 & outward_32 inserting into an Fast_Tree_Outward_Root_16
int insert_fast_tree_outward_16(Fast_Tree * root, Fast_Tree_Outward_Root_16 * fast_tree, uint16_t key) {

	int ret;

	if (fast_tree -> inward_leaves.items == NULL){
		int init_ret = init_fast_table(&(fast_tree -> inward_leaves), root -> table_config_outward_leaf);
		if (unlikely(init_ret != 0)){
			fprintf(stderr, "Error: failure to init inward_leaves table from outward_16\n");
			return -1;
		}
		fast_tree -> outward_leaf.min = 0xFF;
		fast_tree -> outward_leaf.max = 0;
		memset(fast_tree -> outward_leaf.bit_vector, 0, 4 * sizeof(uint64_t));
	}
   	
	uint8_t ind_8 = (key & IND_8_MASK) >> 8;
	uint8_t off_8 = (key & OFF_8_MASK);

	// The inward leaf of an outward root is still and outward_leaf type
	Fast_Tree_Outward_Leaf * inward_leaf_ref = NULL;

	Fast_Tree_Outward_Leaf * outward_leaf_ref = &(fast_tree -> outward_leaf);

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &inward_leaf_ref);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (!inward_leaf_ref){
		
		Fast_Tree_Outward_Leaf new_inward_leaf;

		memset(&new_inward_leaf, 0, sizeof(Fast_Tree_Outward_Leaf));

		new_inward_leaf.min = off_8;
		new_inward_leaf.max = off_8;
		
		set_bitvector(new_inward_leaf.bit_vector, off_8);
		
		ret = insert_fast_table(&(fast_tree -> inward_leaves), &ind_8, &new_inward_leaf);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table from outward_16\n");
			return -1;
		}

		(root -> tree_stats).num_outward_leaves += 1;

		set_bitvector(outward_leaf_ref -> bit_vector, ind_8);

		if (ind_8 < outward_leaf_ref -> min){
			outward_leaf_ref -> min = ind_8;
		}

		if (ind_8 > outward_leaf_ref -> max){
			outward_leaf_ref -> max = ind_8;
		}
	}
	else{
		set_bitvector(inward_leaf_ref -> bit_vector, off_8);

		if (off_8 < inward_leaf_ref -> min){
			inward_leaf_ref -> min = off_8;
		}

		if (off_8 > inward_leaf_ref -> max){
			inward_leaf_ref -> max = off_8;
		}
	}
	return 0;

}

int insert_fast_tree_32(Fast_Tree * root, Fast_Tree_32 * fast_tree, uint32_t key, void * value, bool to_overwrite, void * prev_value, uint64_t base, bool * element_inserted, Fast_Tree_Leaf ** new_main_leaf){

	int ret;

	if (fast_tree -> inward.items == NULL){
		// the cnt will be incremented within the next function call (insert_fast_tree_nonmain_16)
		int init_ret = init_fast_table(&(fast_tree -> inward), root -> table_config_16);
		if (unlikely(init_ret != 0)){
			fprintf(stderr, "Error: failure to init inward_tree_16 table from main_32\n");
			return -1;
		}
	}


	if (key < fast_tree -> min){
		fast_tree -> min = key;
	}
	if (key > fast_tree -> max){
		fast_tree -> max = key;
	}

	uint16_t ind_16 = (key & IND_16_MASK) >> 16;
	uint16_t off_16 = (key & OFF_16_MASK);

	Fast_Tree_16 * inward_tree_16_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (!inward_tree_16_ref){
		
		Fast_Tree_16 new_inward_tree_16;

		// the cnt will be incremented within the next function call (insert_fast_tree_16)
		memset(&new_inward_tree_16, 0, sizeof(Fast_Tree_16));

		new_inward_tree_16.min = off_16;
		new_inward_tree_16.max = off_16;

		(root -> tree_stats).num_trees_16 += 1;

		ret = insert_fast_table(&(fast_tree -> inward), &ind_16, &new_inward_tree_16);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table from 32\n");
			return -1;
		}

		ret = insert_fast_tree_outward_16(root, &(fast_tree -> outward_root), ind_16);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert into outward tree from 32\n");
			return -1;
		}

		// Getting the pointer within the table
		find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);

		// should never happen because we just inserted
		if (unlikely(!inward_tree_16_ref)){
			fprintf(stderr, "Error: cannot find inward_tree_16 that was just inserted\n");
			return -1;
		}
	}
	

	ret = insert_fast_tree_16(root, inward_tree_16_ref, off_16, value, to_overwrite, prev_value, base + (uint64_t) ((ind_16) << 16), element_inserted, new_main_leaf);

	if (*element_inserted){
		fast_tree -> cnt += 1;
	}

	return ret;

}

int insert_fast_tree_outward_32(Fast_Tree * root, Fast_Tree_Outward_Root_32 * fast_tree, uint32_t key){

	int ret;

	// OUTWARD ROOT 32 HAS ALREADY INITIALIZED TABLE FROM INIT_FAST_TREE()

	uint16_t ind_16 = (key & IND_16_MASK) >> 16;
	uint16_t off_16 = (key & OFF_16_MASK);

	Fast_Tree_16 * inward_tree_16_ref = NULL;

	// NOT COPYING THE VALUE, JUST SETTING REF
	find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (!inward_tree_16_ref){
		
		Fast_Tree_16 new_inward_tree_16;

		memset(&new_inward_tree_16, 0, sizeof(Fast_Tree_16));

		new_inward_tree_16.min = off_16;
		new_inward_tree_16.max = off_16;

		(root -> tree_stats).num_nonmain_trees_16 += 1;

		ret = insert_fast_table(&(fast_tree -> inward), &ind_16, &new_inward_tree_16);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table from outward_32\n");
			return -1;
		}

		ret = insert_fast_tree_outward_16(root, &(fast_tree -> outward_root), ind_16);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert into outward tree from outward_32\n");
			return -1;
		}

		// Getting the pointer within the table
		find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);

		// should never happen because we just inserted
		if (unlikely(!inward_tree_16_ref)){
			fprintf(stderr, "Error: cannot find inward_tree_16 that was just inserted\n");
			return -1;
		}
	}
	
	ret = insert_fast_tree_nonmain_16(root, inward_tree_16_ref, off_16);
	return ret;


}

// returns 0 on success -1 on error
// fails is key is already in the tree and overwrite set to false
// if key was already in the tree and had a non-null value, then copies the previous value into prev_value
int insert_fast_tree(Fast_Tree * fast_tree, uint64_t key, void * value, bool to_overwrite, void * prev_value) {

	int ret;

	uint32_t ind_32 = (key & IND_32_MASK) >> 32;
	uint32_t off_32 = (key & OFF_32_MASK);

	Fast_Tree_32 * inward_tree_32_ref = NULL;
	
	// NOT COPYING THE VALUE, JUST SETTING REF
	find_fast_table(&(fast_tree -> inward), &ind_32, false, (void **) &inward_tree_32_ref);

	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (!inward_tree_32_ref){

		Fast_Tree_32 new_inward_tree_32;

		memset(&new_inward_tree_32, 0, sizeof(Fast_Tree_32));

		new_inward_tree_32.min = off_32;
		new_inward_tree_32.max = off_32;

		ret = insert_fast_table(&(fast_tree -> inward), &ind_32, &new_inward_tree_32);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert tree into table\n");
			return -1;
		}

		(fast_tree -> tree_stats).num_trees_32 += 1;

		ret = insert_fast_tree_outward_32(fast_tree, &(fast_tree -> outward_root), ind_32);
		if (unlikely(ret != 0)){
			fprintf(stderr, "Error: failure to insert into outward tree\n");
			return -1;
		}

		// Getting the pointer within the table
		find_fast_table(&(fast_tree -> inward), &ind_32, false, (void **) &inward_tree_32_ref);

		// should never happen because we just inserted
		if (unlikely(!inward_tree_32_ref)){
			fprintf(stderr, "Error: cannot find inward_tree_32 that was just inserted\n");
			return -1;
		}


	}
	

	bool element_inserted = false;

	Fast_Tree_Leaf * new_main_leaf = NULL;
	ret = insert_fast_tree_32(fast_tree, inward_tree_32_ref, off_32, value, to_overwrite, prev_value, (uint64_t) ind_32 << 32, &element_inserted, &new_main_leaf);

	if (new_main_leaf){
		link_fast_tree_leaf(fast_tree, new_main_leaf);
	}

	if (element_inserted){
		fast_tree -> cnt += 1;
	}

	if (key < fast_tree -> min){
		fast_tree -> min = key;
	}
	if (key > fast_tree -> max){
		fast_tree -> max = key;
	}

	return ret;



}




Fast_Tree_Leaf * get_leaf(Fast_Tree * fast_tree, uint64_t key){

	uint32_t ind_32 = (key & IND_32_MASK) >> 32;
	uint32_t off_32 = (key & OFF_32_MASK);

	Fast_Tree_32 * inward_tree_32_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_32, false, (void **) &inward_tree_32_ref);

	if (!inward_tree_32_ref){
		return NULL;
	}

	uint16_t ind_16 = (off_32 & IND_16_MASK) >> 16;
	uint16_t off_16 = (off_32 & OFF_16_MASK);

	Fast_Tree_16 * inward_tree_16_ref = NULL;

	find_fast_table(&(inward_tree_32_ref -> inward), &ind_16, false, (void **) &inward_tree_16_ref);

	if (!inward_tree_16_ref){
		return NULL;
	}

	uint8_t ind_8 = (off_16 & IND_8_MASK) >> 8;

	Fast_Tree_Leaf ** fast_tree_leaf_ref = NULL;

	find_fast_table(&(inward_tree_16_ref -> inward_leaves), &ind_8, false, (void **) &fast_tree_leaf_ref);

	if (!fast_tree_leaf_ref){
		return NULL;
	}

	// we already checked that the reference was non-null, so we know leaf exists
	Fast_Tree_Leaf * fast_tree_leaf = *fast_tree_leaf_ref;

	return fast_tree_leaf;
}


// SEARCH PREV and SEARCH NEXT RETURN <= and >= key's relative to search key respectively!
// Rely on passing -1/+1 to achieve strict predecessor/successor

uint8_t lookup_bitvector_prev(uint64_t * bit_vector, uint8_t key){


	// upper 2 bits
	uint8_t vec_ind = (key & LEAF_VEC_IND_MASK) >> 6;
	// lower 6 bits
	uint8_t bit_ind = (key & LEAF_BIT_POS_MASK);

	

	uint64_t orig_vec = bit_vector[vec_ind];
	// need to clear the upper bits (all bits at positions > bit_ind) before looing for
	// highest set bit

	// set 1's for all positions between [0, bit_ind]
	uint64_t lower_mask = (((1ULL << bit_ind) - 1) | (1ULL << bit_ind));
	uint64_t cur_search_vec = orig_vec & lower_mask;

	uint8_t cur_val = 64 * vec_ind;
	uint8_t cur_vec_ind = vec_ind;
	
	int found_element_pos;
	while (cur_vec_ind >= 0){

		if (cur_search_vec == 0){
			cur_val -= 64;
			cur_vec_ind -= 1;
			cur_search_vec = bit_vector[cur_vec_ind];
			continue;
		}

		// get highest order set bit position
		found_element_pos = 63 - __builtin_clzll(cur_search_vec);

		return cur_val + found_element_pos;
	}

	// should never get here
	fprintf(stderr, "Error: no position found in lookup_bitvector_prev\n");
	return 0xFF;
}


uint8_t lookup_bitvector_next(uint64_t * bit_vector, uint8_t key){


	// upper 2 bits
	uint8_t vec_ind = (key & LEAF_VEC_IND_MASK) >> 6;
	// lower 6 bits
	uint8_t bit_ind = (key & LEAF_BIT_POS_MASK);

	uint64_t orig_vec = bit_vector[vec_ind];

	// need to clear the lower bits before looing for
	// highest set bit
	uint64_t cur_search_vec = orig_vec & (ALL_ONES_64 << bit_ind);

	uint8_t cur_val = 64 * vec_ind;
	uint8_t cur_vec_ind = vec_ind;
	int found_element_pos;
	while (cur_vec_ind >= 0){

		if (cur_search_vec == 0){
			cur_val += 64;
			cur_vec_ind += 1;
			cur_search_vec = bit_vector[cur_vec_ind];
			continue;
		}

		// get lowest order set bit position
		found_element_pos = __builtin_ctzll(cur_search_vec);

		return cur_val + found_element_pos;
	}

	// should never get here
	fprintf(stderr, "Error: no position found in lookup_bitvector_next\n");
	return 0;


}


// Because all values in the table are pointers, we can 
// get a reference within the table and deference it
// to get a staic pointer value that is returned

// The value table might get resized so we don't want to to return a pointer
// to something within the table!
void * get_value_from_leaf(Fast_Tree_Leaf * fast_tree_leaf, uint8_t key){
	// if there isn't a table for this leaf
	if (fast_tree_leaf -> values.items == NULL){
		return NULL;
	}
	// otherwise lookup key within table
	uint8_t value_key = key;
	void * table_value_ptr = NULL;

	
	find_fast_table(&(fast_tree_leaf -> values), &value_key, false, (void **) &table_value_ptr);
	if ((!table_value_ptr) || !((void **) table_value_ptr)){
		return NULL;
	}

	void * value = *((void **) table_value_ptr);
	return value;
}







// SEARCH PREV and SEARCH NEXT RETURN <= and >= key's relative to search key respectively!
// Rely on passing -1/+1 to achieve strict predecessor/successor

int search_prev_fast_tree_16(Fast_Tree_16 * fast_tree, uint16_t search_key, Fast_Tree_Result * ret_search_result){

	uint8_t ind_8 = (search_key & IND_8_MASK) >> 8;
	uint8_t off_8 = (search_key & OFF_8_MASK);


	Fast_Tree_Leaf ** leaf_ref = NULL;

	Fast_Tree_Leaf * main_leaf = NULL;

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &leaf_ref);

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!leaf_ref) || (off_8 < (*leaf_ref) -> min)){

		uint8_t prev_leaf_ind = lookup_bitvector_prev(fast_tree -> outward_leaf.bit_vector, ind_8 - 1);

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward_leaves), &prev_leaf_ind, false, (void **) &leaf_ref);

		// this should never happen
		if (unlikely(!leaf_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		main_leaf = *leaf_ref;

		// set the bottom result
		ret_search_result -> key = ((uint16_t) prev_leaf_ind << 8) + (uint16_t) main_leaf -> max;

		// we are in the leaf of main tree, so can accelerate by populing the values here
		ret_search_result -> fast_tree_leaf = main_leaf;
		ret_search_result -> value = get_value_from_leaf(main_leaf, main_leaf -> max);
	}
	else{

		main_leaf = *leaf_ref;

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		uint8_t prev_leaf_off = lookup_bitvector_prev(main_leaf -> bit_vector, off_8);

		// set the bottom result
		ret_search_result -> key = ((uint16_t) ind_8 << 8) + (uint16_t) prev_leaf_off;

		ret_search_result -> fast_tree_leaf = main_leaf;
		ret_search_result -> value = get_value_from_leaf(main_leaf, prev_leaf_off);
	}

	return 0;

}

int search_prev_fast_tree_nonmain_16(Fast_Tree_16 * fast_tree, uint16_t search_key, Fast_Tree_Result * ret_search_result){

	uint8_t ind_8 = (search_key & IND_8_MASK) >> 8;
	uint8_t off_8 = (search_key & OFF_8_MASK);


	Fast_Tree_Outward_Leaf * leaf_ref = NULL;

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &leaf_ref);

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!leaf_ref) || (off_8 < leaf_ref -> min)){

		uint8_t prev_leaf_ind = lookup_bitvector_prev(fast_tree -> outward_leaf.bit_vector, ind_8 - 1);

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward_leaves), &prev_leaf_ind, false, (void **) &leaf_ref);

		// this should never happen
		if (unlikely(!leaf_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// set the bottom result
		ret_search_result -> key = ((uint16_t) prev_leaf_ind << 8) + (uint16_t) leaf_ref -> max;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		uint8_t prev_leaf_off = lookup_bitvector_prev(leaf_ref -> bit_vector, off_8);

		// set the bottom result
		ret_search_result -> key = ((uint16_t) ind_8 << 8) + (uint16_t) prev_leaf_off;
	}

	return 0;
}

int search_prev_fast_tree_outward_16(Fast_Tree_Outward_Root_16 * fast_tree, uint16_t search_key, Fast_Tree_Result * ret_search_result){

	uint8_t ind_8 = (search_key & IND_8_MASK) >> 8;
	uint8_t off_8 = (search_key & OFF_8_MASK);


	Fast_Tree_Outward_Leaf * leaf_ref = NULL;

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &leaf_ref);

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!leaf_ref) || (off_8 < leaf_ref -> min)){

		uint8_t prev_leaf_ind = lookup_bitvector_prev(fast_tree -> outward_leaf.bit_vector, ind_8 - 1);

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward_leaves), &prev_leaf_ind, false, (void **) &leaf_ref);

		// this should never happen
		if (unlikely(!leaf_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// set the bottom result
		ret_search_result -> key = ((uint16_t) prev_leaf_ind << 8) + (uint16_t) leaf_ref -> max;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		uint8_t prev_leaf_off = lookup_bitvector_prev(leaf_ref -> bit_vector, off_8);

		// set the bottom result
		ret_search_result -> key = ((uint16_t) ind_8 << 8) + (uint16_t) prev_leaf_off;
	}

	return 0;
}


int search_prev_fast_tree_32(Fast_Tree_32 * fast_tree, uint32_t search_key, Fast_Tree_Result * ret_search_result){

	uint16_t ind_16 = (search_key & IND_16_MASK) >> 16;
	uint16_t off_16 = (search_key & OFF_16_MASK);

	Fast_Tree_16 * inward_tree_16_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);

	int ret;

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!inward_tree_16_ref) || (off_16 < inward_tree_16_ref -> min)){

		// this will temporarily populate the ret_search_result with 32-bit value represent next ind_32
		ret = search_prev_fast_tree_outward_16(&(fast_tree -> outward_root), ind_16 - 1, ret_search_result);
		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_32 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint32_t prev_ind_16 = ret_search_result -> key;

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward), &prev_ind_16, false, (void **) &inward_tree_16_ref);

		// this should never happen
		if (unlikely(!inward_tree_16_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// now reset the result to be correct
		ret_search_result -> key = ((uint32_t) prev_ind_16 << 16) + (uint32_t) inward_tree_16_ref -> max;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		ret = search_prev_fast_tree_16(inward_tree_16_ref, off_16, ret_search_result);

		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_16 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint16_t prev_off_16 = ret_search_result -> key;

		// now reset the result to be correct
		ret_search_result -> key = ((uint32_t) ind_16 << 16) + (uint32_t) prev_off_16;
	}

	return 0;
	
}

int search_prev_fast_tree_outward_32(Fast_Tree_Outward_Root_32 * fast_tree, uint32_t search_key, Fast_Tree_Result * ret_search_result){

	uint16_t ind_16 = (search_key & IND_16_MASK) >> 16;
	uint16_t off_16 = (search_key & OFF_16_MASK);


	Fast_Tree_16 * inward_tree_16_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);

	int ret;

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!inward_tree_16_ref) || (off_16 < inward_tree_16_ref -> min)){

		// this will temporarily populate the ret_search_result with 32-bit value represent next ind_32
		ret = search_prev_fast_tree_outward_16(&(fast_tree -> outward_root), ind_16 - 1, ret_search_result);
		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_32 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint32_t prev_ind_16 = ret_search_result -> key;

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward), &prev_ind_16, false, (void **) &inward_tree_16_ref);

		// this should never happen
		if (unlikely(!inward_tree_16_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// now reset the result to be correct
		ret_search_result -> key = ((uint32_t) prev_ind_16 << 16) + (uint32_t) inward_tree_16_ref -> max;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		ret = search_prev_fast_tree_nonmain_16(inward_tree_16_ref, off_16, ret_search_result);

		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_16 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint16_t prev_off_16 = ret_search_result -> key;

		// now reset the result to be correct
		ret_search_result -> key = ((uint32_t) ind_16 << 16) + (uint32_t) prev_off_16;
	}

	return 0;

}

int search_prev_fast_tree(Fast_Tree * fast_tree, uint64_t search_key, Fast_Tree_Result * ret_search_result){

	// initally set search result
	ret_search_result -> fast_tree_leaf = NULL;
	ret_search_result -> key = search_key;
	ret_search_result -> value = NULL;

	// Can immediately return because we know there will not be a next within the tree
	if (search_key < fast_tree -> min){
		return -1; 
	}


	uint32_t ind_32 = (search_key & IND_32_MASK) >> 32;
	uint32_t off_32 = (search_key & OFF_32_MASK);


	Fast_Tree_32 * inward_tree_32_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_32, false, (void **) &inward_tree_32_ref);

	int ret;

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!inward_tree_32_ref) || (off_32 < inward_tree_32_ref -> min)){

		// this will temporarily populate the ret_search_result with 32-bit value represent next ind_32
		ret = search_prev_fast_tree_outward_32(&(fast_tree -> outward_root), ind_32 - 1, ret_search_result);
		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_32 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint32_t prev_ind_32 = ret_search_result -> key;

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward), &prev_ind_32, false, (void **) &inward_tree_32_ref);

		// this should never happen
		if (unlikely(!inward_tree_32_ref)){
			fprintf(stderr, "Error: expected to find ind_32 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// now reset the result to be correct
		ret_search_result -> key = ((uint64_t) prev_ind_32 << 32) + (uint64_t) inward_tree_32_ref -> max;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		ret = search_prev_fast_tree_32(inward_tree_32_ref, off_32, ret_search_result);

		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_32 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint32_t prev_off_32 = ret_search_result -> key;

		// now reset the result to be correct
		ret_search_result -> key = ((uint64_t) ind_32 << 32) + (uint64_t) prev_off_32;
	}

	return 0;
}


int search_next_fast_tree_16(Fast_Tree_16 * fast_tree, uint16_t search_key, Fast_Tree_Result * ret_search_result){

	uint8_t ind_8 = (search_key & IND_8_MASK) >> 8;
	uint8_t off_8 = (search_key & OFF_8_MASK);


	Fast_Tree_Leaf ** leaf_ref = NULL;

	Fast_Tree_Leaf * main_leaf = NULL;

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &leaf_ref);

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!leaf_ref) || (off_8 > (*leaf_ref) -> max)){

		uint8_t next_leaf_ind = lookup_bitvector_next(fast_tree -> outward_leaf.bit_vector, ind_8 + 1);

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward_leaves), &next_leaf_ind, false, (void **) &leaf_ref);

		// this should never happen
		if (unlikely(!leaf_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		main_leaf = *leaf_ref;

		// set the bottom result
		ret_search_result -> key = ((uint16_t) next_leaf_ind << 8) + (uint16_t) main_leaf -> min;

		// we are in the leaf of main tree, so can accelerate by populing the values here
		ret_search_result -> fast_tree_leaf = main_leaf;
		ret_search_result -> value = get_value_from_leaf(main_leaf, main_leaf -> min);
	}
	else{

		main_leaf = *leaf_ref;

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		uint8_t next_leaf_off = lookup_bitvector_next(main_leaf -> bit_vector, off_8);

		// set the bottom result
		ret_search_result -> key = ((uint16_t) ind_8 << 8) + (uint16_t) next_leaf_off;

		ret_search_result -> fast_tree_leaf = main_leaf;
		ret_search_result -> value = get_value_from_leaf(main_leaf, next_leaf_off);
	}

	return 0;

}

int search_next_fast_tree_nonmain_16(Fast_Tree_16 * fast_tree, uint16_t search_key, Fast_Tree_Result * ret_search_result){

	uint8_t ind_8 = (search_key & IND_8_MASK) >> 8;
	uint8_t off_8 = (search_key & OFF_8_MASK);


	Fast_Tree_Outward_Leaf * leaf_ref = NULL;

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &leaf_ref);

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!leaf_ref) || (off_8 > leaf_ref -> max)){

		uint8_t next_leaf_ind = lookup_bitvector_next(fast_tree -> outward_leaf.bit_vector, ind_8 + 1);

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward_leaves), &next_leaf_ind, false, (void **) &leaf_ref);

		// this should never happen
		if (unlikely(!leaf_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// set the bottom result
		ret_search_result -> key = ((uint16_t) next_leaf_ind << 8) + (uint16_t) leaf_ref -> min;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		uint8_t next_leaf_off = lookup_bitvector_next(leaf_ref -> bit_vector, off_8);

		// set the bottom result
		ret_search_result -> key = ((uint16_t) ind_8 << 8) + (uint16_t) next_leaf_off;
	}

	return 0;
}

int search_next_fast_tree_outward_16(Fast_Tree_Outward_Root_16 * fast_tree, uint16_t search_key, Fast_Tree_Result * ret_search_result){

	uint8_t ind_8 = (search_key & IND_8_MASK) >> 8;
	uint8_t off_8 = (search_key & OFF_8_MASK);


	Fast_Tree_Outward_Leaf * leaf_ref = NULL;

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &leaf_ref);

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!leaf_ref) || (off_8 > leaf_ref -> max)){

		uint8_t next_leaf_ind = lookup_bitvector_next(fast_tree -> outward_leaf.bit_vector, ind_8 + 1);

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward_leaves), &next_leaf_ind, false, (void **) &leaf_ref);

		// this should never happen
		if (unlikely(!leaf_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// set the bottom result
		ret_search_result -> key = ((uint16_t) next_leaf_ind << 8) + (uint16_t) leaf_ref -> min;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		uint8_t next_leaf_off = lookup_bitvector_next(leaf_ref -> bit_vector, off_8);

		// set the bottom result
		ret_search_result -> key = ((uint16_t) ind_8 << 8) + (uint16_t) next_leaf_off;
	}

	return 0;
}


int search_next_fast_tree_32(Fast_Tree_32 * fast_tree, uint32_t search_key, Fast_Tree_Result * ret_search_result){

	uint16_t ind_16 = (search_key & IND_16_MASK) >> 16;
	uint16_t off_16 = (search_key & OFF_16_MASK);

	Fast_Tree_16 * inward_tree_16_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);

	int ret;

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!inward_tree_16_ref) || (off_16 > inward_tree_16_ref -> max)){

		// this will temporarily populate the ret_search_result with 32-bit value represent next ind_32
		ret = search_next_fast_tree_outward_16(&(fast_tree -> outward_root), ind_16 + 1, ret_search_result);
		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_32 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint32_t next_ind_16 = ret_search_result -> key;

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward), &next_ind_16, false, (void **) &inward_tree_16_ref);

		// this should never happen
		if (unlikely(!inward_tree_16_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// now reset the result to be correct
		ret_search_result -> key = ((uint32_t) next_ind_16 << 16) + (uint32_t) inward_tree_16_ref -> min;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		ret = search_next_fast_tree_16(inward_tree_16_ref, off_16, ret_search_result);

		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_16 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint16_t next_off_16 = ret_search_result -> key;

		// now reset the result to be correct
		ret_search_result -> key = ((uint32_t) ind_16 << 16) + (uint32_t) next_off_16;
	}

	return 0;
	
}

int search_next_fast_tree_outward_32(Fast_Tree_Outward_Root_32 * fast_tree, uint32_t search_key, Fast_Tree_Result * ret_search_result){

	uint16_t ind_16 = (search_key & IND_16_MASK) >> 16;
	uint16_t off_16 = (search_key & OFF_16_MASK);


	Fast_Tree_16 * inward_tree_16_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);

	int ret;

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!inward_tree_16_ref) || (off_16 > inward_tree_16_ref -> max)){

		// this will temporarily populate the ret_search_result with 32-bit value represent next ind_32
		ret = search_next_fast_tree_outward_16(&(fast_tree -> outward_root), ind_16 + 1, ret_search_result);
		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_32 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint32_t next_ind_16 = ret_search_result -> key;

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward), &next_ind_16, false, (void **) &inward_tree_16_ref);

		// this should never happen
		if (unlikely(!inward_tree_16_ref)){
			fprintf(stderr, "Error: expected to find ind_16 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// now reset the result to be correct
		ret_search_result -> key = ((uint32_t) next_ind_16 << 16) + (uint32_t) inward_tree_16_ref -> min;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		ret = search_next_fast_tree_nonmain_16(inward_tree_16_ref, off_16, ret_search_result);

		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_16 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint16_t next_off_16 = ret_search_result -> key;

		// now reset the result to be correct
		ret_search_result -> key = ((uint32_t) ind_16 << 16) + (uint32_t) next_off_16;
	}

	return 0;

}

int search_next_fast_tree(Fast_Tree * fast_tree, uint64_t search_key, Fast_Tree_Result * ret_search_result){

	// initally set search result
	ret_search_result -> fast_tree_leaf = NULL;
	ret_search_result -> key = search_key;
	ret_search_result -> value = NULL;


	// Can immediately return because we know there will not be a next within the tree
	if (search_key > fast_tree -> max){
		return -1; 
	}


	uint32_t ind_32 = (search_key & IND_32_MASK) >> 32;
	uint32_t off_32 = (search_key & OFF_32_MASK);


	Fast_Tree_32 * inward_tree_32_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_32, false, (void **) &inward_tree_32_ref);

	int ret;

	// this index did not exist so now our search will be looking for the
	// the successor of index and returing the minimum value from this 32_tree
	if ((!inward_tree_32_ref) || (off_32 > inward_tree_32_ref -> max)){

		// this will temporarily populate the ret_search_result with 32-bit value represent next ind_32
		ret = search_next_fast_tree_outward_32(&(fast_tree -> outward_root), ind_32 + 1, ret_search_result);
		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_32 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint32_t next_ind_32 = ret_search_result -> key;

		// now we should be guarenteed that there will be a tree with the next index
		find_fast_table(&(fast_tree -> inward), &next_ind_32, false, (void **) &inward_tree_32_ref);

		// this should never happen
		if (unlikely(!inward_tree_32_ref)){
			fprintf(stderr, "Error: expected to find ind_32 tree after outward search, but not found\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		// now reset the result to be correct
		ret_search_result -> key = ((uint64_t) next_ind_32 << 32) + (uint64_t) inward_tree_32_ref -> min;
	}
	else{

		// this will temporarily populate the ret_search_result with 32-bit value represent next off_32
		ret = search_next_fast_tree_32(inward_tree_32_ref, off_32, ret_search_result);

		// this should never happen
		if (unlikely(ret)){
			fprintf(stderr, "Error: search_next_fast_tree_outward_32 returned error\n");
			ret_search_result -> key = search_key;
			return -1;
		}

		uint32_t next_off_32 = ret_search_result -> key;

		// now reset the result to be correct
		ret_search_result -> key = ((uint64_t) ind_32 << 32) + (uint64_t) next_off_32;
	}

	return 0;
}





// reutnrs 0 on success -1 if no satisfying search result
// sets the search result
int search_fast_tree(Fast_Tree * fast_tree, uint64_t search_key, FastTreeSearchModifier search_type, Fast_Tree_Result * ret_search_result) {
	
	// initally set search result
	ret_search_result -> fast_tree_leaf = NULL;
	ret_search_result -> key = search_key;
	ret_search_result -> value = NULL;

	if (fast_tree -> cnt == 0){
		return -1;
	}

	int ret;

	uint64_t found_key;
	void * value;
	Fast_Tree_Leaf * fast_tree_leaf;

	switch(search_type){
		case FAST_TREE_MIN:
			ret_search_result -> fast_tree_leaf = fast_tree -> min_leaf;
			ret_search_result -> key = fast_tree -> min;			
			ret_search_result -> value = get_value_from_leaf(fast_tree -> min_leaf, fast_tree -> min & LEAF_KEY_MASK);
			ret = 0;
			break;
		case FAST_TREE_MAX:
			ret_search_result -> fast_tree_leaf = fast_tree -> max_leaf;
			ret_search_result -> key = fast_tree -> max;			
			ret_search_result -> value = get_value_from_leaf(fast_tree -> max_leaf, fast_tree -> max & LEAF_KEY_MASK);
			ret = 0;
			break;
		case FAST_TREE_PREV:
			// trying to search prev on 0 will underflow so immediately return none
			if (search_key == 0){
				return -1;
			}
			ret = search_prev_fast_tree(fast_tree, search_key - 1, ret_search_result);
			break;
		case FAST_TREE_NEXT:
			// trying to search next on the maximum value will overflow so immediately return none
			if (search_key == TREE_MAX){
				return -1;
			}
			// not equal so just call next on search key + 1
			ret = search_next_fast_tree(fast_tree, search_key + 1, ret_search_result);
			break;
		// For the just equal case we can directly walk down the main tree
		case FAST_TREE_EQUAL:
			fast_tree_leaf = get_leaf(fast_tree, search_key);
			if (!fast_tree_leaf){
				return -1;
			}
			// we know the leaf exists
			// now we lookup the value based on lower 8 bits
			value = get_value_from_leaf(fast_tree_leaf, search_key & LEAF_KEY_MASK);
			ret_search_result -> fast_tree_leaf = fast_tree_leaf;
			ret_search_result -> key = search_key;			
			ret_search_result -> value = value;
			ret = 0;
			break;
		
		case FAST_TREE_EQUAL_OR_NEXT:
			ret = search_next_fast_tree(fast_tree, search_key, ret_search_result);
			break;
		
		case FAST_TREE_EQUAL_OR_PREV:
			ret = search_prev_fast_tree(fast_tree, search_key, ret_search_result);
			break;
		default:
			fprintf(stderr, "Error: unknown search type\n");
			return -1;
	}

	// THIS MEANS THAT ret_search_result -> key has been properly set
	
	if (ret == 0){

		found_key = ret_search_result -> key;
		// in the case of finding the appropriate key through
		// an outward root
		if (!(ret_search_result -> fast_tree_leaf)){
			fast_tree_leaf = get_leaf(fast_tree, found_key);
			// this should never happen
			if (unlikely(!fast_tree_leaf)){
				fprintf(stderr, "Error: search was supposed to find key, but no leaf found\n");
				ret_search_result -> fast_tree_leaf = NULL;
				ret_search_result -> value = NULL;
				return -1;
			}
			ret_search_result -> fast_tree_leaf = fast_tree_leaf;
			ret_search_result -> value = get_value_from_leaf(fast_tree_leaf, found_key & LEAF_KEY_MASK);
		}
		// if we got the correct leaf, but wrong key then value would be empty
		else if (search_key != found_key){
			ret_search_result -> value = get_value_from_leaf(ret_search_result -> fast_tree_leaf, found_key & LEAF_KEY_MASK);
		}
		// otherwise we found the key through main tree and then fast_tree_leaf and value
		// have already been set

		return 0;
	}

	return ret;

}


void destroy_and_unlink_fast_tree_leaf(Fast_Tree * root, Fast_Tree_Leaf * fast_tree_leaf, uint64_t * triggered_new_min_key, uint64_t * triggered_new_max_key){

	if (fast_tree_leaf -> values.items != NULL){
		destroy_fast_table(&(fast_tree_leaf -> values));
	}

	Fast_Tree_Leaf * prev_leaf = fast_tree_leaf -> prev;
	Fast_Tree_Leaf * next_leaf = fast_tree_leaf -> next;

	if (prev_leaf){
		*triggered_new_max_key = prev_leaf -> base + prev_leaf -> max;
		prev_leaf -> next = next_leaf;
	}
	// if there was no previous we know this was at the head of ordered_leaves
	else{
		root -> min_leaf = next_leaf;
	}

	if (next_leaf){
		*triggered_new_min_key = next_leaf -> base + next_leaf -> min;
		next_leaf -> prev = prev_leaf;
	}
	else{
		root -> max_leaf = prev_leaf;
	}

	free(fast_tree_leaf);

	return;
}

void remove_fast_tree_16(Fast_Tree * root, Fast_Tree_16 * fast_tree, uint16_t key, uint64_t * triggered_new_min_key, uint64_t * triggered_new_max_key, bool * element_removed, bool * tree_removed, void * prev_value){

	// OUTWARD ROOT 32 HAS ALREADY INITIALIZED TABLE FROM INIT_FAST_TREE()

	// Assuming we won't find element, then setting to true if we do...
	*tree_removed = false;	
	*element_removed = false;

	uint8_t ind_8 = (key & IND_8_MASK) >> 8;
	uint8_t off_8 = (key & OFF_8_MASK);

	Fast_Tree_Leaf ** inward_leaf_ref = NULL;
	Fast_Tree_Outward_Leaf * outward_leaf_ref = &(fast_tree -> outward_leaf);

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &inward_leaf_ref);

	if (!inward_leaf_ref){
		return;
	}

	Fast_Tree_Leaf * main_leaf = *inward_leaf_ref;

	bool in_leaf = check_bitvector(main_leaf -> bit_vector, off_8);
	if (!in_leaf){
		return;
	}

	clear_bitvector(main_leaf -> bit_vector, off_8);

	// Setting element removed here which will get propogated back up!
	*element_removed = true;

	fast_tree -> cnt -= 1;

	// if we want to save the previous value (i.e. the caller passed in non-null value)
	// then we should copy the results to the derefrenced prev_value
	// this function will handle if the table does not exist by default
	remove_fast_table(&(main_leaf -> values), &off_8, prev_value);
	main_leaf -> cnt -= 1;



	if ((main_leaf -> min == off_8) && (main_leaf -> max == off_8)){
		destroy_and_unlink_fast_tree_leaf(root, main_leaf, triggered_new_min_key, triggered_new_max_key);

		remove_fast_table(&(fast_tree -> inward_leaves), &ind_8, NULL);

		(root -> tree_stats).num_leaves -= 1;

		clear_bitvector(outward_leaf_ref -> bit_vector, ind_8);

		if ((outward_leaf_ref -> min == ind_8) && (outward_leaf_ref -> max == ind_8)){
			destroy_fast_table(&(fast_tree -> inward_leaves));
			*tree_removed = true;
			return;
		}
		else if (outward_leaf_ref -> min == ind_8){
			outward_leaf_ref -> min = lookup_bitvector_next(outward_leaf_ref -> bit_vector, ind_8);
		}
		else if (outward_leaf_ref -> max == ind_8){
			outward_leaf_ref -> max = lookup_bitvector_prev(outward_leaf_ref -> bit_vector, ind_8);
		}

	}
	else if (main_leaf -> min == off_8){
		main_leaf -> min = lookup_bitvector_next(main_leaf -> bit_vector, off_8);
		*triggered_new_min_key = main_leaf -> base + main_leaf -> min;
	}
	else if (main_leaf -> max == off_8){
		main_leaf -> max = lookup_bitvector_prev(main_leaf -> bit_vector, off_8);
		*triggered_new_max_key = main_leaf -> base + main_leaf -> max;
	}


	if (fast_tree -> min == key){
		
		if (fast_tree -> cnt == 1){
			fast_tree -> min = fast_tree -> max;
		}
		else{
			fast_tree -> min = ((*triggered_new_min_key) >> 32) & OFF_16_MASK;
		}
	}
	else if (fast_tree -> max == key){
		if (fast_tree -> cnt == 1){
			fast_tree -> max = fast_tree -> min;
		}
		else{
			fast_tree -> max = ((*triggered_new_max_key) >> 32) & OFF_16_MASK;
		}
	}



	return;
}


void remove_fast_tree_nonmain_16(Fast_Tree * root, Fast_Tree_16 * fast_tree, uint16_t key, bool * tree_removed){

	// OUTWARD ROOT 32 HAS ALREADY INITIALIZED TABLE FROM INIT_FAST_TREE()

	*tree_removed = false;

	uint8_t ind_8 = (key & IND_8_MASK) >> 8;
	uint8_t off_8 = (key & OFF_8_MASK);

	Fast_Tree_Outward_Leaf * inward_leaf_ref = NULL;
	Fast_Tree_Outward_Leaf * outward_leaf_ref = &(fast_tree -> outward_leaf);

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &inward_leaf_ref);

	clear_bitvector(inward_leaf_ref -> bit_vector, off_8);
	
	fast_tree -> cnt -= 1;
	
	uint16_t new_min = key;
	uint16_t new_max = key;
	// Removing from outward tree implies we know that the inward tree exists

	// we can remove this leaf if this was the last element left
	if ((inward_leaf_ref -> min == off_8) && (inward_leaf_ref -> max == off_8)){

		remove_fast_table(&(fast_tree -> inward_leaves), &ind_8, NULL);

		(root -> tree_stats).num_outward_leaves -= 1;

		clear_bitvector(outward_leaf_ref -> bit_vector, ind_8);

		Fast_Tree_Outward_Leaf * new_min_max_leaf;

		// if this was the last leaf in the tree we should remove the tree
		if ((outward_leaf_ref -> min == ind_8) && (outward_leaf_ref -> max == ind_8)){
			destroy_fast_table(&(fast_tree -> inward_leaves));
			*tree_removed = true;
			return;
		}
		// if we are removing this leaf we might need to update the min/max value of parent tree
		// so lookup the next index and get its min/max offset and combine to create min/max value
		else if (outward_leaf_ref -> min == ind_8){
			outward_leaf_ref -> min = lookup_bitvector_next(outward_leaf_ref -> bit_vector, ind_8);
			find_fast_table(&(fast_tree -> inward_leaves), &outward_leaf_ref -> min, false, (void **) &new_min_max_leaf);
			new_min = (uint16_t) (outward_leaf_ref -> min << 8) + (uint16_t) new_min_max_leaf -> min;
		}
		else if (outward_leaf_ref -> max == ind_8){
			outward_leaf_ref -> max = lookup_bitvector_prev(outward_leaf_ref -> bit_vector, ind_8);
			find_fast_table(&(fast_tree -> inward_leaves), &outward_leaf_ref -> max, false, (void **) &new_min_max_leaf);
			new_max = (uint16_t) (outward_leaf_ref -> max << 8) + (uint16_t) new_min_max_leaf -> max;
		}
	}
	else if (inward_leaf_ref -> min == off_8){
		inward_leaf_ref -> min = lookup_bitvector_next(inward_leaf_ref -> bit_vector, off_8);
		new_min = (uint16_t) (ind_8 << 8) + (uint16_t) inward_leaf_ref -> min;
	}
	else if (inward_leaf_ref -> max == off_8){
		inward_leaf_ref -> max = lookup_bitvector_prev(inward_leaf_ref -> bit_vector, off_8);
		new_max = (uint16_t) (ind_8 << 8) + (uint16_t) inward_leaf_ref -> max;
	}

	if (fast_tree -> min == key){
		if (fast_tree -> cnt == 1){
			fast_tree -> min = fast_tree -> max;
		}
		else{
			fast_tree -> min = new_min;
		}

	}
	else if (fast_tree -> max == key){
		if (fast_tree -> cnt == 1){
			fast_tree -> max = fast_tree -> min;
		}
		else{
			fast_tree -> max = new_max;
		}
	}

	return;
}


void remove_fast_tree_outward_16(Fast_Tree * root, Fast_Tree_Outward_Root_16 * fast_tree, uint16_t key, bool * tree_removed){

	// OUTWARD ROOT 32 HAS ALREADY INITIALIZED TABLE FROM INIT_FAST_TREE()

	*tree_removed = false;

	uint8_t ind_8 = (key & IND_8_MASK) >> 8;
	uint8_t off_8 = (key & OFF_8_MASK);

	Fast_Tree_Outward_Leaf * inward_leaf_ref = NULL;
	Fast_Tree_Outward_Leaf * outward_leaf_ref = &(fast_tree -> outward_leaf);

	find_fast_table(&(fast_tree -> inward_leaves), &ind_8, false, (void **) &inward_leaf_ref);

	clear_bitvector(inward_leaf_ref -> bit_vector, off_8);
	
	// Removing from outward tree implies we know that the inward tree exists

	// we can remove this leaf if this was the last element left
	if ((inward_leaf_ref -> min == off_8) && (inward_leaf_ref -> max == off_8)){

		remove_fast_table(&(fast_tree -> inward_leaves), &ind_8, NULL);

		(root -> tree_stats).num_outward_leaves -= 1;

		clear_bitvector(outward_leaf_ref -> bit_vector, ind_8);

		// if this was the last leaf in the tree we should remove the tree
		if ((outward_leaf_ref -> min == ind_8) && (outward_leaf_ref -> max == ind_8)){
			destroy_fast_table(&(fast_tree -> inward_leaves));
			*tree_removed = true;
			return;
		}
		else if (outward_leaf_ref -> min == ind_8){
			outward_leaf_ref -> min = lookup_bitvector_next(outward_leaf_ref -> bit_vector, ind_8);
		}
		else if (outward_leaf_ref -> max == ind_8){
			outward_leaf_ref -> max = lookup_bitvector_prev(outward_leaf_ref -> bit_vector, ind_8);
		}
	}
	else if (inward_leaf_ref -> min == off_8){
		inward_leaf_ref -> min = lookup_bitvector_next(inward_leaf_ref -> bit_vector, off_8);
	}
	else if (inward_leaf_ref -> max == off_8){
		inward_leaf_ref -> max = lookup_bitvector_prev(inward_leaf_ref -> bit_vector, off_8);
	}

	return;
}

void remove_fast_tree_32(Fast_Tree * root, Fast_Tree_32 * fast_tree, uint32_t key, uint64_t * triggered_new_min_key, uint64_t * triggered_new_max_key, bool * element_removed, bool * tree_removed, void * prev_value){

	
	*tree_removed = false;

	uint16_t ind_16 = (key & IND_16_MASK) >> 16;
	uint16_t off_16 = (key & OFF_16_MASK);

	Fast_Tree_16 * inward_tree_16_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (!inward_tree_16_ref){
		*element_removed = false;
		return;
	}

	bool child_tree_removed = false;
	remove_fast_tree_16(root, inward_tree_16_ref, off_16, triggered_new_min_key, triggered_new_max_key, element_removed, &child_tree_removed, prev_value);

	if (!element_removed){
		return;
	}

	fast_tree -> cnt -= 1;

	if (child_tree_removed){
		// remove this tree from table
		remove_fast_table(&(fast_tree -> inward), &ind_16, NULL);

		(root -> tree_stats).num_trees_16 -= 1;

		// we need to remove ind_16 from the outward root as well
		bool outward_removed = false;
		remove_fast_tree_outward_16(root, &(fast_tree -> outward_root), ind_16, &outward_removed);
		
		if (outward_removed){
			destroy_fast_table(&(fast_tree -> inward));
			*tree_removed = true;
			return;
		}
	}

	if (fast_tree -> min == key){
		
		if (fast_tree -> cnt == 1){
			fast_tree -> min = fast_tree -> max;
		}
		else{
			fast_tree -> min = *triggered_new_min_key & OFF_32_MASK;
		}
	}
	else if (fast_tree -> max == key){
		if (fast_tree -> cnt == 1){
			fast_tree -> max = fast_tree -> min;
		}
		else{
			fast_tree -> max = *triggered_new_max_key & OFF_32_MASK;
		}
	}

	return;

}

void remove_fast_tree_outward_32(Fast_Tree * root, Fast_Tree_Outward_Root_32 * fast_tree, uint32_t key){

	uint16_t ind_16 = (key & IND_16_MASK) >> 16;
	uint16_t off_16 = (key & OFF_16_MASK);

	Fast_Tree_16 * inward_tree_16_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_16, false, (void **) &inward_tree_16_ref);

	bool child_tree_removed;
	remove_fast_tree_nonmain_16(root, inward_tree_16_ref, off_16, &child_tree_removed);
	
	if (child_tree_removed){

		(root -> tree_stats).num_nonmain_trees_16 -= 1;

		remove_fast_table(&(fast_tree -> inward), &ind_16, NULL);

		bool outward_removed = false;
		remove_fast_tree_outward_16(root, &(fast_tree -> outward_root), ind_16, &outward_removed);

		// don't destroy the inward tree out outward_32, this is created at init time
	}

	return;


}

// returns 0 on success -1 on error
// fails is key is already in the tree and overwrite set to false
// if key was already in the tree and had a non-null value, then copies the previous value into prev_value
int remove_fast_tree(Fast_Tree * fast_tree, uint64_t key, void * prev_value) {

	if ((fast_tree -> cnt == 0) || ((key < fast_tree -> min) || (key > fast_tree -> max))){	
		return -1;
	}	

	uint32_t ind_32 = (key & IND_32_MASK) >> 32;
	uint32_t off_32 = (key & OFF_32_MASK);

	Fast_Tree_32 * inward_tree_32_ref = NULL;

	find_fast_table(&(fast_tree -> inward), &ind_32, false, (void **) &inward_tree_32_ref);
	// not in the tree so we need to create
	// also means that ind_32 wan't inserted into the outward root
	if (!inward_tree_32_ref){
		return -1;
	}

	bool child_tree_removed = false;
	bool element_removed = false;

	uint64_t triggered_new_min_key = key;
	uint64_t triggered_new_max_key = key;

	remove_fast_tree_32(fast_tree, inward_tree_32_ref, off_32, &triggered_new_min_key, &triggered_new_max_key, &element_removed, &child_tree_removed, prev_value);

	if (!element_removed){
		return -1;
	}

	fast_tree -> cnt -= 1;

	if (child_tree_removed){
		(fast_tree -> tree_stats).num_trees_32 -= 1;
		remove_fast_table(&(fast_tree -> inward), &ind_32, NULL);
		remove_fast_tree_outward_32(fast_tree, &(fast_tree -> outward_root), ind_32);
	}

	if (fast_tree -> cnt == 0){
		fast_tree -> min = TREE_MAX;
		fast_tree -> max = 0;
		return 0;
	}

	if (fast_tree -> min == key){
		fast_tree -> min = triggered_new_min_key;
	}
	else if (fast_tree -> max == key){
		fast_tree -> max = triggered_new_max_key;
	}

	return 0;
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

