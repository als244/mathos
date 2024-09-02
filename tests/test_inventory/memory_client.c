#include "memory_client.h"


// This assumes that the client
// has populated the mem_op with the correct reservation details

// For op_type == MEMORY_RESERVATION, mem_reservation will be populated with the correct details
// and the pointer to the buffer will be returned

// For opt

// can optionally pass in Mem_Op_Timestamps to get timestamp information about request
MemOpStatus submit_memory_op(Memory * memory, MemOpType op_type, Mem_Reservation * mem_reservation, Mem_Op_Timestamps * op_timestamps) {

	struct timespec time;
	clock_gettime(CLOCK_REALTIME, &time);

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
	clock_gettime(CLOCK_REALTIME, &time);
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