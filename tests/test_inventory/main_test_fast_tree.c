#include "sys.h"
#include "fingerprint.h"
#include "exchange_client.h"
#include "utils.h"
#include "self_net.h"


// Temporary
#include "hsa_memory.h"
#include "memory.h"
#include "rocblas_funcs.h"


#include "fast_table.h"
#include "fast_tree.h"

int main(int argc, char * argv[]){


	struct timespec start, stop;
	uint64_t timestamp_start, timestamp_stop, elapsed_ns;

	int ret;
	
	char * master_ip_addr, * self_ip_addr;

	/*
	if ((argc != 2) && (argc != 3)){
		fprintf(stderr, "Error: Usage ./testWorker <master_ip_addr> <self_ip_addr>\n");
		return -1;
	}
	
	master_ip_addr = argv[1];
	self_ip_addr = NULL;
	if (argc == 3){
		self_ip_addr = argv[2];
	}
	*/




	// TESTING FAST TABLE


	Mem_Range range_1;
	range_1.range_size = 70;

	Mem_Range range_2;
	range_2.range_size = 50;

	Mem_Range range_3;
	range_3.range_size = 70;

	Mem_Range range_4;
	range_4.range_size = 1024;

	Mem_Range range_5;
	range_5.range_size = 222222;

	Mem_Range range_6;
	range_6.range_size = (1UL << 32) + 222222;


	Deque * mem_ranges_70 = init_deque(NULL);
	if (mem_ranges_70 == NULL){
		fprintf(stderr, "Error: init deque failed\n");
		return -1;
	}

	Deque * mem_ranges_50 = init_deque(NULL);
	if (mem_ranges_50 == NULL){
		fprintf(stderr, "Error: init deque failed\n");
		return -1;
	}

	Deque * mem_ranges_1024 = init_deque(NULL);
	if (mem_ranges_1024 == NULL){
		fprintf(stderr, "Error: init deque failed\n");
		return -1;
	}


	Deque * mem_ranges_222222 = init_deque(NULL);
	if (mem_ranges_1024 == NULL){
		fprintf(stderr, "Error: init deque failed\n");
		return -1;
	}


	Deque * mem_ranges_big = init_deque(NULL);
	if (mem_ranges_1024 == NULL){
		fprintf(stderr, "Error: init deque failed\n");
		return -1;
	}


	ret = insert_deque(mem_ranges_70, BACK_DEQUE, &range_1);
	if (ret != 0){
		fprintf(stderr, "Error: insert_deque failed\n");
		return -1;
	}

	ret = insert_deque(mem_ranges_70, BACK_DEQUE, &range_3);
	if (ret != 0){
		fprintf(stderr, "Error: insert_deque failed\n");
		return -1;
	}

	ret = insert_deque(mem_ranges_50, BACK_DEQUE, &range_2);
	if (ret != 0){
		fprintf(stderr, "Error: insert_deque failed\n");
		return -1;
	}

	ret = insert_deque(mem_ranges_1024, BACK_DEQUE, &range_4);
	if (ret != 0){
		fprintf(stderr, "Error: insert_deque failed\n");
		return -1;
	}

	ret = insert_deque(mem_ranges_222222, BACK_DEQUE, &range_5);
	if (ret != 0){
		fprintf(stderr, "Error: insert_deque failed\n");
		return -1;
	}

	ret = insert_deque(mem_ranges_big, BACK_DEQUE, &range_6);
	if (ret != 0){
		fprintf(stderr, "Error: insert_deque failed\n");
		return -1;
	}

	printf("INITIALIZING TREE...\n\n");

	Fast_Tree * fast_tree = init_fast_tree(true);
	if (!fast_tree){
		fprintf(stderr, "Error: init fast tree failed\n");
		return -1;
	}

	void * prev_value;
	
	// mem_range_size is the key

	printf("INSERTING RANGE SIZE + DEQUE INTO TREE...\n\n\n");

	ret = insert_fast_tree(fast_tree, 70, mem_ranges_70, false, &prev_value);

	if (ret != 0){
		fprintf(stderr, "Error: insert_fast_tree failed\n");
	}

	ret = insert_fast_tree(fast_tree, 50, mem_ranges_50, false, &prev_value);

	if (ret != 0){
		fprintf(stderr, "Error: insert_fast_tree failed\n");
	}

	ret = insert_fast_tree(fast_tree, 1024, mem_ranges_1024, false, &prev_value);

	if (ret != 0){
		fprintf(stderr, "Error: insert_fast_tree failed\n");
	}

	ret = insert_fast_tree(fast_tree, 222222, mem_ranges_222222, false, &prev_value);

	if (ret != 0){
		fprintf(stderr, "Error: insert_fast_tree failed\n");
	}

	ret = insert_fast_tree(fast_tree, (1UL << 32) + 222222, mem_ranges_big, false, &prev_value);

	if (ret != 0){
		fprintf(stderr, "Error: insert_fast_tree failed\n");
	}


	uint64_t search_key = 40;
	Fast_Tree_Result search_result;
	FastTreeSearchModifier search_type = FAST_TREE_EQUAL_OR_NEXT;
	

	

	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = search_fast_tree(fast_tree, search_key, search_type, &search_result);
	clock_gettime(CLOCK_MONOTONIC, &stop);


	if (ret != 0){
		fprintf(stderr, "Error: no key found\n");
		return -1;
	}

	uint64_t found_key = search_result.key;
	Fast_Tree_Leaf * found_leaf = search_result.fast_tree_leaf;
	Deque * found_deque = (Deque *) search_result.value;

	printf("Search Key: %lu => Search equal/prev result found key: %lu\n", search_key, found_key);

	if (!found_deque){
		fprintf(stderr, "Error: was expecting the value to be populated with deque, but returned null\n");
		return -1;
	}

	timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
	timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;

	elapsed_ns = timestamp_stop - timestamp_start;


	uint64_t remove_key = 50;

	printf("\nRemoving key %lu...\n\n", remove_key);

	prev_value = NULL;
	ret = remove_fast_tree(fast_tree, remove_key, &prev_value);

	if (ret != 0){
		fprintf(stderr, "Error: could not remove key\n");
		return -1;
	}

	if (!prev_value){
		fprintf(stderr, "Error: was expecting a previous value to be non-null\n");
		return -1;
	}

	ret = search_fast_tree(fast_tree, search_key, search_type, &search_result);

	found_key = search_result.key;
	found_leaf = search_result.fast_tree_leaf;
	found_deque = (Deque *) search_result.value;

	printf("Search Key: %lu => Search equal/prev result found key: %lu\n", search_key, found_key);

	if (!found_deque){
		fprintf(stderr, "Error: was expecting the value to be populated with deque, but returned null\n");
		return -1;
	}

	printf("Simple test success!!\n\tElasped Search Time (ns): %lu\n\n", elapsed_ns);


	return 0;


}