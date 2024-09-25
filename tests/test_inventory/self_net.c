#include "self_net.h"

// Will only use this compare function upon seeing a port go down
// and need to remove all items tied to that port
// Thus will use node_port_ind as the comparison
int self_active_ctrl_endpoint_cmp(void * self_endpoint, void * other_self_endpoint){
	uint32_t node_port_ind = ((Self_Endpoint *) self_endpoint) -> qp_port -> node_port_ind;
	uint32_t other_node_port_ind = ((Self_Endpoint *) other_self_endpoint) -> qp_port -> node_port_ind;
	if (node_port_ind == other_node_port_ind){
		return 0;
	}
	else if (node_port_ind > other_node_port_ind){
		return 1;
	}
	else{
		return -1;
	}
}


struct ibv_cq_ex * init_cq(struct ibv_context * ibv_dev_ctx, int num_cq_entries){

	// 1.) Create IBV_CQ_EX struct

	// TODO: have better configuration of sizing of queues!!! 
	struct ibv_cq_init_attr_ex cq_attr;
	memset(&cq_attr, 0, sizeof(cq_attr));
	cq_attr.cqe = num_cq_entries;    

	// Possible perf. optimization, but leaving out for now...
	// every cq will be in its own thread...
	uint32_t cq_create_flags = IBV_CREATE_CQ_ATTR_SINGLE_THREADED;
	cq_attr.flags = cq_create_flags;

	// NOTE: THESE FLAGS DON'T SEEM TO BE WORKING...
	// Maybe only on InfiniBand fabric...?
	uint64_t wc_flags = IBV_WC_EX_WITH_QP_NUM | IBV_WC_EX_WITH_SRC_QP | IBV_WC_EX_WITH_SLID | IBV_WC_EX_WITH_BYTE_LEN 
							 | IBV_WC_EX_WITH_COMPLETION_TIMESTAMP | IBV_WC_EX_WITH_COMPLETION_TIMESTAMP_WALLCLOCK;

	cq_attr.comp_mask = IBV_CQ_INIT_ATTR_MASK_FLAGS;
	cq_attr.wc_flags = wc_flags;

	struct ibv_cq_ex * ibv_cq = ibv_create_cq_ex(ibv_dev_ctx, &cq_attr);
	if (ibv_cq == NULL){
		fprintf(stderr, "Error: could not create ibv_cq\n");
		return NULL;
	}

	return ibv_cq;
}

// called from within init_self_port_container()
// upon port initialization Endpoint_Collection is populated based on num_endpoint_types and num_qps_per_type

int init_self_port(Self_Net * self_net, int device_id, uint8_t port_num, uint32_t node_port_ind, Self_Port * ret_port) {

	int ret;


	ret_port -> ib_device_id = device_id;
	// the physical port num on specific IB device
	// needed in order to create address handles to this destination
	ret_port -> port_num = port_num;
	// index into Self_Node -> Ports
	ret_port -> node_port_ind = node_port_ind;

	// opened device context associated with this port
	struct ibv_context * dev_ctx = (self_net -> ibv_dev_ctxs)[device_id];


	// 1.) Obtain Port GID

	union ibv_gid gid;
	// index 0 means port GID (provided by vendor)
	ret = ibv_query_gid(dev_ctx, port_num, 0, &gid);
	if (ret != 0){
		fprintf(stderr, "Error: could not not query GID for device #%d, port num #%d\n", 
					device_id, (int) port_num);
		return -1;
	}

	ret_port -> gid = gid;

	// 2.) Initially query port to get attributes (lid, mtu, speed)
	//		- note that over course of system runtime these attributes may change!
	
	struct ibv_port_attr port_attr;

	ret = ibv_query_port(dev_ctx, port_num, &port_attr);
	if (ret != 0){
		fprintf(stderr, "Error: ibv_query_port failed for device #%d, port num #%d\n", 
							device_id, (int) port_num);
		return -1;
	}

	// Save the import values from port attr
	ret_port -> state = port_attr.state;
	ret_port -> lid = port_attr.lid;
	ret_port -> active_mtu = port_attr.active_mtu;
	ret_port -> active_speed = port_attr.active_speed;
	// TODO: For multi-casting
	//			- not sure how this works, yet...
	ret_port -> sm_lid = port_attr.sm_lid;
	ret_port -> sm_sl = port_attr.sm_sl;

	// 3.) Deal with Partitioning
	//		- This is for either QoS or Security
	//		- SKIPPING FOR NOW


	// 4.) TODO: Obtain Local CPUs for the device associated with this port
	// - use the sysfs path within struct ibv_device -> ibdev_path
	//		- then read the file "local_cpus"
	//			- this file contains comma seperated uint32's representing bit-mask of local cpus
	

	return 0;

}


