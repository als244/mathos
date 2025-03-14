#ifndef MASTER_H
#define MASTER_H

#include "common.h"
#include "config.h"
#include "table.h"

typedef struct master {
	char * ip_addr;
	uint32_t max_nodes;
	uint32_t min_init_nodes;
	pthread_mutex_t id_to_assign_lock;
	// Monotonically increasing and set when doing a new join
	// First acquire lock, read value, and upon successful join, release lock
	// The joins are serialized 
	//	- (can rethink if the performance is too poor for large networks)
	uint32_t id_to_assign;
	// Can be incremented and decremented atomoically with joins/leaves
	// by using sem_post/sem_wait
	// Initialized to max_nodes
	sem_t avail_node_cnt_sem;
	// maintins table of all node_id => ip_mappings
	// Upon join, a node is inserted. Upon leave, a node is removed
	Table * node_configs;
} Master;


Master * init_master(char * ip_addr, uint32_t max_nodes, uint32_t min_init_nodes);

// should ideally never return
// only shutdown message or error
int run_master(Master * master);

#endif