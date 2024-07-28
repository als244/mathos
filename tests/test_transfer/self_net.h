#ifndef SELF_NET_H
#define SELF_NET_H

#include "common.h"
#include "config.h"
#include "ctrl_channel.h"
#include "deque.h"

#define GID_NUM_BYTES 16

typedef struct self_port Self_Port;

typedef struct self_endpoint {
	// This will be send to the other side so they can refer to this endpoint easily
	// It is the index within self_node -> endpoints
	uint32_t node_endpoint_ind;
	// back-reference the port this QP was created on
	// can look up port details easily this way
	// if we have a pool of struct QPs can figure things out
	Self_Port * qp_port;
	EndpointType endpoint_type;
	// only used within self_net => null for others
	// SRQ might be null depending on endpoint_type
	struct ibv_qp * ibv_qp;
	// found with ibv_query_qp()
	// For CONTORL_QP and MASTER_QP: 
	//	- Send these values (along with port_num, gid, & lid)
	// 		during rdma_init phase of initialization (send over TCP connection)
	// FOR DATA_QP:
	//	- After acquiring exclusive access to this QP for an inbound transfer, 
	//		send these values 
	//		(along with log_port_ind, which is a logical index to a port so that sender can determine correct AH)
	uint32_t qkey;
	uint32_t qp_num;
	// what we will be using as an abstraction for posting to / receiving from that will keep track of
	// only Valid on endpoints with type CONTROL_ENDPOINT
	Ctrl_Channel * send_ctrl_channel;
	// if specified to use SRQ for control endpoint, then this receive channel points to the shared_recv_channel
	// from self_net -> dev_shared_recv_ctrl_channels
	Ctrl_Channel * recv_ctrl_channel;
} Self_Endpoint;

// Idea is to keep a Small set of total QPs (~ 1 per CPU-core)
// and then have remote ports load balanced across these
// But also want to seperate Control Messages (fixed size + system RAM)
// From Data Messages (variable size + variable memory location)

// Note: For UD queue pairs, 1-way RDMA-writes/reads are not supported
//			- using RC queue pairs is not scalable as the number of QPs in system is quadratic 
//			- using excessive QPs means NIC cache misses and significant performance degredation
//			- So instead, we will strictly be using UD transports
//				- A different, but likely worse idea (though simpler to implement and "more reliable"):
//						- It possible to have a system with combination of RC/UD
//						- A feasible solution could be a system with O(N log N) queue pairs
//							- Where N queue pairs serve as UD and are dedicated for low-latency, point-to-point 
//								control messages within entire system
//							- Where log N queue pairs serve as RC and are dedicated for handling Data shipments
//								and RDMA operations are forwarded along a path of log N intermediaries
//									- 
//				
// Thus we must have protocol to ensure that the receiver can prepare
// memory (at a specific location in system) corresponding to a specific set of
// send messages (all the data packets corresponding to an inbound-transfer associated
// with a specific object fingerprint)


struct self_port {
	// index within the self_node ports array
	// this is the value sent during data transfer initiation
	// the other side will have a copy of the same array and will use 
	// this node_port_ind to look up the appropriate Address Handle 
	uint32_t node_port_ind;
	// the device that this port is associated with
	// refers to index within network ibv_dev_ctxs
	int ib_device_id;
	// the port_num indicies start at 1
	// ib_port_num for a specific device
	uint8_t port_num;
	// populated with ibv_query_gid()
	// NEEDED FOR configuration of world_net 
	// to create Address Handle's to remote ports
	// index 0 containrs the port GID
	// It is 16 bytes, where the high order 8 bytes refer to subnet and lower 8 bytes refer to vendor suplied GUID
	union ibv_gid gid;
	// The below 6 fields are found using ibv_query_port()
	// a field within port_attr,
	// but needed for configuring world_net
	// so just storing as extra field for clarity
	// Subnet Manager (in InfiniBand) assigns lid
	uint16_t lid;
	// Indicates if port is active or down
	// Still intitialize QPs on down state, but make sure to not send these out as destination control QPs
	// or use them to receive inbound data transfers
	enum ibv_port_state state;
	// a field within port_attr
	enum ibv_mtu active_mtu;
	// a field within port_attr,
	// but keeping here for convenience
	// use port_attr active_speed_ex, but if active_speed_ex is 0, then use port_attr uint8_t active_speed
	uint32_t active_speed;
	// needed for creating a multicast group
	uint16_t sm_lid;
	uint8_t sm_sl;
	// populated iwth ibv_query_pkey()
	// only needed if doing fancy partitioning for QoS or security
	uint16_t pkey;
};

typedef struct self_node {
	uint32_t num_ports;
	uint32_t num_endpoints;
	// all devices' ports are packed together for easy lookup / transmission
	Self_Port * ports;
	// all of the nodes' endpoints (QPs) are packed together
	Self_Endpoint * endpoints;
	// to maintin a list of all available endpoints to send control messsages from 
	Deque * active_ctrl_endpoints;
} Self_Node;

typedef struct self_net {
	int num_endpoint_types;
	EndpointType * endpoint_types;
	int num_ib_devices;
	// result of call from ibv_get_device_list()
	struct ibv_device ** ib_devices;
	// every device has their own ibv_context
	// result of call from ibv_open_device()
	struct ibv_context ** ibv_dev_ctxs;
	// have one pd per rdma device
	// created with ib_alloc_pd()
	struct ibv_pd ** dev_pds;
	// number of physical ports per device
	int * num_ports_per_dev;
	// Create SRQs for each device that might be used
	// for QPs within a port depending on QP usage type
	struct ibv_srq ** dev_srqs;
	Ctrl_Channel ** dev_shared_recv_ctrl_channels;
	// outer index is device_id
	// inner index is endpoint type
	struct ibv_cq_ex *** cq_collection;
	// outer index is device_id
	// inner index is endpoint type
	// pthread_create is not called until the end of init_net
	pthread_t ** cq_threads;
	// the cpu_set assoicated with the device
	// found by reading the local_cpus file from sysfs
	// this file has comma seperate uint32_t's representing cpu bitmasks
	// the completition queue handler threads (assoicated with each device)
	// should have these bitmasks set when the thread is spawned
	// using pthread_setaffinity_np() 
	cpu_set_t ** ib_dev_cpu_affinities;
	// where all the QPs are stored
	Self_Node * self_node;
	// If ip_addr is non-null will use this ip_address to connect to master port
	// Otherwise, use system default
	// Whichever ip address is used to connect to master server will 
	// be the ip address used for this node's rdma_init tcp server
	char * ip_addr;
} Self_Net;


// allocates structures and populates self_net
Self_Net * init_self_net(int num_endpoint_types, EndpointType * endpoint_types, bool * to_use_srq_by_type, int * num_qps_per_type, char * self_ip_addr);


// populating default values arguments to init_self_net
Self_Net * default_worker_config_init_self_net(char * self_ip_addr);

Self_Net * default_master_config_init_self_net(char * self_ip_addr);

// this is called from within the control handler threads
// it sees a work completition and needs to extract it
// and do something with it (pass it off to other worker threads)
// (e.g. exchange workers, sched workers, config workers, etc.)
Ctrl_Channel * get_ctrl_channel(Self_Net * self_net, uint64_t wr_id);


#endif