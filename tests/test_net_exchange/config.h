#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"
#include "messages.h"


// NOTE: THE "MASTER" PROCESS AND ALL WORKERS NEED TO AGREE ON THIS CONFIG!

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


// SELF_NET CONFIGURATION


// BID TODO: fix this configuration of queue sizes to be more flexible!!!

#define SRQ_MAX_WR 1U << 14
#define SRQ_MAX_SGE 2

#define QP_MAX_SEND_WR 1U << 10
#define QP_MAX_SEND_SGE 2

#define QP_MAX_RECV_WR 1U << 8
#define QP_MAX_RECV_SGE 2

#define QP_MAX_INLINE_DATA 0 // SEEMS LIKE 956 is the max for qp creation error...?


// CONTROL 

#define RECV_CTRL_MAX_POLL_ENTRIES 1U << 14
#define SEND_CTRL_MAX_POLL_ENTRIES 1U << 10

#define CTRL_RECV_DISPATCHER_BACKLOG_MESSAGES 1U << 21


// MASTER CLASS CONFIGURATION

#define NUM_MASTER_WORKER_THREADS 1
#define MASTER_WORKER_MAX_TASKS_BACKLOG 4096



// EXCHANGE CLASS CONFIGURATION

#define NUM_EXCHANGE_WORKER_THREADS 16
#define EXCHANGE_WORKER_MAX_TASKS_BACKLOG 1U << 16


// EXCHANGE CONFIGURATION

#define EXCHANGE_MIN_BID_TABLE_ITEMS 1UL << 6
#define EXCHANGE_MAX_BID_TABLE_ITEMS 1UL << 20

#define EXCHANGE_MIN_OFFER_TABLE_ITEMS 1UL << 6
#define EXCHANGE_MAX_OFFER_TABLE_ITEMS 1UL << 30

#define EXCHANGE_MIN_FUTURE_TABLE_ITEMS 1UL << 6
#define EXCHANGE_MAX_FUTURE_TABLE_ITEMS 1UL << 20


// the load factor and shrink factor only matter if min_size != max_size
#define EXCHANGE_TABLES_LOAD_FACTOR 0.5f
#define EXCHANGE_TABLES_SHRINK_FACTOR 0.1f



// INVENTORY CLASS CONFIGURATION


// INVENTORY CONFIGURATION

#define MAX_FINGERPRINT_MATCH_LOCATIONS 24


#endif


	