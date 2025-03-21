#include "net.h"


// Generic Table Structure expects uint64_t values
uint64_t net_node_hash_func(void * net_node, uint64_t table_size) {
	uint32_t key = ((Net_Node *) net_node) -> node_id;
	// Take from "https://gist.github.com/badboy/6267743"
	// Credit: Robert Jenkins
	key = (key+0x7ed55d16) + (key<<12);
   	key = (key^0xc761c23c) ^ (key>>19);
   	key = (key+0x165667b1) + (key<<5);
   	key = (key+0xd3a2646c) ^ (key<<9);
   	key = (key+0xfd7046c5) + (key<<3);
   	key = (key^0xb55a4f09) ^ (key>>16);
	return (uint64_t) key % table_size;
}

int net_node_cmp(void * net_node, void * other_net_node) {
	uint32_t id_a = ((Net_Node *) net_node) -> node_id;
	uint32_t id_b = ((Net_Node *) other_net_node) -> node_id;
	return id_a - id_b;
}


// after successful join request from master, but before connecting to any other workers
// happens during processing of join request
// initialize this with the known max_nodes and assigned node_id
Net_World * init_net_world(Self_Net * self_net, uint32_t node_id, uint32_t max_nodes, uint32_t min_init_nodes, char * self_rdma_init_server_ip_addr){

	int ret; 

	Net_World * net_world = (Net_World *) malloc(sizeof(Net_World));
	if (net_world == NULL){
		fprintf(stderr, "Error: malloc failed allocating world net\n");
		return NULL;
	}

	// 1.) Assigning this node's self_net to net_world container
	net_world -> self_net = self_net;

	// 2.) Assigning the values retrieved from join request

	// set the node_id that was just received from master
	net_world -> self_node_id = node_id;
	net_world -> max_nodes = max_nodes;
	net_world -> min_init_nodes = min_init_nodes;

	// 3.) Create a semphore to let the main init function know when it can return
	//		- Post to this semaphore occurs when net_world -> nodes table contains at least min_init_nodes + 1 (additional for master)
	//		- After every addition to the table check the table cnt and possibly post when it equal min_init_nodes + 1
	//			- Nodes are added to the table based on the function path from:
	//			- process_rdma_init_connection -> net_add_node
	ret = sem_init(&(net_world -> is_init_ready), 0, 0);
	if (ret != 0){
		fprintf(stderr, "Error: could not initialize is_init_ready semaphore\n");
		return NULL;
	}

	// 4.) specify the ip address to use for this node's rdma_init server

	net_world -> self_rdma_init_server_ip_addr = self_rdma_init_server_ip_addr;


	// 5.) Create the rdma_init_info structure that will be used for sharing this node's rdma info (ports + qps)
	Rdma_Init_Info * self_rdma_init_info = build_rdma_init_info(self_net, node_id);
	if (self_rdma_init_info == NULL){
		fprintf(stderr, "Error: failed to build rdma_init_info within init_net_world\n");
		return NULL;
	}

	net_world -> self_rdma_init_info = self_rdma_init_info;


	// 6.) Set up Nodes table that will be populated based on others' rdma_init_info 
	//		(which are received during inital tcp connections)


	// setting fixed table size of min_nodes == max_nodes

	uint32_t min_nodes = max_nodes;
	float load_factor = 1.0f;
	float shrink_factor = 0.0f;

	Hash_Func hash_func_net_node = &net_node_hash_func;
	Item_Cmp item_cmp_net_node = &net_node_cmp;
	Table * nodes = init_table(min_nodes, max_nodes, load_factor, shrink_factor, hash_func_net_node, item_cmp_net_node);
	if (nodes == NULL){
		fprintf(stderr, "Error: could not initialize net_world nodes table\n");
		return NULL;
	}

	net_world -> nodes = nodes;

	return net_world;
}

// TODO:
void destroy_net_world(Net_World * net_world){
	return;
}


struct ibv_ah * create_ah_from_remote_data(Net_World * net_world, int self_dev_id, union ibv_gid remote_gid, uint16_t remote_lid, uint8_t remote_port_num){


	// 1.) first obtain pd corresponing to the source device


	Self_Net * self_net = net_world -> self_net;
	struct ibv_pd * dev_pd = (self_net -> dev_pds)[self_dev_id];


