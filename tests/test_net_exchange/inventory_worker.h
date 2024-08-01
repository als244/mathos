#ifndef INVENTORY_WORKER_H
#define INVENTORY_WORKER_H

#include "common.h"
#include "fifo.h"
#include "net.h"
#include "work_pool.h"
#include "inventory.h"


// this will be the value within worker_thread_data -> worker_arg
typedef struct inventory_worker_data {
	// Inventory * inventory;
	Net_World * net_world;
} Inventory_Worker_Data;


void * run_inventory_worker(void * _worker_thread_data);

#endif