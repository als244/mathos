#include "rdma_init_info.h"

Rdma_Init_Info * build_rdma_init_info(Self_Net * self_net, uint32_t node_id) {

	Rdma_Init_Info * rdma_init_info = (Rdma_Init_Info *) malloc(sizeof(Rdma_Init_Info));
	if (rdma_init_info == NULL){
		fprintf(stderr, "Error: malloc failed to allocate rdma_init_info\n");
		return NULL;
	}

	Self_Node * self_node = self_net -> self_node;

	uint32_t num_ports = self_node -> num_ports;
	uint32_t num_endpoints = self_node -> num_endpoints;

	// 1.) Set the header info so the receiver can allocate the correct amount of memory
	//		to receiver the arrays sent after

	rdma_init_info -> header.node_id = node_id;
	rdma_init_info -> header.num_ports = num_ports;
	rdma_init_info -> header.num_endpoints = num_endpoints;

	printf("Buiding rdma info: Node Id: %u, Num Ports: %u, Num Endpoints: %u\n", node_id, num_ports, num_endpoints);

	// 2.) Need to share port information for other side to create address handles and know other important attributes

	Remote_Port_Init * remote_ports_init = (Remote_Port_Init *) malloc(num_ports * sizeof(Remote_Port_Init));
	if (remote_ports_init == NULL){
		fprintf(stderr, "Error: malloc failed to allocate remote_ports_init arr\n");
		return NULL;
	}

	Self_Port * ports = self_node -> ports;
	Self_Port * cur_port;
	for (uint32_t i = 0; i < num_ports; i++){
		cur_port = &(ports[i]);

		// Set the data needed for other side to create address handle to this port
		remote_ports_init[i].ah_creation_data.gid = cur_port -> gid;
		remote_ports_init[i].ah_creation_data.lid = cur_port -> lid;
		remote_ports_init[i].ah_creation_data.port_num = cur_port -> port_num;

		// indicate if this port is available by sending it's state
		// only availabe if state == IBV_PORT_ACTIVE
		remote_ports_init[i].state = cur_port -> state;

		// indicate the mtu in case this port's mtu differs with current one
		// in which case use the minimum of the two
		remote_ports_init[i].active_mtu = cur_port -> active_mtu;
	
		// indicate the bandwidth of the port so the sender can choose an approriately 
		// BW-provisioned port of their own to send to 
		// (i.e. don't waste high BW port as sender if this port has lower BW)
		remote_ports_init[i].active_speed = cur_port -> active_speed;
	}

	rdma_init_info -> remote_ports_init = remote_ports_init;

	// 3.) Need to share endpoint (QP) information for other side to specify what the destination QP info should be

	Remote_Endpoint * remote_endpoints = (Remote_Endpoint *) malloc(num_endpoints * sizeof(Remote_Endpoint));
	if (remote_endpoints == NULL){
		fprintf(stderr, "Error: malloc failed to allocate remote_endpoints arr\n");
		return NULL;
	}

	Self_Endpoint * endpoints = self_node -> endpoints;
	Self_Endpoint * cur_endpoint;
	for (uint32_t i = 0; i < num_endpoints; i++){
		cur_endpoint = &(endpoints[i]);
		remote_endpoints[i].endpoint_type = cur_endpoint -> endpoint_type;
		remote_endpoints[i].remote_node_port_ind = cur_endpoint -> qp_port -> node_port_ind;
		printf("Adding remote endpoint with id: %u. Assigned remote_node_port_ind: %u\n", i, remote_endpoints[i].remote_node_port_ind);
		remote_endpoints[i].remote_qp_num = cur_endpoint -> qp_num;
		remote_endpoints[i].remote_qkey = cur_endpoint -> qkey;
	}

	rdma_init_info -> remote_endpoints = remote_endpoints;

	return rdma_init_info;
}
