#include "memory_client.h"


size_t sizeof_dtype(DataType dtype){

	switch(dtype){
		case FP8:
			return 1;
		case FP16:
			return 2;
		case BF16:
			return 2;
		case FP32:
			return 4;
		case U_INT8:
			return 1;
		case U_INT16:
			return 2;
		case U_INT32:
			return 4;
		case U_INT64:
			return 8;
		case REG_INT:
			return sizeof(int);
		case REG_LONG:
			return sizeof(long);
		default:
			return 0;
	}
}

// This assumes that the client
// has populated the mem_op with the correct reservation details

// For op_type == MEMORY_RESERVATION, mem_reservation will be populated with the correct details
// and the pointer to the buffer will be returned

// For opt

// can optionally pass in Mem_Op_Timestamps to get timestamp information about request
MemOpStatus submit_memory_op(Memory * memory, MemOpType op_type, Mem_Reservation * mem_reservation, Mem_Op_Timestamps * op_timestamps) {

	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);

	uint64_t timestamp_submitted = time.tv_sec * 1e9 + time.tv_nsec;

	Mem_Op new_mem_op;

	new_mem_op.type = op_type;
	new_mem_op.mem_reservation = mem_reservation;
	new_mem_op.is_complete = false;

	new_mem_op.timestamps.submitted = timestamp_submitted;

	int fifo_id;

	if (op_type == MEMORY_RESERVATION){
		fifo_id = mem_reservation -> pool_id;
	}
	else if (op_type == MEMORY_RELEASE){
		fifo_id = mem_reservation -> fulfilled_pool_id;
	}

	// set the system mempool to have id num_devices
	if (fifo_id == -1){
		fifo_id = memory -> num_devices;
	}

	if (unlikely((fifo_id < 0) || (fifo_id > memory -> num_devices))) {
		fprintf(stderr, "Error: trying to submit memory op to fifo id %d, but this is not available\n", fifo_id);
		return MEMORY_INVALID_OP;
	}

	Fifo ** mem_op_fifos = memory -> mem_op_fifos;

	Mem_Op * new_mem_op_ptr = &new_mem_op;

	// producing is blocking, but we can control the amount of 
	// potential blocking here with MEMORY_OPS_BUFFER_MAX_REQUESTS_PER_MEMPOOL...
	produce_fifo(mem_op_fifos[fifo_id], &new_mem_op_ptr);
	clock_gettime(CLOCK_MONOTONIC, &time);
	uint64_t timestamp_queued = time.tv_sec * 1e9 + time.tv_nsec;
	new_mem_op.timestamps.queued = timestamp_queued;


	// Choosing to have this function be blocking until memory operation is complete
	// however doesn't need to be....


	// wait until the server marks this as completed

	// not sure if we need to mark as volatile for compiler to 
	// not optimize away the loop waiting for completition...
	volatile bool * is_complete_ptr = &(new_mem_op.is_complete);

	while (!(*is_complete_ptr)) {}


	// COULD POPULATE DATASET FOR ANALYZING THE MEM_OP_TIMESTAMPS HERE!!!!
	if (op_timestamps){
		memcpy(op_timestamps, &(new_mem_op.timestamps), sizeof(Mem_Op_Timestamps));
	}

	return new_mem_op.status;
}


// Responsbile for populating the mem_reservation
// Upon success returns mem_reservation -> buffer

// However, the mem_reservation needs to be saved so it can later be passed into release memory
void * reserve_memory(Memory * memory, Mem_Reservation * mem_reservation, Mem_Op_Timestamps * op_timestamps){

	MemOpStatus op_status = submit_memory_op(memory, MEMORY_RESERVATION, mem_reservation, op_timestamps);

	if (op_status != MEMORY_SUCCESS){
		return NULL;
	}

	return mem_reservation -> buffer;
}



void release_memory(Memory * memory, Mem_Reservation * mem_reservation, Mem_Op_Timestamps * op_timestamps){
	submit_memory_op(memory, MEMORY_RELEASE, mem_reservation, op_timestamps);
	return;
}


// int save_mem_ref(Memory * memory, Mem_Ref * ref, uint64_t offset, uint64_t len, char * filepath) {

// 	int ret;

// 	// create file

// 	FILE * fp = fopen(filepath, "wb");
// 	if (!fp){
// 		fprintf(stderr, "Error: unable to open file: %s\n", filepath);
// 		return -1;
// 	}


// 	ssize_t n_written;

// 	// check if on device in which case we should transfer to cpu first

// 	int src_pool_id = (ref -> reservation).fulfilled_pool_id;

