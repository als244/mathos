#ifndef SYS_H
#define SYS_H

#include "common.h"
#include "config.h"
#include "exchange.h"
#include "init_net.h"

#include "work_pool.h"
#include "exchange_worker.h"
#include "inventory_worker.h"


typedef struct system {
	Work_Pool * work_pool;
	Exchange * exchange;
	Net_World * net_world;
	// contains semaphores that the calling thread 
	// should wait on before ready the results of the 
	// throughput benchmark;
	Deque * are_benchmarks_ready;
} System;


System * init_system(char * master_ip_addr, char * self_ip_addr);

int add_message_class_benchmark(System * system, CtrlMessageClass message_class, uint64_t start_message_cnt, uint64_t end_message_cnt);

int start_system(System * system);


#endif