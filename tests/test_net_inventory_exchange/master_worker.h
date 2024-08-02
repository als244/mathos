#ifndef MASTER_WORKER_H
#define MASTER_WORKER_H

#include "common.h"
#include "exchange.h"
#include "net.h"
#include "work_pool.h"


// this will be the value within worker_thread_data -> worker_arg
typedef struct master_worker_data {
	Net_World * net_world;
} Master_Worker_Data;


void * run_master_worker(void * _worker_thread_data);

#endif