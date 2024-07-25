#ifndef TCP_RDMA_INIT_H
#define TCP_RDMA_INIT_H

#include "common.h"
#include "config.h"

typedef enum enpoint_type {
	CONTROL_ENDPOINT,
	DATA_ENDPOINT,
	MASTER_ENDPOINT,
	ALERT_MULTICAST_ENDPOINT
} EndpointType;

// information needed in order to create an address handle
// 1 per port
typedef struct ah_creation_data {
	// the gid's refer to sp
	union ibv_gid gid;
	uint16_t lid;
	// this is the port_num associated with a specifc gid/ibv_context
	uint8_t port_num;
} Ah_Creation_Data;



typedef struct remote_port_init {
	Ah_Creation_Data ah_creation_data;
	// needed for other side know which endpoints are available
	// (i.e. which control endpoints are reachable)
	enum ibv_port_state state;
	// other port specs that the other side might care about
	enum ibv_mtu active_mtu;
	uint32_t active_speed;
} Remote_Port_Init;
	

typedef struct remote_endpoint {
	EndpointType endpoint_type;
	// this is the port index assoicated 
	// the the index within tcp_rdma_init_info -> remote_port_init
	uint32_t logical_port_ind;
	uint32_t remote_qp_num;
	uint32_t remote_qkey;
} Remote_Endpoint;

typedef struct tcp_rdma_init_info_h {
	uint32_t num_ports;
	uint32_t num_endpoints;
} Tcp_Rdma_Init_Info_H;

typedef struct tcp_rdma_init_info {
	Remote_Port_Init * remote_port_init;
	// For Non-Data endpoints:
	//	- it is the sender's responsiblity to properly
	// 		choose the endpoint 
	// 	(if there are multiple options for each endpoint type) 
	// based on some scheme
	// For Data-Endpoints:
	//	- the receiver will tell the sender which endpoint ind
	Remote_Endpoint * remote_endpoints;
} Tcp_Rdma_Init_Info;


#endif