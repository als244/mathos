#ifndef SETUP_NET_H
#define SETUP_NET_H

#include "common.h"

typedef enum queue_pair_usage_type {
	CONTROL_QP,
	DATA_QP
} QueuePairUsageType;

typedef enum completion_queue_usage_type {
	CONTROL_CQ,
	DATA_CQ
} CompletionQueueUsageType;

typedef struct qp {
	// this is the first-level index into qp_collection -> [send_cqs|recv_cqs] arrays
	// keeping this here for bookkeeping/debuggability
	QueuePairUsageType qp_usage_type;
	// only used within self_net => null for others
	// SRQ might be null depending on qp_usage_type
	struct ibv_qp * ibv_qp;
	// used as part of world_net to send messages
	// found with ibv_query_qp()
	uint32_t port_num;
	uint32_t qkey;
	uint32_t qp_num;
	// Might also want to pkey_index...? 
	// only needed if partitions and subnet manager handles..?
} QP;

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


typedef struct qp_collection {
	// keeping track of device_id for debugging (a bit redundant)
	int device_id;
	int num_qp_types;
	QueuePairUsageType * qp_usage_types;
	bool * to_use_srq_by_type;
	int * num_qps_per_type;
	// first level: by type
	// second level: number qp within type
	// third level: pointer to QP
	QP *** queue_pairs;
} QP_Collection;


typedef struct cq {
	// this is the index into cq_collection -> [send_cqs|recv_cqs] arrays
	// keeping this here for bookkeeping/debuggability
	CompletionQueueUsageType cq_usage_type;
	struct ibv_cq_ex * ibv_cq;
} CQ;


// We will have a send + receive completition queue for each qp type
// They are shared for all QPs within a device context
// (i.e. all Send Queues of type 0 within all physical ports of device 0 will share)
typedef struct cq_collection {
	// keeping track of device_id for debugging (a bit redundant)
	int device_id;
	// for now, the same as num_qp_types
	int num_cq_types;
	CompletionQueueUsageType * cq_usage_types;
	CQ ** send_cqs;
	CQ ** recv_cqs;
} CQ_Collection;


typedef struct port {
	// the device that this port is associated with
	// refers to index within network ibv_dev_ctxs
	int device_id;
	uint8_t port_num;
	// populated with ibv_query_port()
	struct ibv_port_attr port_attr;
	// a field within port_attr, 
	// but needed for configuring world_net
	// so just storing as extra field for clarity
	uint16_t lid;
	
	// MIGHT NEED THESE FOR WORLD_NET_CONFIGURATION
	// But subnet manager might already take care of them...?
	/*
	// populated with ibv_query_gid()
	// NEEDED FOr configuration of world_net 
	// to create Address Handle's to remote ports
	union ibv_gid gid;
	// populated iwth ibv_query_pkey()
	uint16_t pkey;
	*/
	// used to break up set of qps into types
	QP_Collection * qp_collection;
	// only used within world_net
	// needed when sending to any QP part of this port's collection
	struct ibv_ah * ibv_ah;
} Port;

typedef struct node_net {
	int node_id;
	int num_ib_devices;
	int * num_ports_per_dev;
	// first level: number of devices
	// second level: number of ports for that device
	// third level: pointer to port
	Port *** ports;
} Node_Net;

typedef struct self_net {
	int node_id;
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
	// Create CQ collections for each device
	CQ_Collection ** dev_cq_collections;
	// Create SRQs for each device that might be used
	// for QPs within a port depending on QP usage type
	struct ibv_srq ** dev_srqs;
	// Keep some accounting numbers around to help with assinging source qp nums
	int total_ports;
	int total_qps_per_port;
	int total_qps_node;
	uint32_t start_qpn;
	uint32_t cur_qpn;
	// where all the QPs are stored
	Node_Net * self_node;
} Self_Net;

typedef struct world_net {
	int num_others;
	Node_Net ** remote_nodes;
} World_Net;

typedef struct network {
	Self_Net * self_net;
	World_Net * world_net;
} Network;


// allocates structures and populates self_net
Network * init_net(int self_id, int num_qp_types, QueuePairUsageType * qp_usage_types, bool * to_use_srq_by_type, int * num_qps_per_type, 
						int num_cq_types, CompletionQueueUsageType * cq_usage_types);

// called when all nodes in system have initialized self-net
// and values for qkey's, qnums, gids, lids can be populated
int setup_world_net(Network * net, int num_others);

#endif