#ifndef JOIN_NET_H
#define JOIN_NET_H

#include "common.h"
#include "config.h"
#include "self_net.h"
#include "rdma_init_info.h"
#include "net.h"
#include "tcp_connection.h"
#include "tcp_rdma_init.h"


// CONNECT TO MASTER's JOIN NET SERVER

// Upon successful connection to the master's join_net server:
//		- a.) The master will assign and send this node an id (uint32_t):
//				- i.) Now this node should set its id within self_net
//		- b.) The master will send the maximum node capacity (uint32_t) of network (which was set as argument when starting the master)
//				- i.) Now this node can initialize net_world because it has its id & maximum capacity of network
//				- ii.) MAYBE? After intializing net_world, this node should create a thread and start its rdma_init TCP server
//						- Doing this step here (instead of after e.), because in case of failure of starting TCP server we want to alert master of failure and exit
//						- FOR NOW: doing this after step 3 completes
//		- c.) The master will send the minimum number of node 'connections' required to "come online" (i.e. the number of Net_Nodes in Net_World -> nodes table):
//				- i.) This is needed for step 7.)
//		- d.) The master will report the number of nodes (uint32_t) that are currently in network
//				- i.) Now this node should allocate an array of this size where the items are of type Node_Ip_Config
//				- ii.) This array will be used in step 7.)
//		- e.) For each node currently in the network the master will send Node_Ip_Config data:
//				- i.) This node should receive this data in the array allocated in 3.c.i
//		- f.) Send a confirmation "true" boolean to master Once all Node_Ip_Config data has been received
//				- i.) This ensures that the server will block and helps with debugging/readability


// Called within step 2 of init_net()
int join_net(Self_Net * self_net, char * master_ip_addr, Join_Response ** ret_join_response, Net_World ** ret_net_world);

#endif