#ifndef MEMORY_CLIENT_H
#define MEMORY_CLIENT_H

#include "common.h"
#include "memory.h"

// Responsbile for populating the mem_reservation
// Upon success returns mem_reservation -> buffer

// However, the mem_reservation needs to be saved so it can later be passed into release memory
void * reserve_memory(Memory * memory, Mem_Reservation * mem_reservation, Mem_Op_Timestamps * op_timestamps);

void release_memory(Memory * memory, Mem_Reservation * mem_reservation, Mem_Op_Timestamps * op_timestamps);


#endif