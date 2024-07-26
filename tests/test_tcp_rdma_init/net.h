#ifndef NET_H
#define NET_H

#include "common.h"
#include "config.h"
#include "table.h"
#include "self_net.h"
#include "rdma_init_info.h"


#define GRH_SIZE_BYTES 40

// need an endpoint for every source port
// => ah is created per context
typedef struct net_endpoint {
	EndpointType endpoint_type;
	uint32_t remote_qp_num;
	uint32_t remote_qkey;
	// this is is the index within net_node -> ports
	// in order to obtain the correct struct ibv_ah * (needed for sending UD)
	// then based on the sender's device choose an ah within this ports address_handles
	// (i.e. to send to this endpoint from self_device_id = j, then do:
	//	net_node -> ports[remote_node_port_ind] -> address_handles[j]
	uint32_t remote_node_port_ind;
} Net_Endpoint;

typedef struct net_port {
	// Unique Identifier provided by vendor
	// gid.raw is a 16 byte value (uint8 *)
	// used to create ah
	// also provided in GRH upon receiving a control message (sgid)
	union ibv_gid gid;
	// Determined by Subnet Manager (switch)
	// FOR NOW, NOT USING
	uint16_t lid;
	// port num corresponding to a specific gid
	uint8_t port_num;
	// Need to know if port is active
	enum ibv_port_state state;
	// other port specs that the other side might care about
	enum ibv_mtu active_mtu;
	uint32_t active_speed;
	// constructing using gid + port_num + lid
	// we need to create an address handle to this remote port for every one of our ibv_contexts
	// want to 
	// indexed by self_net -> self_port -> ib_device_id
	struct ibv_ah ** address_handles;
} Net_Port;


typedef struct net_node {
	// Assigned by master join_net server
	uint32_t node_id;
	// Note that on mellnox cards each port is counted as a device
	uint32_t num_ports;
	// Note that on mellanox cards there is typically 1 port per dev, because each dev is actually a port
	uint32_t num_endpoints;
	Net_Port * ports;
	// Every remote node will assign the other side all of their QPs
	// For Non-Data endpoints:
	//	- it is the sender's responsiblity to properly
	// 		choose the endpoint 
	// 	(if there are multiple options for each endpoint type) 
	// based on some scheme
	// For Data-Endpoints:
	//	- the receiver will tell the sender which endpoint ind
	Net_Endpoint * endpoints;
} Net_Node;



typedef struct net_world {
	// Can be allocated before success joining except for self_net -> node_id
	Self_Net * self_net;
	// the values received from master during join request
	uint32_t self_node_id;
	uint32_t max_nodes;
	uint32_t min_init_nodes;
	// This corresponds to the ip address that the worker connected to master with
	// If the worker specified its ip address upon initialiation it will be the same value
	char * self_rdma_init_server_ip_addr;
	// what this node will share with others during tcp rdma_init connection
	Rdma_Init_Info * self_rdma_init_info;
	// the thread that is running this node's rdma_init server
	//		- always running
	pthread_t tcp_rdma_init_server_thread;
	// mapping from node id => node_net
	// Populated from either this node's rdma_init tcp server
	// or when it assumes client role and connects to a remote rdma_init tcp server (i.e. all the nodes in server_nodes_to_ip)
	Table * nodes;
} Net_World;


// after successful join request from master
// initialize this with the known max_nodes and assigned node_id
Net_World * init_net_world(Self_Net * self_net, uint32_t node_id, uint32_t max_nodes, uint32_t min_init_nodes, char * self_rdma_init_server_ip_addr);

void destroy_net_world(Net_World * net_world);

// this is called during the processing of a tcp_rdma_init connection (tcp_rdma_init.c)
// it allocates memory for a node, and populates ports and enpoints based on the received rdma_init_info
// for each port creates address handle for all of the self_ib_devices so that we can send to this remote port from any of our ports easily
Net_Node * net_add_node(Net_World * net_world, Rdma_Init_Info * remote_rdma_init_info);


// Destroys all address handles, frees all memory associated with node, and removes from net_world -> nodes table
void destroy_remote_node(Net_World * net_world, Net_Node * node);


// Used for Data Transmission
// Data Request sends (node_id, port_num, qp_num, q_key)
// Here qp_num and q_key denote a specific Data QP that the receiver acquired exclusive access to

// The destination node_id, port_num are known at intialization and are in table,
// Within the data sending, the qp_num, and q_key from request are used not the ctrl qp values
struct ibv_ah * get_dest_ah_from_request(Net_World * net_world, uint32_t source_node_port_ind, uint32_t dest_node_id, uint32_t dest_node_endpoint_num);


int get_ctrl_endpoint(Net_World * net_world, uint32_t dest_node_id, Net_Endpoint * ret_net_endpoint);

#endif