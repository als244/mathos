#ifndef TCP_RDMA_INIT_H
#define TCP_RDMA_INIT_H

#include "common.h"
#include "config.h"
#include "self_net.h"
#include "rdma_init_info.h"
#include "net.h"
#include "tcp_connection.h"


// Started in a thread at the end of processing join_request
void * run_tcp_rdma_init_server(void * _net_world);


// After successful join request, the node will a receive an array of all Node_Config
// with ip addresses that it should connect to. It will call this function for each of these
int connect_to_rdma_init_server(Net_World * net_world, char * rdma_init_server_ip_addr);

#endif