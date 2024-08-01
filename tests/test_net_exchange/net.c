#include "net.h"


// Will only use this compare function upon seeing a port go down
// and need to remove all items tied to that port
// Thus will use remote_node_port_ind as the comparison
int remote_active_ctrl_endpoint_cmp(void * net_endpoint, void * other_net_endpoint){
	uint32_t remote_node_port_ind = ((Net_Endpoint *) net_endpoint) -> remote_node_port_ind;
	uint32_t other_remote_node_port_ind = ((Net_Endpoint *) other_net_endpoint) -> remote_node_port_ind;
	return remote_node_port_ind - other_remote_node_port_ind;
}

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

	Ah_Creation_Data * cur_ah_creation_data;
	struct ibv_ah * cur_ah;
	for (uint32_t i = 0; i < node -> num_ports; i++){

		// a.) create address handles to get to this remote port

		cur_ah_creation_data = &remote_ports_init[i].ah_creation_data;
		// set the values in case we want to examine them
		remote_ports[i].gid = cur_ah_creation_data -> gid;
		remote_ports[i].lid = cur_ah_creation_data -> lid;
		remote_ports[i].port_num = cur_ah_creation_data -> port_num;


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

		char * gid_hex = byte_arr_to_hex_str(GID_NUM_BYTES, remote_ports[i].gid.raw);

		printf("\n[Node %u] Added remote port from node: %u. Port info:\n\tGID: %s\n\tLID: %u\n\tPort Num: %u\n\tState: %d\n\tActive MTU: %u\n\tActive Speed: %u\n\n", 
				net_world -> self_node_id, node -> node_id, gid_hex, remote_ports[i].lid, remote_ports[i].port_num, remote_ports[i].state, remote_ports[i].active_mtu, remote_ports[i].active_speed);
		
		free(gid_hex);
	}	

	node -> ports = remote_ports;

	// 3.) Create a deque to maintain all active control endpoints we can send to

	Deque * active_ctrl_endpoints = init_deque(&remote_active_ctrl_endpoint_cmp);

	// 4.) Allocate memory for the endpoints

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


	Remote_Endpoint * cur_remote_endpoint_info;


	for (uint32_t i = 0; i < node -> num_endpoints; i++){
		cur_remote_endpoint_info = &remote_endpoints_info[i];

		// set the endpoint type so this node knows which endpoint to send to 
		remote_endpoints[i].endpoint_type = cur_remote_endpoint_info -> endpoint_type;

		// set the values for qp num and qkey so this node can address the right endpoint
		// at a given remote port
		remote_endpoints[i].remote_qp_num = cur_remote_endpoint_info -> remote_qp_num;
		remote_endpoints[i].remote_qkey = cur_remote_endpoint_info -> remote_qkey; 

		// this is is the index within net_node -> ports
		// in order to obtain the correct struct ibv_ah * (needed for sending UD)
		// then based on the sender's device choose an ah within this ports address_handles
		// (i.e. to send to this endpoint from self_device_id = j, then do:
		//	net_node -> ports[remote_node_port_ind] -> address_handles[j]
		remote_endpoints[i].remote_node_port_ind = cur_remote_endpoint_info -> remote_node_port_ind;

		// add the index of the endpoint in case we want to move this struct around and refer back to the endpoints array
		remote_endpoints[i].remote_node_endpoint_ind = i;

		printf("\n[Node %u] Added remote endpoint from node: %u. Endpoint info:\n\tEndpoint Type: %d\n\tQP Num: %u\n\tQKey: %u\n\tRemote Node Port Ind: %u\n\tRemote Node Endpoint Ind: %u\n\n\n", 
				net_world -> self_node_id, node -> node_id, remote_endpoints[i].endpoint_type, remote_endpoints[i].remote_qp_num, remote_endpoints[i].remote_qkey, remote_endpoints[i].remote_node_port_ind, i);

		// if this is a contorl endpoint and it's corresponding port is active add it to the active_ctrl_endpoints deque
		if ((remote_endpoints[i].endpoint_type == CONTROL_ENDPOINT) && 
				(remote_ports[remote_endpoints[i].remote_node_port_ind].state == IBV_PORT_ACTIVE)){
			ret = insert_deque(active_ctrl_endpoints, BACK_DEQUE, &(remote_endpoints[i]));
			
			// only happens during OOM
			if (unlikely(ret != 0)){
				fprintf(stderr, "Error: could not insert remote endpoint to the active_ctrl_endpoints deque\n");
				for (uint32_t k = 0; k < node -> num_ports; k++){
					destory_address_handles(node -> ports, k, num_self_ib_devices);
				}
				free(node -> ports);
				free(node -> endpoints);
				free(node);
				destroy_deque(active_ctrl_endpoints, false);
				return NULL;
			}
		}
	}

	node -> endpoints = remote_endpoints;
	node -> dest_active_ctrl_endpoints = active_ctrl_endpoints;


	// 4.) Set a default sending control channel to get to this node
	//		- chooseing dest_node_id % noumber of self active ctrl endpoints

	Deque * self_active_ctrl_endpoints = net_world -> self_net -> self_node -> active_ctrl_endpoints;
	uint64_t num_self_active_ctrl_dest = get_count_deque(self_active_ctrl_endpoints);

	uint64_t default_assigned_ctrl_send_ind = node -> node_id % num_self_active_ctrl_dest;

	Self_Endpoint * default_send_ctrl_endpoint;
	ret = peek_item_at_index_deque(self_active_ctrl_endpoints, FRONT_DEQUE, default_assigned_ctrl_send_ind, (void **) &default_send_ctrl_endpoint);
	
	Ctrl_Channel * default_send_ctrl_channel;
	// there are no active contorl endpoints here
	if (ret != 0){
		default_send_ctrl_channel = NULL;
	}
	else{
		default_send_ctrl_channel = default_send_ctrl_endpoint -> send_ctrl_channel;
	}

	node -> default_send_ctrl_channel = default_send_ctrl_channel;


	// 5.) Set default net_dest for control destination (if we want to avoid overhead of taking/replacing from active dest list)
	//		- choosing self_node_id % number of active ctrl endpoints at dest
	//			- this so that in a large network various nodes will have different default destinations to the same node id

	Net_Endpoint * default_dest_ctrl_endpoint;
	uint64_t num_active_ctrl_dest = get_count_deque(active_ctrl_endpoints);
	uint64_t default_assigned_ctrl_dest_ind = net_world -> self_node_id % num_active_ctrl_dest;

	ret = peek_item_at_index_deque(active_ctrl_endpoints, FRONT_DEQUE, default_assigned_ctrl_dest_ind, (void **) &default_dest_ctrl_endpoint);
	// There were no destination control endpoints with active port
	//	- indicate ah = NULL as this
	struct ibv_ah * ah;
	uint32_t remote_qp_num;
	uint32_t remote_qkey;
	if (ret != 0){
		ah = NULL;
		remote_qp_num = 0;
		remote_qkey = 0;
	}
	else{

		// need to look up the correct address handle
		
		// 1.) look up deafult sending channel to determine the ib device
		// there are no sending control endpoints with an active port => we can't have a destination contorl endpoint
		if (default_send_ctrl_channel == NULL){
			ah = NULL;
			remote_qp_num = 0;
			remote_qkey = 0;
		}
		else{
			int ib_device_id_default_ctrl_send = default_send_ctrl_channel -> ib_device_id;
			uint32_t default_dest_ctrl_port_ind = default_dest_ctrl_endpoint -> remote_node_port_ind;
			Net_Port default_dest_ctrl_port = remote_ports[default_dest_ctrl_port_ind];
			struct ibv_ah ** default_dest_ctrl_port_address_handles = default_dest_ctrl_port.address_handles;
			ah = default_dest_ctrl_port_address_handles[ib_device_id_default_ctrl_send];
			remote_qp_num = default_dest_ctrl_endpoint -> remote_qp_num;
			remote_qkey = default_dest_ctrl_endpoint -> remote_qkey;
		}
	}

	// setting the default control destination
	node -> default_ctrl_dest.ah = ah;
	node -> default_ctrl_dest.remote_qp_num = remote_qp_num;
	node -> default_ctrl_dest.remote_qkey = remote_qkey;	


	// 5.) Add this node to the table

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

