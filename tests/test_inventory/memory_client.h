#ifndef MEMORY_CLIENT_H
#define MEMORY_CLIENT_H

#include "common.h"
#include "memory.h"

typedef enum data_type {
	FP4,
	FP8,
	FP16,
	BF16,
	FP32,
	FP64,
	U_INT4,
	U_INT8,
	U_INT16,
	U_INT32,
	U_INT64,
	REG_INT,
	REG_LONG
} DataType;


typedef struct mem_ref {
	void * data;
	uint64_t size;
	// will want to add details about 
	// Mem_Reservations here!
	Mem_Reservation reservation;
} Mem_Ref;

// Responsbile for populating the mem_reservation
// Upon success returns mem_reservation -> buffer

// However, the mem_reservation needs to be saved so it can later be passed into release memory
void * reserve_memory(Memory * memory, Mem_Reservation * mem_reservation, Mem_Op_Timestamps * op_timestamps);

void release_memory(Memory * memory, Mem_Reservation * mem_reservation, Mem_Op_Timestamps * op_timestamps);

int save_mem_ref(Memory * memory, Mem_Ref * ref, uint64_t offset, uint64_t len, char * filepath);

int copy_mem_ref(Memory * memory, int dest_pool_id, Mem_Ref * new_ref, Mem_Ref * replica_ref, void * stream_ref, sem_t * sem_to_post);

size_t sizeof_dtype(DataType dtype);

#endif