// 	void * data_ptr = (void *) ((uint64_t) (ref -> data) + offset);

// 	if (len == 0){
// 		len = ref -> size;
// 	}

// 	if (src_pool_id != SYSTEM_MEMPOOL_ID){

// 		void * host_buf = malloc(ref -> size);

// 		ret = cuda_copy_to_host_memory(memory -> backend_memory, src_pool_id, data_ptr, len, host_buf);
// 		if (ret){
// 			fprintf(stderr, "Error: unable to copy %lu bytes from device pool: %d, to host\n", len, src_pool_id);
// 			return -1;
// 		}
// 		n_written = fwrite(host_buf, 1, len, fp);
// 		free(host_buf);

// 	}
// 	else{
// 		n_written = fwrite(data_ptr, 1, len, fp);
// 	}

// 	if (n_written != len){
// 		fprintf(stderr, "Error: unable to write ref to file. Wrote %lu bytes, expected %lu\n", n_written, len);
// 		return -1;
// 	}

// 	fclose(fp);

// 	return 0;

// }


// // For now just assuming replica -> pool_id = SYSTEM_MEMPOOL_ID
// // but could easily just change the data transfer functions to be dev/dev f

// // Assumes that to_update -> size has already been updated correctly
// // and that to_update -> data is a pointer
// int copy_mem_ref(Memory * memory, int dest_pool_id, Mem_Ref * new_ref, Mem_Ref * replica_ref, void * stream_ref, sem_t * sem_to_post){

// 	Mem_Reservation * cur_res = &(new_ref -> reservation);
	
// 	// cases where we need to allocate
// 	// otherwise we can just overwrite the previous allocation (common for cyclic patterns of replacing layers...)
// 	if (!(new_ref -> data) || (new_ref -> size != replica_ref -> size) || (cur_res -> fulfilled_pool_id != dest_pool_id)) {

// 		// release previous reservation and create new one
// 		if (new_ref -> size > 0){
// 			release_memory(memory, cur_res, NULL);
// 		}

// 		memset(cur_res, 0, sizeof(Mem_Reservation));

// 		// make new reservation on this pool
// 		cur_res -> pool_id = dest_pool_id;
// 		cur_res -> size_bytes = replica_ref -> size;

// 		void * new_data_ptr = reserve_memory(memory, cur_res, NULL);
// 		if (!new_data_ptr){
// 			fprintf(stderr, "Error: failed to reserve memory on pool id %d of size %lu\n", 
// 							cur_res -> pool_id, cur_res -> size_bytes);
// 			return -1;
// 		}

// 		new_ref -> data = new_data_ptr;
// 		new_ref -> size = replica_ref -> size;

// 	}
	

// 	// not using this for now because assuming prev_pool == SYSTEM_MEMPOOL_ID
// 	Mem_Reservation * replica_res = &(replica_ref -> reservation);

// 	// FOR NOW ASSUMING THIS IS ON HOST!
// 	int replica_pool = replica_res -> fulfilled_pool_id;

// 	void * replica_data_ptr = replica_ref -> data;

// 	// now assert new_ref -> size == replica_ref -> size
// 	uint64_t data_size = new_ref -> size;

// 	// now assert new_ref -> data has allocation of new_ref -> size
// 	void * new_data_ptr = new_ref -> data;


// 	// TODO: 
// 	//	- THIS SHOULD BE BACKEND_AGNOSTIC!!!
// 	//	- AND ALSO FLEXIBLE WITH DIFFERENT TO/FROM POOL LOCATIONS!
// 	int ret = cuda_async_copy_to_device_memory(memory -> backend_memory, replica_data_ptr, dest_pool_id, new_data_ptr, data_size, stream_ref);
// 	if (ret){
// 		fprintf(stderr, "Error: copying from host to device failed\n");
// 		return -1;
// 	}


// 	if (sem_to_post){

// 		if (!stream_ref){
// 			ret = add_callback_to_post_complete(memory -> backend_memory, dest_pool_id, STREAM_INBOUND, NULL, sem_to_post);
// 		}
// 		else{
// 			// Just for now while interface not fleshed out yet...
// 			CUstream stream = *((CUstream *) stream_ref);
// 			ret = add_callback_to_post_complete(memory -> backend_memory, dest_pool_id, STREAM_CUSTOM, stream, sem_to_post);
// 		}
		
// 		if (ret){
// 			fprintf(stderr, "Error: failed to add callback for posting complete for copy mem ref\n");
// 			return -1;
// 		}

// 	}

// 	return 0;
// }