// Upon intialization the default control / send channels are decided upon
//	- choose node_id % number of self active ctrl endpoints to send
//		- so that control messags to different nodes won't be blocked
//	- choosing self_node_id % number of active ctrl endpoints at dest
//		- this so that in a large network various nodes will have different default destinations to the same node id

// This function has little overhead involved with sending because it doesn't have to acquire lock from active_ctrl_dest deques or deal
// with extra overhead of determining address handle
int post_send_ctrl_net(Net_World * net_world, Ctrl_Message * ctrl_message) {

	int ret;

	// 1.) Lookup node in table
	uint32_t remote_node_id = ctrl_message -> header.dest_node_id;
	Net_Node target_node;
	target_node.node_id = remote_node_id;

	Net_Node * remote_node = find_item_table(net_world -> nodes, &target_node);
	if (remote_node == NULL){
		fprintf(stderr, "Error: post_send_ctrl_net failed because couldn't find remote node with id %u in net_world -> nodes table\n", remote_node_id);
		return -1;
	}

	// 2.) Obtain the appropriate sending channel
	Ctrl_Channel * default_send_ctrl_channel = remote_node -> default_send_ctrl_channel;
	if (default_send_ctrl_channel == NULL){
		fprintf(stderr, "Error: post_send_ctrl_net failed becaues it appears this node as no control endpoints available\n");
		return -1;
	}

	// 3.) Obtain default destination from node
	Net_Dest default_ctrl_dest = remote_node -> default_ctrl_dest;
	if (default_ctrl_dest.ah == NULL){
		fprintf(stderr, "Error: post_send_ctrl_net failed because it appears the destination node %u, has no control endpoints available\n", remote_node_id);
		return -1;
	}

	// 4.) Actually post send
	ret = post_send_ctrl_channel(default_send_ctrl_channel, ctrl_message, default_ctrl_dest.ah, default_ctrl_dest.remote_qp_num, default_ctrl_dest.remote_qkey);
	if (ret != 0){
		fprintf(stderr, "Error: failure to post to control channel within default_post_send_ctrl_net\n");
		return -1;
	}

	return 0;

}


