#ifndef INIT_NET_H
#define INIT_NET_H

#include "common.h"
#include "config.h"
#include "table.h"
#include "self_net.h"
#include "net.h"
#include "join_net.h"


// PRIOR TO INTIALIZING NET, THE NODE NEEDS TO HAVE ALREADY INTIALIZED EVERYTHING ELSE. THIS INCLUDES:
// - Inventory Manager:
//		- Inventory Cache (shared system memory)
//		- Compute Device Mempools (by calling backend memory plugins)
// - Execution Engine: 
//		- Setting up all the device run/wait queues (will be empty)
// - PROBABLY OTHER STUFF...



// SELF NET INITIALIZATION:
// (Do this before requesting to join the net, so a self_net failure is detected before involving others)

// 1.) Creates a self_net which is responsible for calling ib verbs create/alloc functions
//		- Everything is initialized except self_net -> node_id. It will be set in 3a.) 
// 2.) TODO: Bring up all QPs
//		- a.) For all control SRQs, post receive requests.
//			  Control shared receive queues should always have receive requests waiting to be consumed
//				- i.) They will go on a shared receive queue

// CONNECT TO MASTER's JOIN NET SERVER

// 3.) Upon successful connection to the master's join_net server:
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

// CONNECT TO REMOTE RDMA_INIT TCP SERVERS

// Now this node has an array of node id's and ip addresses (really s_addr's == network-ordered uint32_t version of char *ip_addr)

// 4.) For each Node_Ip_Config array, initiate connection to this node's rdma_init TCP server:
//		

// PROCESS TCP CONNECTION FOR RDMA_INIT_CONNECTION (within tcp_rdma_init.c)

// Now a connection has been formed between two worker nodes. 
// Note that the server and client will both call the same function to properly exchange rdam_init info

// 5.) After return from accept()/connect(): 
//		a.) Populate Connection_Data appropriately
//				- i.) Pass this structure to the function process_tcp_connection_for_rdma_init()
//	

// Note: The rdma_init info comes from each worker node's self_net
// 6.) Ordering of data to send (& then recv) within process_tcp_connection_for_rdma_init()
//		- a.) Node ID (uint32)
//				- i.) Now the other side can allocate a Net_Node and eventually insert it into their net_world -> nodes table (step f.) 
//					- The client needs to inform the server who they are
//						- This client didn't exist when the server intially joined the network
//					- The server's node_id message should match the node_id from connecting sides expected Node_Ip_Config)
//		- b.) Number of Devices (uint8) + Number of ports per device (uint8 *) 
//				- i.) Now the other side can allocate the Net_Node -> ports array
//		- c.) For each port send (GID + Port_Num + ID + Pkey + maybe sgid_index, i'm not sure what this means yet.?):
//				- i.) Now the other side can create a Net_Port and also intialize an Address Handle for this remote port (Net_Port -> ah)
//					- All AH are needed because a DATA_REQUEST message chooses Data QPs (from various ports) dynamically
//						- The data QPs chosen will be based on local topology and destination
//						- The other nodes need to be ready (i.e. have an address handle prepared) for any combo of node + device + port_num
//		- d.) Choose a Target Control QP (round-robin assignment) for the other node to send it's control messages to
//				- i.) Now the other side can populate its Net_Node -> ctrl_dest
//		- e.) Declare receives in same order and create the Net_Node
//		- f.) Insert the Net_Node into the Net_World -> nodes table
//		- g.) Send confirmation "true" boolean upon success
//				- i.) If any step in a.) - f.) fails, should send a "false" boolean



// COMPLETITION OF JOIN NET

// TODO: NEED TO WORK ON GETTING THIS DESIGN CLEAN!!!

// Now the Net_World -> nodes table should start to become populated

// 7.) The "min_init_nodes" argument passed into the master and received within join_net indicates 
//		the minimum number of nodes within Net_World -> nodes need to be populated before returning from this "join_net" function
//		- a.) TODO: After returning from this initialization function ca notify the master server
//				- i.) Now the "network's request firewall" will open up this node and it can start to handle requests
//						- Think about this harder, (feels like doesn't make much sense)
//		

// 8.) TODO: The rdma_init TCP server will still be running in the background (there may be future joiners)
//	- Upon every new "join" (detected by an incoming connection request to this node's rdma_init server)
//	  or "leave" (detected by a message from the master) => need to rebalance this node's exchange metadata based on new network_node count
//		- Expect this to be infrequent, but very nice/convienent functionality to have!
//			- At the very beginning there will be a lot of new joiners, thus the num_net_nodes_threshold
//		- Instead of rebalance just using the network_node count, might be better (and more flexible policy) for the master to multi-cast new
//		  exchange fingerprint partitions
//			- In this way the nodes' exchange responsibilities can be partitioned proportionally to their system memory capacities


Net_World * init_net(char * self_ip_addr, char * master_ip_addr);

#endif