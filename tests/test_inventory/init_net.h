#ifndef INIT_NET_H
#define INIT_NET_H

#include "common.h"
#include "config.h"
#include "self_net.h"
#include "join_net.h"
#include "tcp_rdma_init.h"
#include "cq_handler.h"


// optionally specify an ip_address to use to connect to master and use for running tcp rdma_init server

// using default endpoint types / qp_nums configuration (within self_net.c), could use specify these here...
Net_World * init_net(char * master_ip_addr, char * self_ip_addr);


#endif