// called from within init_self_node()
// At initialization time port -> ah is left blank and populated during setup_world_net()
Self_Port * init_all_ports(Self_Net * self_net, uint32_t num_ports, int num_endpoint_types, EndpointType * endpoint_types, bool * to_use_srq_by_type, int * num_qps_per_type) {

	int ret;

	// Obtain device info from Self_Net
	int num_ib_devices = self_net -> num_ib_devices;
	int * num_ports_per_dev = self_net -> num_ports_per_dev;

	// 1.) Create packed array for all ports
	
	Self_Port * ports = (Self_Port *) malloc(num_ports * sizeof(Self_Port));
	if (ports == NULL){
		fprintf(stderr, "Error: malloc failed for allocating ports container\n");
		return NULL;
	}


	uint32_t cur_node_port_ind = 0;

	// physical port start at num = 1...?
	// port 0 reserved for subnet manager...?
	int device_num_ports;

	for (int device_id = 0; device_id < num_ib_devices; device_id++){
		device_num_ports = num_ports_per_dev[device_id];
		printf("Device #%d has %d ports\n", device_id, device_num_ports);
		// in InfiniBand physical port numbers start at 1
		for (int phys_port_num = 1; phys_port_num < device_num_ports + 1; phys_port_num++){
			ret = init_self_port(self_net, device_id, phys_port_num, cur_node_port_ind, &(ports[cur_node_port_ind]));

			// Within init_self_port, decided to report error as changing the device_id to be -1
			if (ret != 0){
				fprintf(stderr, "Error: failed to initialize port for device #%d, phys port num #%d\n", 
							device_id, phys_port_num);
				return NULL;
			}

			// Creating port (& all QPs attached to it) succeeded

			cur_node_port_ind += 1;
		}
	}

	return ports;
}


int bringup_qp(struct ibv_qp * qp, uint8_t port_num, uint32_t qkey, uint16_t pkey_index, uint32_t sq_psn){

	int ret;

	// first go to INIT, then RTS, then to RTS
	struct ibv_qp_attr mod_attr;
	memset(&mod_attr, 0, sizeof(mod_attr));

	// transition from reset to init
	mod_attr.qp_state = IBV_QPS_INIT;
	mod_attr.port_num = port_num;
	mod_attr.qkey = qkey;
	mod_attr.pkey_index = pkey_index;
	ret = ibv_modify_qp(qp, &mod_attr, IBV_QP_STATE | IBV_QP_PORT | IBV_QP_QKEY | IBV_QP_PKEY_INDEX);
	if (ret != 0){
		fprintf(stderr, "Error: could not move QP to Init state\n");
		return -1;
	}

	// now transition to RTR
	mod_attr.qp_state = IBV_QPS_RTR;
	ret = ibv_modify_qp(qp, &mod_attr, IBV_QP_STATE);
	if (ret != 0){
		fprintf(stderr, "Error: could not move QP to Ready-to-Receive state\n");
		return -1;
	}

	// now go to RTS state
	mod_attr.qp_state = IBV_QPS_RTS;
	mod_attr.sq_psn = sq_psn;
	ret = ibv_modify_qp(qp, &mod_attr, IBV_QP_STATE | IBV_QP_SQ_PSN);
	if (ret != 0){
		fprintf(stderr, "Error: could not move QP to Ready-to-Send state\n");
		return -1;
	}

	return 0;
}



