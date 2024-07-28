#include "init_net.h"


Net_World * init_net(char * master_ip_addr, char * self_ip_addr) {

	int ret;

	// 1.) create self_net, which 

	// self_ip_addr is optional

	Self_Net * self_net = default_worker_config_init_self_net(self_ip_addr);
	if (self_net == NULL){
		fprintf(stderr, "Error: self_net initialization failed\n");
		return NULL;
	}

	// 2.) Request to join the network
	//		- upon success, this function will returned:
	//			- an initialized net_world populated with master's rdma info
	//			- a response from the master with all the other nodes' ips we should connect to
	//			- will start this node's rdma_init server in the background


	Join_Response * join_response;
	Net_World * net_world;
	

	ret = join_net(self_net, master_ip_addr, &join_response, &net_world);
	if (ret != 0){
		fprintf(stderr, "Error: join_net initialization failed\n");
		return NULL;
	}

	// 3.) now connect to all of the nodes in join response (defined in config.h)
	int num_other_nodes_to_connect_to = join_response -> header.cur_node_cnt;

	Node_Config * other_nodes_to_connect_to = join_response -> node_config_arr;

	Node_Config cur_rdma_init_server;

	struct in_addr rdma_init_server_in_addr;
	char * rdma_init_server_ip_addr;
	for (int i = 0; i < num_other_nodes_to_connect_to; i++){
		cur_rdma_init_server = other_nodes_to_connect_to[i];
		rdma_init_server_in_addr.s_addr = cur_rdma_init_server.s_addr;
		rdma_init_server_ip_addr = inet_ntoa(rdma_init_server_in_addr);
		ret = connect_to_rdma_init_server(net_world, rdma_init_server_ip_addr, false);
		if (ret != 0){
			fprintf(stderr, "Error: fatal failure connecting to rdma init server with ip addr: %s\n", rdma_init_server_ip_addr);
			return NULL;
		}
		printf("[Node %u] RDMA Initialization Successful! To Node ID: %u (ip addr: %s). Added to net_world table.\n", 
					net_world -> self_node_id, cur_rdma_init_server.node_id, rdma_init_server_ip_addr);
	}



	// 3b.) done with join_response so can free (was allocated within processing join_net)
	free(join_response);


	// 4.) wait until min_init_nodes (besides master) have been added to the net_world -> nodes table
	sem_wait(&(net_world -> is_init_ready));


	// 5.) return net_world
	return net_world;
}