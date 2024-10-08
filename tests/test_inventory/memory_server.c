#include "memory_server.h"

#define TO_PRINT_MEMORY_SERVER 0

void * run_memory_server(void * _memory){

	// cast correctly
	Memory * memory = (Memory *) _memory;

	int num_devices = memory -> num_devices;
	int num_mempools = num_devices + 1;

	Fifo ** mem_op_fifos = memory -> mem_op_fifos;

	// FOR NOW:
	// 	- round robin each mempool queue processing mem_ops requests

	Mempool * device_mempools = memory -> device_mempools;
	Mempool system_mempool = memory -> system_mempool;

	


	Mem_Op *** op_backlogs = (Mem_Op ***) malloc(num_mempools * sizeof(Mem_Op **));
	if (!op_backlogs){
		fprintf(stderr, "Error: malloc failed to allocate op_backlog container\n");
		return NULL;
	}

	for (int i = 0; i < num_mempools; i++){
		op_backlogs[i] = (Mem_Op **) calloc(MEMORY_OPS_BUFFER_MAX_REQUESTS_PER_MEMPOOL, sizeof(Mem_Op *));
		if (!op_backlogs[i]){
			fprintf(stderr, "Error: malloc failed to allocate op_backlog for mempool #%d\n", i);
			return NULL;
		}
	}

	uint64_t * num_queued_per_pool = (uint64_t *) calloc(num_mempools, sizeof(uint64_t));
	if (!num_queued_per_pool){
		fprintf(stderr, "Error: malloc failed to allocate num ops queued per pool tracker\n");
		return NULL;
	}

	uint64_t total_to_process = 0;
	uint64_t remain_ops;

	struct timespec start, finish;
	uint64_t timestamp_start, timestamp_finish;

	int num_backup_pools;
	bool is_fulfilled;
	int cur_backup_pool_num;
	int backup_pool_id;

	Mem_Op * cur_op;
	Mem_Reservation * cur_reservation;
	Mempool * cur_mempool;
	MemOpStatus op_status;

	Mem_Reservation completed_reservation;
	printf("[Memory Server] Ready to Process Memory Ops!\n\n");


	while (1){

		// consume all the outstanding requests

		// We really could have seperate subthreads per pool and a single master to handle arbitrating
		// if there are any issues (i.e. a request one pool not satisfied, so give backup)

		// The clients sets op in the fifo corresponding to primary pool id (for requests), 
		// or the fulfilled_pool_id (for releasing)


		// When there are no memory requests this thread will be spinning on trying to consume from empty 
		// queues. Chose this behavior to satisfy clients requests with low-latency, but if becomes a problem
		// of burning too many cycles (power or insuccefient cpus), can make this blocking...
		for (int i = 0; i < num_mempools; i++){
			num_queued_per_pool[i] = consume_all_nonblock_fifo(mem_op_fifos[i], op_backlogs[i]);
			total_to_process += num_queued_per_pool[i];
		}

		while (total_to_process > 0){
			
			// now process round robin all of the outstanding requests
			for (int i = 0; i < num_mempools; i++){

				remain_ops = num_queued_per_pool[i];

				// we could have simple stack to prevent skipping over empty queues but there should only be MAX 10s of pools
				if (remain_ops == 0){
					continue;
				}

				// system mempool
				if (i == num_devices){
					cur_mempool = &system_mempool;
				}
				else{
					cur_mempool = &(device_mempools[i]);
				}

				

				cur_op = op_backlogs[i][remain_ops - 1];
				cur_reservation = cur_op -> mem_reservation;



				clock_gettime(CLOCK_REALTIME, &start);

				// releasing memory should never fail
				if (cur_op -> type == MEMORY_RELEASE){

					

					// assert cur_reservation -> fulfilled_pool_id == i if i < num_devices, else -1 (for system mem)
					op_status = do_release_memory(cur_mempool, cur_reservation);
					
					// should never happen, except when OOM errors for metadata structs
					if (unlikely(op_status != MEMORY_SUCCESS)){
						fprintf(stderr, "Error: had a system error releasing memory on mempool %d... should never happen: Reservation details:\n\tStart chunk id: %lu\n\tNum Chunks: %lu\n\n", 
											cur_reservation -> fulfilled_pool_id, cur_reservation -> start_chunk_id, cur_reservation -> num_chunks);
					}
				}

				if (cur_op -> type == MEMORY_RESERVATION){

					// assert cur_reservation -> pool_id == i if i < num_devices, else -1 (for system mem)
					op_status = do_reserve_memory(cur_mempool, cur_reservation);

					// if we didn't get a valid reservation on primary pool
					if (unlikely(op_status != MEMORY_SUCCESS)){
						// should never happen, except when OOM errors for metadata structs
						if (op_status == MEMORY_SYSTEM_ERROR){
							fprintf(stderr, "Error: had a system error releasing memory on mempool %d... should never happen: Reservation details:\n\tSize bytes: %lu\n", 
											cur_reservation -> pool_id, cur_reservation -> size_bytes);
						}

						// we were OOM on the primary pool, so we need to check all the backup pools supplied by request
						if (op_status == MEMORY_POOL_OOM){

							num_backup_pools = cur_reservation -> num_backup_pools;
							
							// try up to all of the specified backup pools
							// while
							cur_backup_pool_num = 0;
							is_fulfilled = false; 
							while ((!is_fulfilled) && (cur_backup_pool_num < num_backup_pools)){
								backup_pool_id = (cur_reservation -> backup_pool_ids)[cur_backup_pool_num];
								if (backup_pool_id == -1){
									cur_mempool = &system_mempool;
								}
								else{
									cur_mempool = &(device_mempools[i]);
								}

								op_status = do_reserve_memory(cur_mempool, cur_reservation);

								if (op_status == MEMORY_SUCCESS){
									cur_reservation -> fulfilled_pool_id = backup_pool_id;
									is_fulfilled = true;
								}

								if (op_status == MEMORY_SYSTEM_ERROR){
									fprintf(stderr, "Error: had a system error releasing memory on mempool %d... should never happen: Reservation details:\n\tSize bytes: %lu\n", 
												backup_pool_id, cur_reservation -> size_bytes);
									is_fulfilled = true;
								}	

								// otherwise was another POOL_OOM error
								cur_backup_pool_num += 1;
							}
						}
					}
					// if we were successful on first try, set the correct fulfilled pool id
					else{
						cur_reservation -> fulfilled_pool_id = cur_reservation -> pool_id;
					}
				}




				cur_op -> status = op_status;

				clock_gettime(CLOCK_REALTIME, &finish);

				timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
				timestamp_finish = finish.tv_sec * 1e9 + finish.tv_nsec;

				cur_op -> timestamps.start_op = timestamp_start;
				cur_op -> timestamps.finish_op = timestamp_finish;

				if (TO_PRINT_MEMORY_SERVER){
					if (cur_op -> type == MEMORY_RELEASE){
						if (op_status == MEMORY_SUCCESS){
							printf("[Memory Server] Processed Release #%lu on Pool:\n\tMem Client ID: %d\n\tPool Id: %d\n\t\tTotal Free Chunks: %lu\n\tStart Chunk ID: %lu\n\tNum Chunks: %lu\n\n", 
								(cur_mempool -> op_stats).num_releases, cur_reservation -> mem_client_id, cur_reservation -> fulfilled_pool_id, cur_mempool -> total_free_chunks, cur_reservation -> start_chunk_id, cur_reservation -> num_chunks);
						}
						else if (op_status == MEMORY_SYSTEM_ERROR){
							printf("[Memory Server] FATAL ERROR! Unexpected System Error Processing Memory Release:\n\tMem Client ID: %d\n\tPool Id: %d\n\tStart Chunk ID: %lu\n\tNum Chunks: %lu\n\n", 
								cur_reservation -> mem_client_id, cur_reservation -> fulfilled_pool_id, cur_reservation -> start_chunk_id, cur_reservation -> num_chunks);
						}
						
					}

					if (cur_op -> type == MEMORY_RESERVATION){
						if (op_status == MEMORY_SUCCESS){
							printf("[Memory Server] Processed Reservation #%lu on Fulfilled Pool:\n\tMem Client ID: %d\n\tSize Bytes: %lu\n\tStart Chunk ID: %lu\n\tNum Chunks: %lu\n\tRequested Primary Pool Id: %d\n\tFulfilled Pool ID: %d\n\t\tRemaining Free Chunks: %lu\n\n", 
									(cur_mempool -> op_stats).num_reservations, cur_reservation -> mem_client_id, cur_reservation -> size_bytes, cur_reservation -> start_chunk_id, cur_reservation -> num_chunks, 
										cur_reservation -> pool_id, cur_reservation -> fulfilled_pool_id, cur_mempool -> total_free_chunks);
						}
						else if (op_status == MEMORY_POOL_OOM){
							printf("[Memory Server] OOM Processing Reservation for Mem Client ID: %d. No pools specified in reservation had %lu contiguous bytes available.\n\n", cur_reservation -> mem_client_id, cur_reservation -> size_bytes);
						}
						else if (op_status == MEMORY_SYSTEM_ERROR){
							printf("[Memory Server] FATAL ERROR! Unexpected System Error Processing Memory Reservation:\n\tMem Client ID: %d\n\tPrimary Pool Id: %d\n\tSize Bytes: %lu\n\n", 
										cur_reservation -> mem_client_id, cur_reservation -> pool_id, cur_reservation -> size_bytes);
						}
						
					}
				}


				// now set the is_complete flag so client knows
				// it can continue
				cur_op -> is_complete = true;

				// decrement the remaining ops for this pool and total number of ops
				num_queued_per_pool[i] -= 1;
				total_to_process -= 1;
			}
		}
	}
}