int init_self_endpoint(Self_Net * self_net, Self_Port * port, int ib_device_id, EndpointType endpoint_type, struct ibv_cq_ex * send_ibv_cq, struct ibv_cq_ex * recv_ibv_cq, bool to_use_srq, uint32_t node_endpoint_ind, Self_Endpoint * ret_endpoint) {

	int ret;

	ret_endpoint -> node_endpoint_ind = node_endpoint_ind;
	ret_endpoint -> qp_port = port;
	ret_endpoint -> endpoint_type = endpoint_type;

	// 1.) Obtain the IBV structs needed to create qp
	struct ibv_context * ibv_dev_ctx = (self_net -> ibv_dev_ctxs)[ib_device_id];
	struct ibv_pd * ibv_pd = (self_net -> dev_pds)[ib_device_id];

	// Set ibv_srq to the device's srq if we want to share
	// (srq's are created by pd == per ibv_context == per device) 
	struct ibv_srq * ibv_srq = NULL;
	if (to_use_srq){
		ibv_srq = (self_net -> dev_srqs)[ib_device_id];
	}

	// 2.) CREATE IBV_QP Struct: MAIN FORM OF COMMUNICATION!!!
	//		- holds pointers to pretty much all other ibv_structs as fields

	struct ibv_qp_init_attr_ex qp_attr;
	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.pd = ibv_pd; // Setting Protection Domain
	qp_attr.qp_type = IBV_QPT_UD;

	// Consider not signaling sends (save latency/BW..?)
	qp_attr.sq_sig_all = 1;       // if not set 0, all work requests submitted to SQ will always generate a Work Completion.
	qp_attr.send_cq = ibv_cq_ex_to_cq(send_ibv_cq);         // completion queue can be shared or you can use distinct completion queues.
	qp_attr.recv_cq = ibv_cq_ex_to_cq(recv_ibv_cq);         // completion queue can be shared or you can use distinct completion queues.

	// Device cap of 2^15 for each side of QP's outstanding work requests...
	qp_attr.cap.max_send_wr = QP_MAX_SEND_WR;  // increase if you want to keep more send work requests in the SQ.
	qp_attr.cap.max_send_sge = QP_MAX_SEND_SGE; // increase if you allow send work requests to have multiple scatter gather entry (SGE).

	qp_attr.cap.max_inline_data = QP_MAX_INLINE_DATA;

	if (ibv_srq == NULL){
		qp_attr.cap.max_recv_wr = QP_MAX_RECV_WR;  // increase if you want to keep more receive work requests in the RQ.
		qp_attr.cap.max_recv_sge = QP_MAX_RECV_SGE; // increase if you allow receive work requests to have multiple scatter gather entry (SGE).
	}
	else{
		qp_attr.srq = ibv_srq;
	}
		
	uint64_t send_ops_flags = IBV_QP_EX_WITH_SEND;
	qp_attr.send_ops_flags = send_ops_flags;
	qp_attr.comp_mask = IBV_QP_INIT_ATTR_SEND_OPS_FLAGS | IBV_QP_INIT_ATTR_PD;

	struct ibv_qp * ibv_qp = ibv_create_qp_ex(ibv_dev_ctx, &qp_attr);
	if (ibv_qp == NULL){
		fprintf(stderr, "Error: could not create qp\n");
		return -1;
	}

	ret_endpoint -> ibv_qp = ibv_qp;

	// 2.) Now bringup qp!

	// for now setting all qkeys to 0 for convenience
	
	uint32_t qkey = 0;
	// not sure what to do with pkey_index, setting to 0 seems ok...?
	//	- needed for initial transition to IBV_QPS_INIT state
	uint16_t pkey_index = 0;

	// not sure what to do with sq_psn, setting to 0 seems ok...?
	//	- needed for transition from IBV_QPS_RTR to IBV_QPS_RTS
	uint32_t sq_psn = 0;


	ret = bringup_qp(ibv_qp, port -> port_num, qkey, pkey_index, sq_psn);
	if (ret != 0){
		fprintf(stderr, "Error: could not bringup qp\n");
		return -1;
	}
	
	
	// 3.) Retrieve and set qp_num to be conveniently accessible
	ret_endpoint -> qkey = qkey;
	ret_endpoint -> qp_num = ibv_qp -> qp_num;


	// 4.) If endpoint is a control endpoint create channels for it to send/recv on
	//		- if suppose to be using srq then don't create recv_channel, but instead point to device's srq

	ret_endpoint -> send_ctrl_channel = NULL;
	ret_endpoint -> recv_ctrl_channel = NULL;

	if (endpoint_type == CONTROL_ENDPOINT){

		// a.) create control channel
		Ctrl_Channel * send_ctrl_channel = init_ctrl_channel(SEND_CTRL_CHANNEL, QP_MAX_SEND_WR, ib_device_id, ibv_pd, node_endpoint_ind, ibv_qp, NULL, false);
		if (send_ctrl_channel == NULL){
			fprintf(stderr, "Error: failed to intialize send control channel on node endpoint ind: %u\n", node_endpoint_ind);
			return -1;
		}
		ret_endpoint -> send_ctrl_channel = send_ctrl_channel;

		// b.) either create or point to receive channel
		Ctrl_Channel * recv_ctrl_channel;
		if (to_use_srq){
			recv_ctrl_channel = (self_net -> recv_ctrl_channels)[ib_device_id];
		}
		else{
			recv_ctrl_channel = init_ctrl_channel(RECV_CTRL_CHANNEL, QP_MAX_RECV_WR, ib_device_id, ibv_pd, node_endpoint_ind, ibv_qp, NULL, true);
			if (recv_ctrl_channel == NULL){
				fprintf(stderr, "Error: failed to intialize receive control channel on node endpoint ind: %u\n", node_endpoint_ind);
				return -1;
			}
		}
		ret_endpoint -> recv_ctrl_channel = recv_ctrl_channel;
	}

	// succcess
	return 0;
}


