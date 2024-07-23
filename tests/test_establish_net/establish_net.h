#ifndef ESTABLISH_NET_H
#define ESTABLISH_NET_H

#include "common.h"
#include "self_net.h"
#include "net.h"
#include "node_ip_config.h"

#define ESTABLISH_NET_PORT 9272

typedef struct server_thread_data {
	Net_World * net_world;
	uint32_t max_accepts;
	uint32_t num_accepts_processed;
} Server_Thread_Data;

typedef struct client_thread_data {
	Net_World * net_world;
	uint32_t num_connects;
	uint32_t num_connects_processed;
} Client_Thread_Data;

void * run_tcp_server_for_rdma_init();

#endif