// THIS ALLOWS US TO THE CHANGE THE SENDING/RECEVIEVING CONTROL ENDPOINTS TO ENABLE SOME POLICY

// Incurs extra overhead of acquiring lock active_ctrl_endpoint deques (for both self/dest) and looking up address handle
//	- thus using the default post_send_ctrl_net function above, until there is a reason not to

// Within this function there is a policy to choose the sending / receiving endpoints 
//	- based on active ctrl endpoint deques within self_node and net_node
//	- currently policy is to do round-robin for each (i.e. take at front and replace at back)
//		- for load balancing reasons
//	- however probably want to take cpu affinity into account...
int policy_post_send_ctrl_net(Net_World * net_world, Ctrl_Message * ctrl_message) {

	int ret;

	uint32_t remote_node_id = ctrl_message -> header.dest_node_id;

	// 1.) Lookup node in table
	Net_Node target_node;
	target_node.node_id = remote_node_id;

	Net_Node * remote_node = find_item_table(net_world -> nodes, &target_node);
	if (remote_node == NULL){
		fprintf(stderr, "Error: policy_post_send_ctrl_net failed because couldn't find remote node with id %u in net_world -> nodes table\n", remote_node_id);
		return -1;
	}

	// 2.) Decide what endpoints to use

	//	Determine the sending and receiving endpoints
	//	- deciding to make this round robin to evenly balance the links
	//		- note, taking from front, but replacing at back
	//	- HOWEVER, probably want to account for CPU locality wihtin this policy


	// 2a.) Choose self sending endpoint

	Self_Endpoint * send_ctrl_endpoint; 
	Deque * self_active_ctrl_endpoints = net_world -> self_net -> self_node -> active_ctrl_endpoints;

	ret = take_and_replace_deque(self_active_ctrl_endpoints, FRONT_DEQUE, BACK_DEQUE, (void **) &send_ctrl_endpoint);
	if (ret != 0){
		fprintf(stderr, "Error: could not post send ctrl net because no active sending control endpoints\n");
		return -1;
	}

	uint32_t send_self_endpoint_ind = send_ctrl_endpoint -> node_endpoint_ind;

	// 2b.) Decide remote endpoint

	Net_Endpoint * remote_ctrl_endpoint;
	Deque * remote_active_ctrl_endpoints = remote_node -> dest_active_ctrl_endpoints;

	ret = take_and_replace_deque(remote_active_ctrl_endpoints, FRONT_DEQUE, BACK_DEQUE, (void **) &remote_ctrl_endpoint);
	if (ret != 0){
		fprintf(stderr, "Error: could not post send ctrl net because no active remote control endpoints at destination node: %u\n", remote_node_id);
		return -1;
	}

	uint32_t remote_node_endpoint_ind = remote_ctrl_endpoint -> remote_node_endpoint_ind;

	// 3.) Get endpoint information

	if (remote_node_endpoint_ind >= remote_node -> num_endpoints){
		fprintf(stderr, "Error: post_send_net failed because remote_node_endpoint_ind of %u, is greater than total remote endpoints of: %u\n", 
			remote_node_endpoint_ind, remote_node -> num_endpoints);
		return -1;
	}

	Net_Endpoint net_endpoint = (remote_node -> endpoints)[remote_node_endpoint_ind];
	
	// 2b.) Retrieve remote qp num and qkey
	uint32_t remote_qp_num = net_endpoint.remote_qp_num;
	uint32_t remote_qkey = net_endpoint.remote_qkey;

	// 3.) Lookup the destination port corresponding to that endpoint
	uint32_t remote_node_port_ind = net_endpoint.remote_node_port_ind;
	if (remote_node_port_ind >= remote_node -> num_ports){
		fprintf(stderr, "Error: post_send_net failed because remote_node_port_ind of %u, is greater than total remote ports of: %u\n", 
			remote_node_port_ind, remote_node -> num_ports);
		return -1;
	}

	Net_Port net_port = (remote_node -> ports)[remote_node_port_ind];
	struct ibv_ah ** address_handles = net_port.address_handles;

	// 4.) Lookup the sending endpoint to retrieve it's send_channel and obtain the correct ah
	Self_Node * self_node = net_world -> self_net -> self_node;
	if (send_self_endpoint_ind >= self_node -> num_endpoints){
		fprintf(stderr, "Error: post_send_net failed because self endpoint ind of %u, is greater than total self endpoints of: %u\n", 
			send_self_endpoint_ind, self_node -> num_endpoints);
		return -1;
	}

	Self_Endpoint self_endpoint = (self_node -> endpoints)[send_self_endpoint_ind];
	Ctrl_Channel * send_ctrl_channel = self_endpoint.send_ctrl_channel;
	int ib_device_id = send_ctrl_channel -> ib_device_id;
	struct ibv_ah * dest_ah = address_handles[ib_device_id];

	// 5.) Actually post send
	ret = post_send_ctrl_channel(send_ctrl_channel, ctrl_message, dest_ah, remote_qp_num, remote_qkey);
	if (ret != 0){
		fprintf(stderr, "Error: failure to post to control channel within post_send_ctrl_net\n");
		return -1;
	}

	return 0;


}