Self_Endpoint * init_all_endpoints(Self_Net * self_net, uint32_t num_ports, Self_Port * ports, uint32_t num_endpoints, int num_endpoint_types, EndpointType * endpoint_types, bool * to_use_srq_by_type, int * num_qps_per_type, Deque * active_ctrl_endpoints){

	int ret;

	// 1.) Created packed array for all endpoints

	Self_Endpoint * endpoints = (Self_Endpoint *) malloc(num_endpoints * sizeof(Self_Endpoint));
	if (ports == NULL){
		fprintf(stderr, "Error: malloc failed for allocating ports container\n");
		return NULL;
	}

	// 2.) For each port create a set of endpoints

	Self_Port * cur_port;
	int cur_ib_device_id;
	struct ibv_cq_ex ** cur_dev_send_cqs;
	struct ibv_cq_ex ** cur_dev_recv_cqs;
	struct ibv_cq_ex * cur_send_cq, *cur_recv_cq;

	uint32_t cur_node_endpoint_ind = 0;

	int endpoint_type_num_qps;
	bool to_use_srq;
	EndpointType cur_endpoint_type;

	for (uint32_t i = 0; i < num_ports; i++){
		cur_port = &(ports[i]);
		cur_ib_device_id = cur_port -> ib_device_id;
		cur_dev_send_cqs = (self_net -> cq_send_collection)[cur_ib_device_id];
		cur_dev_recv_cqs = (self_net -> cq_recv_collection)[cur_ib_device_id];
		for (int j = 0; j < num_endpoint_types; j++){
			// using the same receive and send cq!
			//	- may want to change
			//	- also consider not signaling sends (save latency/BW..?)
			cur_send_cq = cur_dev_send_cqs[j];
			cur_recv_cq = cur_dev_recv_cqs[j];
			cur_endpoint_type = endpoint_types[j];
			to_use_srq = to_use_srq_by_type[j];
			endpoint_type_num_qps = num_qps_per_type[j];
			for (int k = 0; k < endpoint_type_num_qps; k++){

				ret = init_self_endpoint(self_net, cur_port, cur_ib_device_id, cur_endpoint_type, cur_send_cq, cur_recv_cq, to_use_srq, cur_node_endpoint_ind, &(endpoints[cur_node_endpoint_ind]));
				
				// Within init_self_endpoint, decided to report error as changing the qp_port to be NULL
				if (ret != 0){
					fprintf(stderr, "Error: failed to initialize endpoint for port #%u, endpoint type ind #%d, endpoint num (within type) #%d\n", i, j, k);
					return NULL;
				}

				// If this is a control endpoint and it's current state is active, then add it to the active control endpoint list
				if ((endpoints[cur_node_endpoint_ind].endpoint_type == CONTROL_ENDPOINT) && (cur_port -> state == IBV_PORT_ACTIVE)){
					ret = insert_deque(active_ctrl_endpoints, BACK_DEQUE, &endpoints[cur_node_endpoint_ind]);
					if (ret != 0){
						fprintf(stderr, "Error: couldnt insert self endpoint into the active_ctrl_endpoints deque\n");
						return NULL;
					}
				}	

				// successfully created endpoint so increment the index within packed array
				cur_node_endpoint_ind++;
			}
		}
	}

	return endpoints;

}


