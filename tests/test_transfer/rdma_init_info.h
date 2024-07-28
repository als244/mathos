#ifndef RDMA_INIT_INFO_H
#define RDMA_INIT_INFO_H

#include "common.h"
#include "config.h"
#include "self_net.h"

// information needed in order to create an address handle
// 1 per port
typedef struct ah_creation_data {
	// the gid's refer to sp
	union ibv_gid gid;
	uint16_t lid;
	// this is the port_num associated with a specifc gid/ibv_context
	uint8_t port_num;
} Ah_Creation_Data;

/*
struct ibv_ah_attr ah_attr;
	memset(&ah_attr, 0, sizeof(ah_attr));

	ah_attr.grh.dgid = remote_gid;
	ah_attr.grh.sgid_index = 0;
	ah_attr.is_global = 1;
	ah_attr.dlid = remote_lid;
	ah_attr.port_num = remote_port_num   ;

	struct ibv_pd * dev_pd = (self_net -> dev_pds)[device_ind];
	struct ibv_ah *ah = ibv_create_ah(dev_pd, &ah_attr);
	if (ah == NULL){
		fprintf(stderr, "Error: could not create address handle\n");
		return -1;
	}
*/


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
	// the the index within rdma_init_info -> remote_port_init
	uint32_t remote_node_port_ind;
	uint32_t remote_qp_num;
	uint32_t remote_qkey;
} Remote_Endpoint;

typedef struct rdma_init_info_h {
	uint32_t node_id;
	uint32_t num_ports;
	uint32_t num_endpoints;
} Rdma_Init_Info_H;

typedef struct rdma_init_info {
	Rdma_Init_Info_H header;
	Remote_Port_Init * remote_ports_init;
	// For Non-Data endpoints:
	//	- it is the sender's responsiblity to properly
	// 		choose the endpoint 
	// 	(if there are multiple options for each endpoint type) 
	// based on some scheme
	// For Data-Endpoints:
	//	- the receiver will tell the sender which endpoint ind
	Remote_Endpoint * remote_endpoints;
} Rdma_Init_Info;


Rdma_Init_Info * build_rdma_init_info(Self_Net * self_net, uint32_t node_id);


#endif