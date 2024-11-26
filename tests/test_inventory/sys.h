#ifndef SYS_H
#define SYS_H

#include "common.h"
#include "config.h"
#include "exchange.h"
#include "inventory.h"
#include "init_net.h"

#include "work_pool.h"
#include "exchange_worker.h"
#include "inventory_worker.h"

#include "memory.h"
#include "memory_server.h"

// JUST FOR NOW WHILE INTERFACE IS UNDERWAY...
#include "hsa_memory.h"


typedef struct system {
	Work_Pool * work_pool;
	Exchange * exchange;
	Inventory * inventory;
	Net_World * net_world;
	// contains semaphores that the calling thread 
	// should wait on before ready the results of the 
	// throughput benchmark;
	Deque * are_benchmarks_ready;
	Memory * memory;
	pthread_t memory_server;
} System;


System * init_system(char * master_ip_addr, char * self_ip_addr, uint64_t sys_mem_usage, uint64_t sys_mem_chunk_size, uint64_t dev_mem_usage, uint64_t dev_mem_chunk_size);

int add_message_class_benchmark(System * system, CtrlMessageClass message_class, uint64_t start_message_cnt, uint64_t end_message_cnt);

int start_system(System * system);


#endif