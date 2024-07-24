#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"
#include "table.h"

typedef struct node_ip_config {
	uint32_t node_id;
	// equivalent to inet_addr(char * ip_addr)
	// s_addr is in network-byte order (not host order)
	uint32_t s_addr;
} Node_Ip_Config;


/* NOT NEEDED...?

// Created After Successfully Joining Network
// This is a mapping of uint32_t node_id => uint32_t s_addr (network byte ordering)
// The items in this table are of type Node_Ip_Config
// The entries in this tabled are generated based upon response from master upon successful connection
// The table only contains node's that this node will connect to in order to establish connection to exchange rdma_init info
// All nodes in this table should have id < self_id, where self_id was assigned by the master server upon join
typedef struct server_nodes_config {
	Table * node_to_ip;
} Server_Nodes_Config;

*/



#endif


	