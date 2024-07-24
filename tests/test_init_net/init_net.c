#include "init_net.h"


Net_World * init_net(char * self_ip_addr, char * master_ip_addr) {

	// 1. init self_net with default qp configs
	//		- doing this first to catch for errors before involving master

	// 2. call join_net to communicate with master and receive node_id and other nodes to connect to

	// 3. set node_id in self_net

	// 4. start this node's RDMA_INIT TCP server
	//		- Decide if we want to move this into step 2.) to prevent races / timeouts
	//			- Also if move into step 2, could be an earlier sign of failure
	//			- Downside is causes a bigger bottleneck at master
	//		- Enables future joiners to connect to this node and trade rdma_init info


	// 5. make / process rdma_init connection will all nodes within join_response -> node_config_arr (returned at step 2)
	//		- TODO: decide how to error handle on bad connections here..?
	//				- probably loop with:
	//					i.) ping master to check this worker node's status (it may have left network or died)
	//					ii.) retry connection

}