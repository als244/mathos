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


// called before exchanging any info. Intializes that nodes table
// That table then gets populated based on tcp exchange
Net_World * init_net_world(Self_Net * self_net, uint32_t max_nodes) {

	Net_World * net_world = (Net_World *) malloc(sizeof(Net_World));
	if (net_world == NULL){
		fprintf(stderr, "Error: malloc failed allocating world net\n");
		return NULL;
	}

	// 1.) Assigning this node's self_net to net_world container
	net_world -> self_net = self_net;

	// SETTING DEFAULT TABLE PARAMS HERE...
	// should really change location / args to config this better
	// MIGHT WANT TO HAVE THE TABLE BE FIXED SIZE SO THESE FACTORS DON'T MATTER
	// Keeping it this way for now, for flexiblity
	float load_factor = 0.5f;
	float shrink_factor = 0.1f;

	// setting min_nodes == max_nodes
	uint32_t min_nodes = max_nodes;

	// 1.) Setting up Nodes Table that will be populated during intial TCP connections

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
		return NULL;
	}

	if (dest_device_id > dest_node -> num_devices){
		fprintf(stderr, "Error: requesting a device number larger than number of available devices\n");
		return NULL;
	}

	Net_Port * device_ports = (dest_node -> ports)[dest_device_id];
	uint8_t device_num_ports = (dest_node -> num_ports_per_dev)[dest_device_id];
	// +1 because indexed starting at 1 (0th entry is blank)
	if (dest_port_num > (device_num_ports + 1)){
		fprintf(stderr, "Error: requesting a port number larger than number of available ports on device\n");
		return NULL;
	}

	Net_Port port = device_ports[dest_port_num];

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