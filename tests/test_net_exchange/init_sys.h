#ifndef INIT_SYS_H
#define INIT_SYS_H

#include "exchange.h"
#include "init_net.h"

typedef struct system {
	Exchange * exchange;
	Net_World * net_world;
} System;


System * init_system(char * master_ip_addr, char * self_ip_addr);


#endif