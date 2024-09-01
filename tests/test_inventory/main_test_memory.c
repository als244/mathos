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


	// 1.) Initialize HSA memory

	printf("Intializing Backend Memory...\n");

	Hsa_Memory * hsa_memory = hsa_init_memory();
	if (hsa_memory == NULL){
		fprintf(stderr, "Error: hsa_init_memory failed\n");
	}

	printf("\nFound %d HSA agents each with a mempool!\n", hsa_memory -> n_agents);

	
	// 2 MB Chunk Size
	int device_id = 0;
	uint64_t chunk_size = 1U << 16;
	uint64_t num_chunks = 10000;

	// should return a 2GB region
	ret = hsa_add_device_memory(hsa_memory, device_id, num_chunks, chunk_size);
	if (ret != 0){
		fprintf(stderr, "Error: failed to add device memory\n");
		return -1;
	}



	// 2.) Intiialize common (across accelerator backends) system memory struct

	// currently this function is within hsa_memory.c, but would be nicer to rearrange things...
	Memory * memory =  init_backend_memory(hsa_memory);
	if (memory == NULL){
		fprintf(stderr, "Error: failed to initialize backend memory\n");
		return -1;
	}


	// REQUEST #1

	Mem_Reservation mem_reservation_test;

	// allocate on device 0 and wanting chunk_size bytes;
	mem_reservation_test.pool_id = 0;
	mem_reservation_test.size_bytes = 2 * chunk_size; 


	printf("\n\n\n1.) Requesting %lu Bytes on Pool %d...\n", mem_reservation_test.size_bytes, mem_reservation_test.pool_id);

	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = reserve_memory(memory, &mem_reservation_test);
	clock_gettime(CLOCK_MONOTONIC, &stop);


	if (ret != 0){
		fprintf(stderr, "Error: failed to reserve memory 1 on pool id %d of size %lu\n", 
					mem_reservation_test.pool_id, mem_reservation_test.size_bytes);
		return -1;
	}

	// now we expect this to have start id 0
	printf("\tStart Chunk ID: %lu\n\tNum Chunks: %lu\n", mem_reservation_test.start_chunk_id, mem_reservation_test.num_chunks);

	timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
	timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
	elapsed_ns = timestamp_stop - timestamp_start;

	printf("\n\tElasped Time (ns): %lu\n\n", elapsed_ns);




	// REQUEST #2

	Mem_Reservation mem_reservation_test_2;

	mem_reservation_test_2.pool_id = 0;
	mem_reservation_test_2.size_bytes = 2 * chunk_size;

	printf("\n\n2.) Requesting %lu Bytes on Pool %d...\n", mem_reservation_test_2.size_bytes, mem_reservation_test_2.pool_id); 

	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = reserve_memory(memory, &mem_reservation_test_2);
	clock_gettime(CLOCK_MONOTONIC, &stop);

	if (ret != 0){
		fprintf(stderr, "Error: failed to reserve memory 2 on pool id %d of size %lu\n", 
					mem_reservation_test_2.pool_id, mem_reservation_test_2.size_bytes);
		return -1;
	}

	// now we expect this to have start id 2
	printf("\tStart Chunk ID: %lu\n\tNum Chunks: %lu\n", mem_reservation_test_2.start_chunk_id, mem_reservation_test_2.num_chunks);

	timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
	timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
	elapsed_ns = timestamp_stop - timestamp_start;

	printf("\n\tElasped Time (ns): %lu\n\n", elapsed_ns);




	// RELEASING #1

	printf("\n\n3.) Releasing reservation...\n\tReleased Start Chunk ID: %lu\n\tReleased Num Chunks: %lu\n", mem_reservation_test.start_chunk_id, mem_reservation_test.num_chunks); 

	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = release_memory(memory, &mem_reservation_test);
	clock_gettime(CLOCK_MONOTONIC, &stop);

	if (ret){
		fprintf(stderr, "Error: release memory failed\n");
		return -1;
	}

	timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
	timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
	elapsed_ns = timestamp_stop - timestamp_start;

	printf("\n\tElasped Time (ns): %lu\n\n", elapsed_ns);






	// REQUEST #3

	Mem_Reservation mem_reservation_test_3;

	mem_reservation_test_3.pool_id = 0;
	mem_reservation_test_3.size_bytes = chunk_size; 

	printf("\n\n4.) Requesting %lu Bytes on Pool %d...\n", mem_reservation_test_3.size_bytes, mem_reservation_test_3.pool_id); 

	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = reserve_memory(memory, &mem_reservation_test_3);
	clock_gettime(CLOCK_MONOTONIC, &stop);

	if (ret != 0){
		fprintf(stderr, "Error: failed to reserve memory 3 on pool id %d of size %lu\n", 
					mem_reservation_test_3.pool_id, mem_reservation_test_3.size_bytes);
		return -1;
	}

	// now we expect this to have start id 0
	printf("\tStart Chunk ID: %lu\n\tNum Chunks: %lu\n", mem_reservation_test_3.start_chunk_id, mem_reservation_test_3.num_chunks);

	timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
	timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
	elapsed_ns = timestamp_stop - timestamp_start;

	printf("\n\tElasped Time (ns): %lu\n\n", elapsed_ns);






	// REQUEST #4

	Mem_Reservation mem_reservation_test_4;

	mem_reservation_test_4.pool_id = 0;
	mem_reservation_test_4.size_bytes = chunk_size; 

	printf("\n\n5.) Requesting %lu Bytes on Pool %d...\n", mem_reservation_test_4.size_bytes, mem_reservation_test_4.pool_id);

	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = reserve_memory(memory, &mem_reservation_test_4);
	clock_gettime(CLOCK_MONOTONIC, &stop);

	if (ret != 0){
		fprintf(stderr, "Error: failed to reserve memory 4 on pool id %d of size %lu\n", 
					mem_reservation_test_4.pool_id, mem_reservation_test_4.size_bytes);
		return -1;
	}

	// now we expect this to have start id 0
	printf("\tStart Chunk ID: %lu\n\tNum Chunks: %lu\n", mem_reservation_test_4.start_chunk_id, mem_reservation_test_4.num_chunks);

	timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
	timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;
	elapsed_ns = timestamp_stop - timestamp_start;

	printf("\n\tElasped Time (ns): %lu\n\n\n", elapsed_ns);


	exit(0);
}