// called from within init_self_net()
// However this data structure (and not self_nets) will be shared with other nodes
// (and created as part of each node's world_net)
Self_Node * init_self_node(Self_Net * self_net, int num_endpoint_types, EndpointType * endpoint_types, bool * to_use_srq_by_type, int * num_qps_per_type) {

	Self_Node * self_node = (Self_Node *) malloc(sizeof(Self_Node));
	if (self_node == NULL){
		fprintf(stderr, "Error: malloc failed for initializing node net\n");
		return NULL;
	}

	int num_ib_devices = self_net -> num_ib_devices;
	int * num_ports_per_dev = self_net -> num_ports_per_dev;
	
	// 1.) Create ports

	uint32_t num_ports = 0;
	for (int i = 0; i < num_ib_devices; i++){
		printf("Device #%d has %d ports\n", i, num_ports_per_dev[i]);
		num_ports += num_ports_per_dev[i];
	}
	printf("Total number of ports: %u\n", num_ports);

	Self_Port * ports = init_all_ports(self_net, num_ports, num_endpoint_types, endpoint_types, to_use_srq_by_type, num_qps_per_type);
	if (ports == NULL){
		fprintf(stderr, "Error: failed to initialize ports from init_self_node\n");
		return NULL;
	}

	// 2.) Initialize a deque to maintain active control endpoints which will be used for sending control messages
	//		- probably have a round robin policy for which endpoint to send from

	Deque * active_ctrl_endpoints = init_deque(&self_active_ctrl_endpoint_cmp);

	// 3.) Create endpoints
	
	uint32_t total_qps_per_port = 0;
	for (int i = 0; i < num_endpoint_types; i++){
		total_qps_per_port += num_qps_per_type[i];
	}

	uint32_t num_endpoints = num_ports * total_qps_per_port;

	Self_Endpoint * endpoints = init_all_endpoints(self_net, num_ports, ports, num_endpoints, num_endpoint_types, endpoint_types, to_use_srq_by_type, num_qps_per_type, active_ctrl_endpoints);
	if (endpoints == NULL){
		fprintf(stderr, "Error: faile to initialize endpoints from init_self_node\n");
		return NULL;
	}


	// 3.) Set self_node

	self_node -> num_ports = num_ports;
	self_node -> ports = ports;
	self_node -> num_endpoints = num_ports * total_qps_per_port;
	self_node -> endpoints = endpoints;
	// this has been modified within init_all_endpoints
	self_node -> active_ctrl_endpoints = active_ctrl_endpoints;

	return self_node;
}

struct ibv_cq_ex *** create_cq_collection(struct ibv_context ** ibv_dev_ctxs, int num_devices, int num_endpoint_types, int num_cq_entries) {
	
	struct ibv_cq_ex *** cq_collection = (struct ibv_cq_ex ***) malloc(num_devices * sizeof(struct ibv_cq_ex **));
	if (cq_collection == NULL){
		fprintf(stderr, "Error: malloc failed for allocating cq collection container\n");
		return NULL;
	}

	struct ibv_context * ibv_dev_ctx;
	for (int i = 0; i < num_devices; i++){
		cq_collection[i] = (struct ibv_cq_ex **) malloc(num_endpoint_types * sizeof(struct ibv_cq_ex *));
		if (cq_collection[i] == NULL){
			fprintf(stderr, "Error: malloc failed for allocating cq array for device %d\n", i);
			return NULL;
		}
		ibv_dev_ctx = ibv_dev_ctxs[i];
		for (int endpoint_type = 0; endpoint_type < num_endpoint_types; endpoint_type++){
			cq_collection[i][endpoint_type] = init_cq(ibv_dev_ctx, num_cq_entries);
			if (cq_collection[i][endpoint_type] == NULL){
				fprintf(stderr, "Error: failed to create completion queue for device: %d, endpoint type: %d\n", i, endpoint_type);
				return NULL;
			}
		}
	}
	return cq_collection;
}

pthread_t ** create_cq_threads(int num_devices, int num_endpoint_types){

	pthread_t ** cq_threads = (pthread_t **) malloc(num_devices * sizeof(pthread_t *));
	if (cq_threads == NULL){
		fprintf(stderr, "Error: malloc failed for allocating cq_threads outer device array\n");
		return NULL;
	}

	for (int i = 0; i < num_devices; i++){
		cq_threads[i] = (pthread_t *) malloc(num_endpoint_types * sizeof(pthread_t));
		if (cq_threads[i] == NULL){
			fprintf(stderr, "Error: malloc failed to create cq_threads array for endpoint types on device: %d\n", i);
			return NULL;
		}
	}

	return cq_threads;
}


