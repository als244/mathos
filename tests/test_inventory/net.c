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


uint64_t ip_to_node_hash_func(void * node_ip_config, uint64_t table_size){
	// here ip_addr is the network-byte order of ip addr equivalent to inet_addr(char *)
	uint32_t key = ((Node_Ip_Config *) node_ip_config) -> ip_addr;
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

int ip_to_node_cmp(void * node_ip_config, void * other_node_ip_config){
	uint32_t ip_a = ((Node_Ip_Config *) node_ip_config) -> ip_addr;
	uint32_t ip_b = ((Node_Ip_Config *) other_node_ip_config) -> ip_addr;
	return ip_a - ip_b;
}

uint64_t node_to_ip_hash_func(void * node_ip_config, uint64_t table_size){
	// here ip_addr is the network-byte order of ip addr equivalent to inet_addr(char *)
	uint32_t key = ((Node_Ip_Config *) node_ip_config) -> node_id;
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

int node_to_ip_cmp(void * node_ip_config, void * other_node_ip_config){
	uint32_t id_a = ((Node_Ip_Config *) node_ip_config) -> node_id;
	uint32_t id_b = ((Node_Ip_Config *) other_node_ip_config) -> node_id;
	return id_a - id_b;
}


// called before exchanging any info. Intializes that nodes table
// That table then gets populated based on tcp exchange
Net_World * init_net_world(Self_Net * self_net, int min_nodes, int max_nodes, char * node_ip_config_filename) {

	Net_World * net_world = (Net_World *) malloc(sizeof(Net_World));
	if (net_world == NULL){
		fprintf(stderr, "Error: malloc failed allocating world net\n");
		return NULL;
	}

	net_world -> self_net = self_net;

	// SETTING DEFAULT TABLE PARAMS HERE...
	// should really change location / args to config this better
	// MIGHT WANT TO HAVE THE TABLE BE FIXED SIZE SO THESE FACTORS DON'T MATTER
	// Keeping it this way for now, for flexiblity
	float load_factor = 0.5f;
	float shrink_factor = 0.1f;

	// 1.) Set up IP to Node & Node to IP tables so as to properly create TCP connections

	// FROM ip_addr => node_id
	Hash_Func hash_func_ip_to_node = &ip_to_node_hash_func;
	Item_Cmp item_cmp_ip_to_node = &ip_to_node_cmp;
	Table * ip_to_node = init_table(min_nodes, max_nodes, load_factor, shrink_factor, hash_func_ip_to_node, item_cmp_ip_to_node);
	if (ip_to_node == NULL){
		fprintf(stderr, "Error: could not initialize net_world ip_to_node table\n");
		return NULL;
	}

	// FROM node_id => ip_addr
	Hash_Func hash_func_node_to_ip = &node_to_ip_hash_func;
	Item_Cmp item_cmp_node_to_ip = &node_to_ip_cmp;
	Table * node_to_ip = init_table(min_nodes, max_nodes, load_factor, shrink_factor, hash_func_node_to_ip, item_cmp_node_to_ip);
	if (node_to_ip == NULL){
		fprintf(stderr, "Error: could not initialize net_world node_to_ip table\n");
		return NULL;
	}


	// 2.) Populate the node_ips table using the node_ips_config file
	int ret = populate_node_ip_tables(ip_to_node, node_to_ip, node_ip_config_filename);
	if (ret != 0){
		fprintf(stderr, "Error: could not populate the node ips table\n");
		return NULL;
	}

	// set these tables in net_world in case they are needed
	net_world -> ip_to_node = ip_to_node;
	net_world -> node_to_ip = node_to_ip; 


	// 3.) Setting up Nodes Table that will be populated during intial TCP connections

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

// Used for Data Transmission
// Data Request sends (node_id, port_num, qp_num, q_key)
// Here qp_num and q_key denote a specific Data QP that the receiver acquired exclusive access to

// The destination node_id, port_num are known at intialization and are in table,
// Within the data sending, the qp_num, and q_key from request are used not the ctrl qp values
struct ibv_ah * get_dest_ah_from_request(Net_World * net_world, uint32_t dest_node_id, uint8_t dest_device_id, uint8_t dest_port_num) {

	Net_Node target_node;
	target_node.node_id = dest_node_id;
	Net_Node * dest_node = find_item_table(net_world -> nodes, &target_node);
	if (dest_node == NULL){
		fprintf(stderr, "Error: could not find destination node\n");
		return -1;
	}

	if (device_id > dest_node -> num_devices){
		fprintf(stderr, "Error: requesting a device number larger than number of available devices\n");
		return NULL;
	}

	Net_Device device = (dest_node -> devices)[device_id];

	// +1 because indexed starting at 1 (0th entry is blank)
	if (dest_port_num > (device.num_ports + 1)){
		fprintf(stderr, "Error: requesting a port number larger than number of available ports on device\n");
		return NULL;
	}

	Net_Port port = device.ports[dest_port_num];

	return port.ah;
}


int get_ctrl_endpoint(Net_World * net_world, uint32_t dest_node_id, Net_Endpoint * ret_net_endpoint) {

	Net_Node target_node;
	target_node.node_id = dest_node_id;

	Net_Node * dest_node = find_item_table(net_world -> nodes, &target_node);
	if (dest_node == NULL){
		fprintf(stderr, "Error: could not find destination node\n");
		return -1;
	}

	*ret_net_endpoint = dest_node -> ctrl_dest;

	return 0;
}