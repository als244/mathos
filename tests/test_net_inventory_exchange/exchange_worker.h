#ifndef EXCHANGE_WORKER_H
#define EXCHANGE_WORKER_H

#include "common.h"
#include "utils.h"
#include "fifo.h"
#include "exchange.h"
#include "net.h"
#include "work_pool.h"
#include "inventory.h"


// this will be the value within worker_thread_data -> worker_arg
typedef struct exchange_worker_data {
	Exchange * exchange;
	Net_World * net_world;
} Exchange_Worker_Data;


void * run_exchange_worker(void * _worker_thread_data);

#endif