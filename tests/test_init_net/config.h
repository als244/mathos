#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

// used within:
//	- Master's run_join_net_server()
//	- Worker's join_net -> connect_to_master()
#define JOIN_NET_PORT 9123
// set timeout to be 10ms for a worker to try joining net again
#define JOIN_NET_TIMEOUT_MICROS 10000

//  used within:
//	- Worker node's RDMA_INIT tcp server
//	- Worker node's connecting to other worker RDMA_INIT server
#define INTERNAL_RDMA_INIT_PORT 9272

// NOTE: THE "MASTER" PROCESS AND ALL WORKERS NEED TO AGREE ON THIS CONFIG!

typedef struct node_config {
	uint32_t node_id;
	// equivalent to inet_addr(char * ip_addr)
	// s_addr is in network-byte order (not host order)
	uint32_t s_addr;
} Node_Config;

// Header for join response
typedef struct join_response_h {
	uint32_t node_id;
	uint32_t max_nodes;
	uint32_t min_init_nodes;
	uint32_t node_cnt;
} Join_Response_H;

// The header is sent/recv first, followed by the node_config_arr
typedef struct join_response {
	Join_Response_H header;
	// will be of size node_cnt sent in the header
	Node_Config * node_config_arr;
} Join_Response;



#endif


	