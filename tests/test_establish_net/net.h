#ifndef NET_H
#define NET_H

#include "common.h"
#include "self_net.h"
// REALLY CAN BE USING A NON-LOCKING TABLE HERE...
// SHOULDN'T BE MODIFIED AFTER INTIALIZATION (EXCEPT RE-STARTS)
#include "table.h"
#include "node_ip_config.h"


#define GRH_SIZE_BYTES 40

#define PATH_MTU IBV_MTU_4096

// contains

// Understand sgid_index better if there are multiple devices...?
// Is SGID_INDEX always 0, or need to call ibv_find_gid_index()..?
typedef struct net_endpoint {
	struct ibv_ah * ah; 
	uint32_t remote_qp_num;
	uint32_t remote_qkey;
} Net_Endpoint;

typedef struct net_port {
	// Unique Identifier provided by vendor
	// gid.raw is a 16 byte value (uint8 *)
	// used to create ah
	// also provided in GRH upon receiving a control message (sgid)
	union ibv_gid gid;
	// port num corresponding to a specific gid
	uint8_t port_num;
	// Determined by Subnet Manager (switch)
	// FOR NOW, NOT USING
	uint16_t lid;
	// If we want to have different QOS / Security Partitions
	// FOR NOW, NOT USING
	uint16_t pkey;
	// constructing using gid + port_num + lid
	// Might need to populate ah.sgid_index before sending (depends on sending QP device/port/gid)
	struct ibv_ah * ah;
} Net_Port;


typedef struct net_node {
	uint32_t node_id;
	// Note that on mellnox cards each port is counted as a device
	uint8_t num_devices;
	// Note that on mellanox cards there is typically 1 port per dev, because each dev is actually a port
	uint8_t * num_ports_per_dev;
	// first level index: device_id (corresponds to ibv_context on remote node)
	// second level index: port_num
	//		- Note that on mellanox cards port num start at 1, 
	//		  so size of ports[i] is num_ports_per_dev[i] + 1 and 0th index is empty
	Net_Port ** ports;
	// Every remote node will assign the other side a specfiic QP (for whole node)
	// to use for sending control messages to
	Net_Endpoint ctrl_dest;
	// keeping the ip address here in case disruption in RDMA 
	// network-byte order
	uint32_t ip_addr;
} Net_Node;



typedef struct net_world {
	Self_Net * self_net;
	// mapping from uinit32_t network-ordered ip addr => Node_Ip_Config (from node_ip_config.h)
	// needed when intializating system to exchange RDMA info
	// created based on a configuration file
	Table * ip_to_node;
	// reverse mapping of above. Also created at init
	Table * node_to_ip;
	// mapping from node id => node_net
	// Populated during the system intialization when exchanging info
	// over TCP connections
	Table * nodes;
} Net_World;


// called before exchanging any info. Intializes that nodes table
// That table then gets populated based on tcp exchange
Net_World * init_net_world(Self_Net * self_net, int min_nodes, int max_nodes, char * node_ip_config_filename);


// Used for Data Transmission
// Data Request sends (node_id, port_num, qp_num, q_key)
// Here qp_num and q_key denote a specific Data QP that the receiver acquired exclusive access to

// The destination node_id, port_num are known at intialization and are in table,
// Within the data sending, the qp_num, and q_key from request are used not the ctrl qp values
struct ibv_ah * get_dest_ah_from_request(Net_World * net_world, uint32_t dest_node_id, uint8_t dest_device_id, uint8_t dest_port_num);

int get_ctrl_endpoint(Net_World * net_world, uint32_t dest_node_id, Net_Endpoint * ret_net_endpoint);

#endif