#ifndef INVENTORY_H
#define INVENTORY_H

#include "common.h"
#include "table.h"
#include "fifo.h"
#include "deque.h"
#include "fingerprint.h"
#include "inventory_messages.h"
#include "work_pool.h"
#include "utils.h"

typedef struct inventory {
	uint64_t num_mempools;
} Inventory;


Inventory * init_inventory();


int do_inventory_function(Inventory * inventory, Ctrl_Message * ctrl_message, uint32_t * ret_num_ctrl_messages, Ctrl_Message ** ret_ctrl_messages);


void print_inventory_message(uint32_t node_id, WorkerType worker_type, int thread_id, Ctrl_Message * ctrl_message);


#endif
