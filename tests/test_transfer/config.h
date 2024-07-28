#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"
#include "messages.h"

#define MASTER_NODE_ID 0

// used within:
//	- Master's run_join_net_server()
//	- Worker's join_net -> connect_to_master()
#define JOIN_NET_PORT 9123
// set timeout to be 100 ms for a worker to try joining net again
#define JOIN_NET_TIMEOUT_MICROS 100000

// used within:
//	- init_master starting its rdma_init server
//	- within processing join_net on the worker side to connect to
#define MASTER_RDMA_INIT_PORT 9184

//  used within:
//	- Worker node's creation of RDMA_INIT tcp server within processing join_net
//	- Worker node's connecting to other worker RDMA_INIT server within init_net function
#define WORKER_RDMA_INIT_PORT 9272
// set timeout to be 100 ms for a worker to try to connect to other rdma_init server again
#define RDMA_INIT_TIMEOUT_MICROS 100000


// NOTE: THE "MASTER" PROCESS AND ALL WORKERS NEED TO AGREE ON THIS CONFIG!




#endif


	