// responsible for opening ib_devices/getting contexts and allocating ib structs
// also responsible for intializing a self_node for itself that it will share with others
Self_Net * init_self_net(int num_endpoint_types, EndpointType * endpoint_types, bool * to_use_srq_by_type, int * num_qps_per_type, char * self_ip_addr){

	int ret;

	Self_Net * self_net = (Self_Net *) malloc(sizeof(Self_Net));
	if (self_net == NULL){
		fprintf(stderr, "Error: malloc failed for initializing self net\n");
		return NULL;
	}

	self_net -> num_endpoint_types = num_endpoint_types;
	self_net -> endpoint_types = malloc(num_endpoint_types * sizeof(EndpointType));
	if (self_net -> endpoint_types == NULL){
		fprintf(stderr, "Error: malloc failed to allocate array to hold endpoint types\n");
		return NULL;
	}
	memcpy(self_net -> endpoint_types, endpoint_types, num_endpoint_types * sizeof(EndpointType));

	// 1.) Get list of RDMA-devices attached to this node
	//		- Note: typically seperate physical ports on Mellanox cards 
	//			are listed as seperate devices each with 1 physical port

	int num_devices;
	struct ibv_device ** devices = ibv_get_device_list(&num_devices);
	if (devices == NULL){
		fprintf(stderr, "Error: could not get ibv_device list\n");
		return NULL;
	}

	if (num_devices == 0){
		fprintf(stderr, "Error: no rdma device fonud\n");
		return NULL;
	}

	self_net -> num_ib_devices = num_devices;
	self_net -> ib_devices = devices;

	// 2.) Now open each device and get it's ibv_context
	struct ibv_context ** ibv_dev_ctxs = malloc(num_devices * sizeof(struct ibv_context *));
	if (ibv_dev_ctxs == NULL){
		fprintf(stderr, "Error: malloc failed for allocating dev ctx container\n");
		return NULL;
	}

	for (int i = 0; i < num_devices; i++){
		ibv_dev_ctxs[i] = ibv_open_device(devices[i]);
		if (ibv_dev_ctxs[i] == NULL){
			fprintf(stderr, "Error: could not open device #%d to get ibv context\n", i);
			return NULL;
		}
	}

	self_net -> ibv_dev_ctxs = ibv_dev_ctxs;

	// 3.) Create Protection Domain for each ibv_context, 
	// (could also optionally create parent domain to be more precise about locking and thread domains)
	struct ibv_pd ** dev_pds = malloc(num_devices * sizeof(struct ibv_pd *));
	if (dev_pds == NULL){
		fprintf(stderr, "Error: malloc failed for allocating device pd container\n");
		return NULL;
	}

	for (int i = 0; i < num_devices; i++){
		dev_pds[i] = ibv_alloc_pd(ibv_dev_ctxs[i]);
		if (dev_pds[i] == NULL){
			fprintf(stderr, "Error: could not create pd for device #%d\n", i);
			return NULL;
		}
	}

	self_net -> dev_pds = dev_pds;

	// 4.) Get the number of physical ports per device
	int * num_ports_per_dev = malloc(num_devices * sizeof(int));
	if (num_ports_per_dev == NULL){
		fprintf(stderr, "Error: malloc failed for allocating ports per dev container\n");
		return NULL;
	}

	struct ibv_device_attr dev_attr;
	for (int i = 0; i < num_devices; i++){
		ret = ibv_query_device(ibv_dev_ctxs[i], &dev_attr);
		if (ret != 0){
			fprintf(stderr, "Error: could not query device\n");
			return NULL;
		}

		num_ports_per_dev[i] = (int) dev_attr.phys_port_cnt;
		printf("Device #%d has %d ports\n", i, dev_attr.phys_port_cnt);
	}

	self_net -> num_ports_per_dev = num_ports_per_dev;

	

	// 4.) Create SRQ for device that QPs can use depending on type
	struct ibv_srq ** dev_srqs = (struct ibv_srq **) malloc(num_devices * sizeof(struct ibv_srq *));
	if (dev_srqs == NULL){
		fprintf(stderr, "Error: malloc failed for allocating dev srq container\n");
		return NULL;
	} 

	// TODO: Need Better Queue Size Configuration!!!
	struct ibv_srq_init_attr srq_init_attr;
	memset(&srq_init_attr, 0, sizeof(struct ibv_srq_init_attr));

	srq_init_attr.attr.max_wr = SRQ_MAX_WR;
	srq_init_attr.attr.max_sge = SRQ_MAX_SGE;

	for (int i = 0; i < num_devices; i++){
		dev_srqs[i] = ibv_create_srq(dev_pds[i], &srq_init_attr);
		if (dev_srqs[i] == NULL){
			fprintf(stderr, "Error: ibv_create_srq failed for device #%d\n", i);
			return NULL;
		}
	}

	self_net -> dev_srqs = dev_srqs;


	// 5.) Create Channels for each srq
	Ctrl_Channel ** recv_ctrl_channels = (Ctrl_Channel **) malloc(sizeof(Ctrl_Channel *));
	if (recv_ctrl_channels == NULL){
		fprintf(stderr, "Error: malloc failed for allocating dev_shared_recv_channels\n");
		return NULL;
	}

	for (uint8_t i = 0; i < num_devices; i++){
		recv_ctrl_channels[i] = init_ctrl_channel(SHARED_RECV_CTRL_CHANNEL, SRQ_MAX_WR, i, dev_pds[i], 0, NULL, dev_srqs[i], true);
		if (recv_ctrl_channels[i] == NULL){
			fprintf(stderr, "Error: failed to initialize shared receive channel for device: %u\n", i);
			return NULL;
		}
	}

	self_net -> recv_ctrl_channels = recv_ctrl_channels;


	// 6.) Create Completition Queues for each device and for each endpoint type for both recv and send cqs
	
	struct ibv_cq_ex *** cq_recv_collection = create_cq_collection(ibv_dev_ctxs, num_devices, num_endpoint_types, SRQ_MAX_WR);
	if (cq_recv_collection == NULL){
		fprintf(stderr, "Error: unable to intialize cq recv collection\n");
		return NULL;
	}

	struct ibv_cq_ex *** cq_send_collection = create_cq_collection(ibv_dev_ctxs, num_devices, num_endpoint_types, QP_MAX_SEND_WR);
	if (cq_send_collection == NULL){
		fprintf(stderr, "Error: unable to intialize cq send collection\n");
		return NULL;
	}

	
	self_net -> cq_recv_collection = cq_recv_collection;
	self_net -> cq_send_collection = cq_send_collection;


	// 7.) Create threads for each completition queue, but do not spawn them (this is the final stage of init_net)
	pthread_t ** cq_recv_threads = create_cq_threads(num_devices, num_endpoint_types);
	if (cq_recv_threads == NULL){
		fprintf(stderr, "Error: unable to create container for cq recv threads\n");
		return NULL;
	}

	pthread_t ** cq_send_threads = create_cq_threads(num_devices, num_endpoint_types);
	if (cq_send_threads == NULL){
		fprintf(stderr, "Error: unable to create container for cq send threads\n");
		return NULL;
	}


	self_net -> cq_recv_threads = cq_recv_threads;
	self_net -> cq_send_threads = cq_send_threads;	


	// 8.) TODO: Get cpu_set's from querying each ib_devices sysfs path

	// Spawning with a specific cpu_set_t occurs when calling run_cq_thread from init_net -> cq_handler.c
	// the cpu_set assoicated with the device
	// found by reading the local_cpus file from sysfs
	// this file has comma seperate uint32_t's representing cpu bitmasks
	// the completition queue handler threads (assoicated with each device)
	// should have these bitmasks set when the thread is spawned
	// using pthread_setaffinity_np()

	

	cpu_set_t ** ib_dev_cpu_affinities = (cpu_set_t **) malloc(num_devices * sizeof(cpu_set_t *));
	if (ib_dev_cpu_affinities == NULL){
		fprintf(stderr, "Error: malloc failed to allocate ib_dev_cpu_affinities array\n");
		return NULL;
	}

	// FOR NOW: setting to null and not using
	for (int i = 0; i < num_devices; i++){
		// should be querying sysfs file
		// filepath can be found from struct ibv_device -> ibdev_path
		// then add extension of local_cpus
		ib_dev_cpu_affinities[i] = NULL;
	}
	
	self_net -> ib_dev_cpu_affinities = ib_dev_cpu_affinities;

	// 9.) Create Self_Node for self which contains information about ports and Queue Pairs
	Self_Node * self_node = init_self_node(self_net, num_endpoint_types, endpoint_types, to_use_srq_by_type, num_qps_per_type);
	if (self_node == NULL){
		fprintf(stderr, "Error: could not initialize self node\n");
		return NULL;
	}

	self_net -> self_node = self_node;
	
	// 7. Optionally use a specific IP address for connecting to master server
	//		- This also becomes the ip address used when binding for the rdma_init TCP server
	//		- If ip_addr is set to Null, then use the s_addr sent back from the join response
	//			when intially connecting to master_join_net server

	// used for TCP connections at initialization to exchange RDMA info
	// Take is input a normal looking ipv4 string, but store it internally as uint32
	// in network-byte order
	self_net -> ip_addr = self_ip_addr;

	return self_net;

}

