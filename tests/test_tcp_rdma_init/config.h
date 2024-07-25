#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

// used within:
//	- Master's run_join_net_server()
//	- Worker's join_net -> connect_to_master()
#define JOIN_NET_PORT 9123
// set timeout to be 1 sec for a worker to try joining net again
#define JOIN_NET_TIMEOUT_MICROS 1000000

//  used within:
//	- Worker node's RDMA_INIT tcp server
//	- Worker node's connecting to other worker RDMA_INIT server
#define INTERNAL_RDMA_INIT_PORT 9272

// NOTE: THE "MASTER" PROCESS AND ALL WORKERS NEED TO AGREE ON THIS CONFIG!


typedef enum enpoint_type {
	CONTROL_ENDPOINT,
	DATA_ENDPOINT,
	MASTER_ENDPOINT,
	ALERT_MULTICAST_ENDPOINT
} EndpointType;

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


	