#include "fast_tree.h"


// Note: that the hash function within each table in the tree is the simple modulus
// of the table size. If we assume uniform distribution of keys across the key-space
// at each level (32, 16, 8, 8) then this is the best we can do and no need
// to be fancy. It takes care of linearly-clustered regions by default, unless there are unique 
// patterns that exist between levels

uint64_t hash_func_32(void * key_ref, uint64_t table_size) {
	uint32_t key = *((uint32_t *) key_ref);
	return key % table_size;
}

uint64_t hash_func_16(void * key_ref, uint64_t table_size){
	uint16_t key = *((uint16_t *) key_ref);
	return key % table_size;
}

uint64_t hash_func_8(void * key_ref, uint64_t table_size){
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

	Fast_Table_Config * table_config_32 = save_fast_table_config(&hash_func_32, sizeof(uint32_t), sizeof(Fast_Tree_32), 
						FAST_TREE_32_MIN_TABLE_SIZE, FAST_TREE_32_MAX_TABLE_SIZE, FAST_TREE_32_LOAD_FACTOR, FAST_TREE_32_SHRINK_FACTOR);
	if (!table_config_32){
		fprintf(stderr, "Error: failed to create 32 config\n");
		return NULL;
	}

	Fast_Table_Config * table_config_16 = save_fast_table_config(&hash_func_16, sizeof(uint16_t), sizeof(Fast_Tree_16), 
						FAST_TREE_16_MIN_TABLE_SIZE, FAST_TREE_16_MAX_TABLE_SIZE, FAST_TREE_16_LOAD_FACTOR, FAST_TREE_16_SHRINK_FACTOR);
	if (!table_config_16){
		fprintf(stderr, "Error: failed to create 16 config\n");
		return NULL;
	}


	Fast_Table_Config * table_config_8 = save_fast_table_config(&hash_func_8, sizeof(uint8_t), sizeof(Fast_Tree_8), 
						FAST_TREE_8_MIN_TABLE_SIZE, FAST_TREE_8_MAX_TABLE_SIZE, FAST_TREE_8_LOAD_FACTOR, FAST_TREE_8_SHRINK_FACTOR);
	if (!table_config_8){
		fprintf(stderr, "Error: failed to create 8 config\n");
		return NULL;
	}

	
	Fast_Table_Config * table_config_outward_leaf = save_fast_table_config(&hash_func_8, sizeof(uint8_t), sizeof(Fast_Tree_Outward_Leaf), 
						FAST_TREE_OUTWARD_LEAF_MIN_TABLE_SIZE, FAST_TREE_OUTWARD_LEAF_MAX_TABLE_SIZE, FAST_TREE_OUTWARD_LEAF_LOAD_FACTOR, FAST_TREE_OUTWARD_LEAF_SHRINK_FACTOR);
	if (!table_config_outward_leaf){
		fprintf(stderr, "Error: failed to create aux leaf config\n");
		return NULL;
	}



	// This is the table for the all vertical path in the main tree (which actually stores all the keys)
	// These leaves will have Fast_Table with values that were inserted into the tree and also have linked list pointers
	// to the other main leaves

	
	Fast_Table_Config * table_config_main_leaf = save_fast_table_config(&hash_func_8, sizeof(uint8_t), sizeof(Fast_Tree_Leaf), 
						FAST_TREE_MAIN_LEAF_MIN_TABLE_SIZE, FAST_TREE_MAIN_LEAF_MAX_TABLE_SIZE, FAST_TREE_MAIN_LEAF_LOAD_FACTOR, FAST_TREE_MAIN_LEAF_SHRINK_FACTOR);
	if (!table_config_main_leaf){
		fprintf(stderr, "Error: failed to create main_leaf config\n");
		return NULL;
	}


	// Notice that the value size bytes for this table is the value that was passed in to this intialization function!!!
	Fast_Table_Config * table_config_value = save_fast_table_config(&hash_func_8, sizeof(uint8_t), value_size_bytes, 
						FAST_TREE_VALUE_MIN_TABLE_SIZE, FAST_TREE_VALUE_MAX_TABLE_SIZE, FAST_TREE_VALUE_LOAD_FACTOR, FAST_TREE_VALUE_SHRINK_FACTOR);
	if (!table_config_value){
		fprintf(stderr, "Error: failed to create main_leaf config\n");
		return NULL;
	}


	fast_tree -> table_config_32 = table_config_32;
	fast_tree -> table_config_16 = table_config_16;
	fast_tree -> table_config_8 = table_config_8;
	fast_tree -> table_config_outward_leaf = table_config_outward_leaf;
	fast_tree -> table_config_main_leaf = table_config_main_leaf;
	fast_tree -> table_config_value = table_config_value;


	fast_tree -> cnt = 0;

	// ensure that the first item will reset min and max appropriately
	fast_tree -> min = 0xFFFFFFFFFFFFFFFF;
	fast_tree -> max = 0;



	// we want to intialize a table that will store vertical children
	// of the root
	


	int ret = init_fast_table(&(fast_tree -> inward), table_config_32);
	if (ret != 0){
		fprintf(stderr, "Error: failure to initialize fast tree root's inward table\n");
		return NULL;
	}


	memset(&(fast_tree -> outward), 0, sizeof(Fast_Tree_Outward_32));


	Item_Cmp leaf_cmp = &fast_tree_leaf_cmp;
	fast_tree -> ordered_leaves = init_deque(leaf_cmp);
	if (!(fast_tree -> ordered_leaves)){
		fprintf(stderr, "Error: failure to initialize the ordered_leaves deque\n");
		return NULL;
	}


	return fast_tree;

}