Self_Net * default_worker_config_init_self_net(char * self_ip_addr) {

	int num_endpoint_types = 2;
	EndpointType endpoint_types[2] = {CONTROL_ENDPOINT, DATA_ENDPOINT};
	bool to_use_srq_by_type[2] = {true, false};
	int num_qps_per_type[2] = {1, 1};

	// returns NULL upon error
	return init_self_net(num_endpoint_types, endpoint_types, to_use_srq_by_type, num_qps_per_type, self_ip_addr);
}


Self_Net * default_master_config_init_self_net(char * self_ip_addr) {

	int num_endpoint_types = 1;
	EndpointType endpoint_types[1] = {CONTROL_ENDPOINT};
	bool to_use_srq_by_type[1] = {true};
	int num_qps_per_type[1] = {1};

	// returns NULL upon error
	return init_self_net(num_endpoint_types, endpoint_types, to_use_srq_by_type, num_qps_per_type, self_ip_addr);
}



// this is called from within the control handler threads
// it sees a work completition and needs to extract it
// and do something with it (pass it off to other worker threads)
// (e.g. exchange workers, sched workers, config workers, etc.)
Ctrl_Channel * get_recv_ctrl_channel(Self_Net * self_net, int ib_device_id){

	// Could error check if we cared
	return (self_net -> recv_ctrl_channels)[ib_device_id];
	
}

