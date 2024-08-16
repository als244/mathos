#include "fast_tree.h"


// HASH FUNCTION STUFF

static uint8_t pearson_table[] = { 64, 67, 45, 157, 253, 237, 236, 112, 17, 69, 61, 182, 173, 44, 235, 153, 102, 223, 251, 95, 166, 136, 160, 25, 60, 198, 146, 62, 87, 200, 71, 169, 220, 5, 131, 133, 138, 99, 73, 48, 129, 96, 130, 139, 233, 246, 248, 41, 150, 175, 98, 214, 74, 177, 66, 219, 105, 78, 65, 32, 94, 16, 240, 14, 68, 174, 37, 81, 238, 107, 88, 135, 13, 180, 132, 28, 155, 222, 228, 70, 92, 232, 163, 168, 103, 167, 190, 91, 206, 205, 24, 179, 161, 255, 83, 31, 208, 12, 148, 189, 79, 106, 51, 137, 122, 159, 178, 224, 72, 191, 225, 11, 18, 193, 8, 126, 84, 50, 114, 140, 247, 215, 9, 221, 171, 152, 196, 59, 242, 35, 244, 56, 213, 164, 76, 209, 245, 226, 197, 75, 158, 216, 147, 211, 52, 186, 1, 195, 201, 46, 172, 165, 85, 170, 185, 0, 254, 7, 80, 144, 141, 6, 89, 217, 128, 86, 30, 121, 113, 184, 199, 252, 110, 57, 188, 54, 231, 183, 187, 156, 63, 47, 39, 118, 124, 90, 207, 43, 116, 2, 26, 212, 202, 234, 203, 218, 93, 210, 34, 55, 29, 101, 192, 21, 40, 49, 249, 82, 100, 230, 204, 20, 125, 239, 145, 108, 111, 27, 176, 4, 53, 241, 229, 3, 143, 77, 15, 149, 23, 154, 58, 36, 109, 123, 134, 119, 162, 104, 243, 127, 22, 42, 10, 19, 227, 115, 117, 33, 250, 38, 97, 151, 120, 194, 142, 181 };

// THE FOLLOWING OPTIONS ASSUMES BOTH AN INPUT AND OUT SPACE of x bits where hash_func_x
// They will be used to map indexes within each level of the Fast_Tree

// Mixing hash function
// Taken from "https://github.com/shenwei356/uint64-hash-bench?tab=readme-ov-file"
// Credit: Thomas Wang
uint64_t hash_func_64(void * key_ref, uint64_t table_size) {
	uint64_t key = *((uint64_t *) key_ref);
	key = (key << 21) - key - 1;
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);
	return key % table_size;
}


uint64_t hash_func_32(void * key_ref, uint64_t table_size) {
	uint32_t key = *((uint32_t *) key_ref);
	// Take from "https://gist.github.com/badboy/6267743"
	// Credit: Robert Jenkins
	key = (key+0x7ed55d16) + (key<<12);
   	key = (key^0xc761c23c) ^ (key>>19);
   	key = (key+0x165667b1) + (key<<5);
   	key = (key+0xd3a2646c) ^ (key<<9);
   	key = (key+0xfd7046c5) + (key<<3);
   	key = (key^0xb55a4f09) ^ (key>>16);
	return key % table_size;
}

// Using Pearson hashing for 8 and 16 bits

// Ref: https://64nops.wordpress.com/wp-content/uploads/2020/11/pearson.acm33.1990-3.pdf
//		- https://64nops.wordpress.com/2020/12/10/hash-algorithms/
uint64_t hash_func_16(void * key_ref, uint64_t table_size){
	
	uint16_t key = *((uint16_t *) key_ref);
	
	uint8_t hash1 = pearson_table[pearson_table[key & 0x00FF] ^ (key & 0xFF00)];
	uint8_t hash2 = pearson_table[pearson_table[(key & 0x00FF) ^ 1] ^ (key & 0xFF00)];
	return ((hash2 << 8) + hash1) % table_size;
}

// this pearson hash can also be used for 16 bits
// with a table of 65k

// Ref: https://bannister.us/weblog/2005/a-diversion-into-hash-tables-and-binary-searches
uint64_t hash_func_8(void * key_ref, uint64_t table_size){
	uint8_t key = *((uint8_t *) key_ref);
	return pearson_table[key] % table_size;
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

	fast_tree -> cnt = 0;

	// ensure that the first item will reset min and max appropriately
	fast_tree -> min = 0xFFFFFFFFFFFFFFFF;
	fast_tree -> max = 0;



	// we want to intialize a table that will store vertical children
	// of the root

	Hash_Func hash_func_root = hash_func_32;

	// these parameters are definied within config.h

	// not that the maximum number of keys in this table is 2^32
	int ret = init_fast_table(&(fast_tree -> inward), hash_func_root, sizeof(uint32_t), sizeof(Fast_Tree_32), 
						FAST_TREE_32_MIN_TABLE_SIZE, FAST_TREE_32_MAX_TABLE_SIZE, FAST_TREE_32_LOAD_FACTOR, FAST_TREE_32_SHRINK_FACTOR);

	if (ret != 0){
		fprintf(stderr, "Error: failure to initialize fast tree root's vertical\n");
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