#ifndef INIT_SYS_H
#define INIT_SYS_H

#include "common.h"
#include "config.h"
#include "exchange.h"
#include "init_net.h"

#include "work_pool.h"
#include "exchange_worker.h"


typedef struct system {
	Work_Pool * work_pool;
	Exchange * exchange;
	Net_World * net_world;
} System;


System * init_system(char * master_ip_addr, char * self_ip_addr);


#endif