Ctrl_Channel * get_send_ctrl_channel(Self_Net * self_net, uint64_t wr_id) {

	CtrlChannelType channel_type;
	uint8_t ib_device_id;
	uint32_t endpoint_id;

	decode_ctrl_wr_id(wr_id, &channel_type, &ib_device_id, &endpoint_id);

	Self_Node * self_node = self_net -> self_node;

	if (channel_type != SEND_CTRL_CHANNEL){
		fprintf(stderr, "Error: decoded a wr_id = %lu to get a channel type of %d, but was expecting a send control type of %d\n", wr_id, channel_type, SEND_CTRL_CHANNEL);
		return NULL;
	}

	if (endpoint_id > self_node -> num_endpoints){
		fprintf(stderr, "Error: decoded wr_id = %lu to get send ctrl channel on endpoint id: %u, but only have %u endpoints\n", wr_id, endpoint_id, self_node -> num_endpoints);
		return NULL;
	}
	return (self_node -> endpoints)[endpoint_id].send_ctrl_channel;
}


Ctrl_Channel * get_ctrl_channel(Self_Net * self_net, uint64_t wr_id) {

	CtrlChannelType channel_type;
	uint8_t ib_device_id;
	uint32_t endpoint_id;

	decode_ctrl_wr_id(wr_id, &channel_type, &ib_device_id, &endpoint_id);

	Self_Node * self_node = self_net -> self_node;

	switch(channel_type){
		case SHARED_RECV_CTRL_CHANNEL:
			if (ib_device_id > self_net -> num_ib_devices){
				fprintf(stderr, "Error: decoded wr_id = %lu to get shared recv ctrl channel on ib device id: %u, but only have %u ib devices\n", wr_id, ib_device_id, self_net -> num_ib_devices);
				return NULL;
			}
			return (self_net -> recv_ctrl_channels)[ib_device_id];
		case RECV_CTRL_CHANNEL:
			if (endpoint_id > self_node -> num_endpoints){
				fprintf(stderr, "Error: decoded wr_id = %lu to get recv ctrl channel on endpoint id: %u, but only have %u endpoints\n", wr_id, endpoint_id, self_node -> num_endpoints);
				return NULL;
			}
			return (self_node -> endpoints)[endpoint_id].recv_ctrl_channel;
		case SEND_CTRL_CHANNEL:
			if (endpoint_id > self_node -> num_endpoints){
				fprintf(stderr, "Error: decoded wr_id = %lu to get send ctrl channel on endpoint id: %u, but only have %u endpoints\n", wr_id, endpoint_id, self_node -> num_endpoints);
				return NULL;
			}
			return (self_node -> endpoints)[endpoint_id].send_ctrl_channel;
		default:
			fprintf(stderr, "Error: decoded wr_id = %lu to get channel type of %d -- uknown channel\n", wr_id, channel_type);
			return NULL;

	}
}