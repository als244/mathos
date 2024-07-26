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
	net_world -> self_rdma_init_server_ip_addr = self_rdma_init_server_ip_addr;


	// 2.) Create the rdma_init_info structure that will be used for sharing this node's rdma info (ports + qps)
	Rdma_Init_Info * self_rdma_init_info = build_rdma_init_info(self_net, node_id);
	if (self_rdma_init_info == NULL){
		fprintf(stderr, "Error: failed to build rdma_init_info within init_net_world\n");
		return NULL;
	}

	net_world -> self_rdma_init_info = self_rdma_init_info;


	// 3.) Set up Nodes table that will be populated based on others' rdma_init_info 
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


void destroy_net_world(Net_World * net_world){
	return;
}