	// 2.) Now create AH attributes

	struct ibv_ah_attr ah_attr;
	memset(&ah_attr, 0, sizeof(ah_attr));

	// Same for all ports
	//	- I think this is right..?
	ah_attr.is_global = 1;
	// 	- not sure what this refers to...
	ah_attr.grh.sgid_index = 0;
	
	// Based on the remote information!
	ah_attr.grh.dgid = remote_gid;
	ah_attr.dlid = remote_lid;
	ah_attr.port_num = remote_port_num;

	// 3.) Make ib verbs call to actually create AH
	struct ibv_ah *ah = ibv_create_ah(dev_pd, &ah_attr);
	if (ah == NULL){
		fprintf(stderr, "Error: could not create address handle\n");
		return NULL;
	}

	return ah;

}

void destory_address_handles(Net_Port * ports, uint32_t node_port_ind, int num_self_ib_devices){

	struct ibv_ah ** address_handles = ports[node_port_ind].address_handles;

	for (int m = 0; m < num_self_ib_devices; m++){
		ibv_destroy_ah(ports[node_port_ind].address_handles[m]);
	}
	
	free(address_handles);
}


// returns the node that was added to the table upon success
// null otherwise
Net_Node * net_add_node(Net_World * net_world, Rdma_Init_Info * remote_rdma_init_info){

	int ret;

	// 1.) allocate node container
	Net_Node * node = (Net_Node *) malloc(sizeof(Net_Node));
	if (node == NULL){
		fprintf(stderr, "Error: malloc failed allocating a remote node container\n");
		return NULL;
	}

	node -> node_id = remote_rdma_init_info -> header.node_id;
	node -> num_ports = remote_rdma_init_info -> header.num_ports;
	node -> num_endpoints = remote_rdma_init_info -> header.num_endpoints;


	// 2.) allocate remote ports container

	Net_Port * remote_ports = (Net_Port *) malloc(node -> num_ports * sizeof(Net_Port));
	if (remote_ports == NULL){
		fprintf(stderr, "Error: malloc failed allocating ports array for remote node\n");
		free(node);
		return NULL;
	}

	// 2b.) For each remote port, need to allocate containers for address handles for each self_ib_device (unique pds/ibv_contexts)

	Remote_Port_Init * remote_ports_init = remote_rdma_init_info -> remote_ports_init;

	// Depending on what this node chooses as it's sending QP (what device that sending QP is on 
	// => need to choose corresponding AH within each of these arrays
	int num_self_ib_devices = net_world -> self_net -> num_ib_devices;

	Ah_Creation_Data cur_ah_creation_data;
	struct ibv_ah * cur_ah;
	for (uint32_t i = 0; i < node -> num_ports; i++){

		// a.) create address handles to get to this remote port

		cur_ah_creation_data = remote_ports_init[i].ah_creation_data;
		// set the values in case we want to examine them
		remote_ports[i].gid = cur_ah_creation_data.gid;
		remote_ports[i].lid = cur_ah_creation_data.lid;
		remote_ports[i].port_num = cur_ah_creation_data.port_num;


		// allocate container for creating all address handles to this remote port from any of our ports
		//		- struct ibv_ah * needs pd == create ah per device
		remote_ports[i].address_handles = (struct ibv_ah **) malloc(num_self_ib_devices * sizeof(struct ibv_ah *));
		// make sure to clean up everything nicely in case of malloc error
		if (remote_ports[i].address_handles == NULL){
			fprintf(stderr, "Error: malloc failed allocating address handles array\n");
			// destroying address handles for all previous ports
			for (uint32_t k = 0; k < i; k++){
				destory_address_handles(remote_ports, k, num_self_ib_devices);
			}
			free(remote_ports);
			free(node);
			return NULL;
		}
		for (int j = 0; j < num_self_ib_devices; j++){
			 cur_ah = create_ah_from_remote_data(net_world, j, remote_ports[i].gid, remote_ports[i].lid, remote_ports[i].port_num);
			 // make sure to clean up everything nicely in case of verbs ah creation error
			 if (cur_ah == NULL){
			 	fprintf(stderr, "Error: create_ah_from_remote_data failed for remote port #%d and self_dev_id #%d\n", i, j);
			 	// destroying address handles for this port
			 	for (int m = 0; m < j; m++){
			 		ibv_destroy_ah(remote_ports[i].address_handles[m]);
			 	}
			 	// destroy this nodes address handles container
			 	free(remote_ports[i].address_handles);

			 	// destorying address handles for all previous ports
			 	for (int k = 0; k < i; k++){
			 		destory_address_handles(remote_ports, k, num_self_ib_devices);
			 	}

			 	// destroying remote ports
			 	free(remote_ports);
			 	free(node);
			 	return NULL;
			 }
			 remote_ports[i].address_handles[j] = cur_ah;
		}

		// b.) set other attributes for remote port
		remote_ports[i].state = remote_ports_init[i].state;
		remote_ports[i].active_mtu = remote_ports_init[i].active_mtu;
		remote_ports[i].active_speed = remote_ports_init[i].active_speed;
	}

	node -> ports = remote_ports;


	// 3.) Allocate memory for the endpoints

	Net_Endpoint * remote_endpoints = (Net_Endpoint *) malloc(node -> num_endpoints * sizeof(Net_Endpoint));
	if (remote_endpoints == NULL){
		fprintf(stderr, "Error: malloc failed allocating endpoints array for remote node\n");
		for (uint32_t k = 0; k < node -> num_ports; k++){
			destory_address_handles(node -> ports, k, num_self_ib_devices);
		}
		free(node -> ports);
		free(node);
		return NULL;
	}

	Remote_Endpoint * remote_endpoints_info = remote_rdma_init_info -> remote_endpoints;


	Remote_Endpoint cur_remote_endpoint_info;
	for (uint32_t i = 0; i < node -> num_endpoints; i++){
		cur_remote_endpoint_info = remote_endpoints_info[i];

		// set the endpoint type so this node knows which endpoint to send to 
		remote_endpoints[i].endpoint_type = cur_remote_endpoint_info.endpoint_type;

		// set the values for qp num and qkey so this node can address the right endpoint
		// at a given remote port
		remote_endpoints[i].remote_qp_num = cur_remote_endpoint_info.remote_qp_num;
		remote_endpoints[i].remote_qkey = cur_remote_endpoint_info.remote_qkey; 

		// this is is the index within net_node -> ports
		// in order to obtain the correct struct ibv_ah * (needed for sending UD)
		// then based on the sender's device choose an ah within this ports address_handles
		// (i.e. to send to this endpoint from self_device_id = j, then do:
		//	net_node -> ports[remote_node_port_ind] -> address_handles[j]
		remote_endpoints[i].remote_node_port_ind = cur_remote_endpoint_info.remote_node_port_ind;
	}

	node -> endpoints = remote_endpoints;

	// 4.) Add this node to the table

	ret = insert_item_table(net_world -> nodes, node);
	if (ret != 0){
		fprintf(stderr, "Error: could not add node id: %u to the table during rdma_init processing\n", node -> node_id);
		for (uint32_t k = 0; k < node -> num_ports; k++){
			destory_address_handles(node -> ports, k, num_self_ib_devices);
		}
		free(node -> ports);
		free(node -> endpoints);
		free(node);
		return NULL;
	}

	// 5.) return success
	return node;

}

// Freeing all the memory here to make it easy during failed processing of tcp_rdma_init (process_rdma_init_connection)
void destroy_remote_node(Net_World * net_world, Net_Node * node){

	// 1.) retrieve the number of ports, and for each port destroy all ibv_ah * and the address handle container

	Net_Port * remote_ports = node -> ports;

	int num_self_ib_devices = net_world -> self_net -> num_ib_devices;
	uint32_t num_ports = node -> num_ports;

	struct ibv_ah ** cur_address_handles;
	for (uint32_t i = 0; i < num_ports; i++){
		cur_address_handles = remote_ports[i].address_handles;
		// destroy all address handles
		for (int self_dev_id = 0; self_dev_id < num_self_ib_devices; self_dev_id++){
			ibv_destroy_ah(cur_address_handles[self_dev_id]);
		}
		free(cur_address_handles);
	}

	// 2.) free remote ports container
	free(remote_ports);

	// 3.) free endpoints container
	Net_Endpoint * endpoints = node -> endpoints;
	free(endpoints);
	
	// 4.) Remove node from net_world table
	remove_item_table(net_world -> nodes, node);

	// 5.) free node container
	free(node);

	return;
}