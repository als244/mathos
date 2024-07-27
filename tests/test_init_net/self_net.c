#include "self_net.h"


// BID TODO: fix this configuration of queue sizes to be more flexible!!!

#define NUM_CQ_ENTRIES 1U << 12
#define SRQ_MAX_WR 1U << 12
#define SRQ_MAX_SGE 2

#define QP_MAX_SEND_WR 1U << 8
#define QP_MAX_SEND_SGE 2

#define QP_MAX_RECV_WR 1U << 8
#define QP_MAX_RECV_SGE 2

#define QP_MAX_INLINE_DATA 512 // SEEMS LIKE 956 is the max for qp creation error...?

CQ * init_cq(struct ibv_context * ibv_dev_ctx, EndpointType endpoint_type){

	CQ * cq = (CQ *) malloc(sizeof(CQ));
	if (cq == NULL){
		fprintf(stderr, "Error: malloc failed to allocate cq\n");
		return NULL;
	}

	// The CQ Usage Type is assoicated with a corresponding QP Usage Type
	// Meaning this CQ is used on QPs of a given type
	cq -> endpoint_type = endpoint_type;

	// 1.) Create IBV_CQ_EX struct

	// TODO: have better configuration of sizing of queues!!! 
	struct ibv_cq_init_attr_ex cq_attr;
	memset(&cq_attr, 0, sizeof(cq_attr));
	cq_attr.cqe = NUM_CQ_ENTRIES;    

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

	cq -> ibv_cq = ibv_cq;

	return cq;
}

CQ_Collection * init_cq_collection(struct ibv_context * ibv_dev_ctx, int device_id, int num_endpoint_types, EndpointType * endpoint_types){

	CQ_Collection * cq_collection = (CQ_Collection *) malloc(sizeof(CQ_Collection));
	if (cq_collection == NULL){
		fprintf(stderr, "Error: malloc failed to allocate cq collection\n");
		return NULL;
	}

	cq_collection -> ib_device_id = device_id;

	// Have One CQ for each qp type per device
	// May want to have more...?
	CQ ** send_cqs = (CQ **) malloc(num_endpoint_types * sizeof(CQ *));
	CQ ** recv_cqs = (CQ **) malloc(num_endpoint_types * sizeof(CQ *));
	if ((send_cqs == NULL) || (recv_cqs == NULL)){
		fprintf(stderr, "Error: malloc failed to allocate cq containers\n");
		return NULL;
	}

	for (int i = 0; i < num_endpoint_types; i++){
		send_cqs[i] = init_cq(ibv_dev_ctx, endpoint_types[i]);
		recv_cqs[i] = init_cq(ibv_dev_ctx, endpoint_types[i]);
		if ((send_cqs[i] == NULL) || (recv_cqs[i] == NULL)){
			fprintf(stderr, "Error: failed to initialize cq for device #%d\n", device_id);
			return NULL;
		}
	}

	cq_collection -> send_cqs = send_cqs;
	cq_collection -> recv_cqs = recv_cqs;

	return cq_collection;
}


// called from within init_self_port_container()
// upon port initialization Endpoint_Collection is populated based on num_endpoint_types and num_qps_per_type

// if error set port.ib_device_id = -1
Self_Port init_self_port(Self_Net * self_net, int device_id, uint8_t port_num, uint32_t node_port_ind) {

	int ret;

	Self_Port port;
	port.ib_device_id = device_id;
	// the physical port num on specific IB device
	// needed in order to create address handles to this destination
	port.port_num = port_num;
	// index into Self_Node -> Ports
	port.node_port_ind = node_port_ind;

	// opened device context associated with this port
	struct ibv_context * dev_ctx = (self_net -> ibv_dev_ctxs)[device_id];


	// 1.) Obtain Port GID

	union ibv_gid gid;
	// index 0 means port GID (provided by vendor)
	ret = ibv_query_gid(dev_ctx, port_num, 0, &gid);
	if (ret != 0){
		fprintf(stderr, "Error: could not not query GID for device #%d, port num #%d\n", 
					device_id, (int) port_num);
		port.ib_device_id = -1;
		return port;
	}

	port.gid = gid;

	// 2.) Initially query port to get attributes (lid, mtu, speed)
	//		- note that over course of system runtime these attributes may change!
	
	struct ibv_port_attr port_attr;

	ret = ibv_query_port(dev_ctx, port_num, &port_attr);
	if (ret != 0){
		fprintf(stderr, "Error: ibv_query_port failed for device #%d, port num #%d\n", 
							device_id, (int) port_num);
		port.ib_device_id = -1;
		return port;
	}

	// Save the import values from port attr
	port.state = port_attr.state;
	port.lid = port_attr.lid;
	port.active_mtu = port_attr.active_mtu;
	port.active_speed = port_attr.active_speed;
	// TODO: For multi-casting
	//			- not sure how this works, yet...
	port.sm_lid = port_attr.sm_lid;
	port.sm_sl = port_attr.sm_sl;

	// 3.) Deal with Partitioning
	//		- This is for either QoS or Security
	//		- SKIPPING FOR NOW


	// 4.) TODO: Obtain Local CPUs for the device associated with this port
	// - use the sysfs path within struct ibv_device -> ibdev_path
	//		- then read the file "local_cpus"
	//			- this file contains comma seperated uint32's representing bit-mask of local cpus
	

	return port;

}


// called from within init_self_node()
// At initialization time port -> ah is left blank and populated during setup_world_net()
Self_Port * init_all_ports(Self_Net * self_net, uint32_t num_ports, int num_endpoint_types, EndpointType * endpoint_types, bool * to_use_srq_by_type, int * num_qps_per_type) {

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
	uint8_t device_num_ports;

	for (int device_id = 0; device_id < num_ib_devices; device_id++){
		device_num_ports = (uint8_t) num_ports_per_dev[device_id];
		// in InfiniBand physical port numbers start at 1
		for (uint8_t phys_port_num = 1; phys_port_num < device_num_ports + 1; phys_port_num++){
			ports[cur_node_port_ind] = init_self_port(self_net, device_id, phys_port_num, cur_node_port_ind);

			// Within init_self_port, decided to report error as changing the device_id to be -1
			if (ports[cur_node_port_ind].ib_device_id == -1){
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



Self_Endpoint init_self_endpoint(Self_Net * self_net, Self_Port * port, int ib_device_id, EndpointType endpoint_type, CQ * send_cq, CQ * recv_cq, bool to_use_srq, uint32_t node_endpoint_ind) {

	int ret;

	Self_Endpoint endpoint;

	endpoint.node_endpoint_ind = node_endpoint_ind;
	endpoint.qp_port = port;
	endpoint.endpoint_type = endpoint_type;

	// 1.) Obtain the IBV structs needed to create qp
	struct ibv_context * ibv_dev_ctx = (self_net -> ibv_dev_ctxs)[ib_device_id];
	struct ibv_pd * ibv_pd = (self_net -> dev_pds)[ib_device_id];

	// Set ibv_srq to the device's srq if we want to share
	// (srq's are created by pd == per ibv_context == per device) 
	struct ibv_srq * ibv_srq = NULL;
	if (to_use_srq){
		ibv_srq = (self_net -> dev_srqs)[ib_device_id];
	}

	// 2.) Obtain the struct ibv_cq_ex completion queues we previously created
	struct ibv_cq_ex * send_ibv_cq = send_cq -> ibv_cq;
	struct ibv_cq_ex * recv_ibv_cq = recv_cq -> ibv_cq;


	// 3.) CREATE IBV_QP Struct: MAIN FORM OF COMMUNICATION!!!
	//		- holds pointers to pretty much all other ibv_structs as fields

	struct ibv_qp_init_attr_ex qp_attr;
	memset(&qp_attr, 0, sizeof(qp_attr));

	qp_attr.pd = ibv_pd; // Setting Protection Domain
	qp_attr.qp_type = IBV_QPT_UD;
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
		// decide for error checking to set endpoint.qp_port == NULL
		endpoint.qp_port = NULL;
		return endpoint;
	}

	endpoint.ibv_qp = ibv_qp;

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
		endpoint.qp_port = NULL;
		return endpoint;
	}
	
	
	// 3.) Retrieve and set qp_num to be conveniently accessible
	endpoint.qkey = qkey;
	endpoint.qp_num = ibv_qp -> qp_num;

	return endpoint;
}


Self_Endpoint * init_all_endpoints(Self_Net * self_net, uint32_t num_ports, Self_Port * ports, uint32_t num_endpoints, int num_endpoint_types, EndpointType * endpoint_types, bool * to_use_srq_by_type, int * num_qps_per_type){


	// 1.) Created packed array for all endpoints

	Self_Endpoint * endpoints = (Self_Endpoint *) malloc(num_endpoints * sizeof(Self_Endpoint));
	if (ports == NULL){
		fprintf(stderr, "Error: malloc failed for allocating ports container\n");
		return NULL;
	}

	// 2.) For each port create a set of endpoints

	Self_Port cur_port;
	int cur_ib_device_id;
	CQ_Collection * cur_cq_collection;
	CQ * cur_send_cq, *cur_recv_cq;

	uint32_t cur_node_endpoint_ind = 0;

	int endpoint_type_num_qps;
	bool to_use_srq;
	EndpointType cur_endpoint_type;

	for (uint32_t i = 0; i < num_ports; i++){
		cur_port = ports[i];
		cur_ib_device_id = cur_port.ib_device_id;
		cur_cq_collection = (self_net -> dev_cq_collections)[cur_ib_device_id];
		for (int j = 0; j < num_endpoint_types; j++){
			cur_send_cq = (cur_cq_collection -> send_cqs)[j];
			cur_recv_cq = (cur_cq_collection -> recv_cqs)[j];
			cur_endpoint_type = endpoint_types[j];
			to_use_srq = to_use_srq_by_type[j];
			endpoint_type_num_qps = num_qps_per_type[j];
			for (int k = 0; k < endpoint_type_num_qps; k++){

				endpoints[cur_node_endpoint_ind] = init_self_endpoint(self_net, &cur_port, cur_ib_device_id, cur_endpoint_type, cur_send_cq, cur_recv_cq, to_use_srq, cur_node_endpoint_ind);
				
				// Within init_self_endpoint, decided to report error as changing the qp_port to be NULL
				if (endpoints[cur_node_endpoint_ind].qp_port == NULL){
					fprintf(stderr, "Error: failed to initialize endpoint for port #%u, endpoint type ind #%d, endpoint num (within type) #%d\n", i, j, k);
					return NULL;
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
		num_ports += num_ports_per_dev[i];
	}

	Self_Port * ports = init_all_ports(self_net, num_ports, num_endpoint_types, endpoint_types, to_use_srq_by_type, num_qps_per_type);
	if (ports == NULL){
		fprintf(stderr, "Error: failed to initialize ports from init_self_node\n");
		return NULL;
	}

	

	// 2.) Create endpoints
	
	uint32_t total_qps_per_port = 0;
	for (int i = 0; i < num_endpoint_types; i++){
		total_qps_per_port += num_qps_per_type[i];
	}

	uint32_t num_endpoints = num_ports * total_qps_per_port;

	Self_Endpoint * endpoints = init_all_endpoints(self_net, num_ports, ports, num_endpoints, num_endpoint_types, endpoint_types, to_use_srq_by_type, num_qps_per_type);
	if (endpoints == NULL){
		fprintf(stderr, "Error: faile to initialize endpoints from init_self_node\n");
		return NULL;
	}


	// 3.) Set self_node

	self_node -> num_ports = num_ports;
	self_node -> ports = ports;
	self_node -> num_endpoints = num_ports * total_qps_per_port;
	self_node -> endpoints = endpoints;

	return self_node;
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

		num_ports_per_dev[i] = dev_attr.phys_port_cnt;
	}

	self_net -> num_ports_per_dev = num_ports_per_dev;

	// 4.) Create CQ_Collections for each device
	CQ_Collection ** dev_cq_collections = (CQ_Collection **) malloc(num_devices * sizeof(CQ_Collection *));
	if (dev_cq_collections == NULL){
		fprintf(stderr, "Error: malloc failed for allocating cq collection containers\n");
		return NULL;
	}

	for (int i = 0; i < num_devices; i++){
		dev_cq_collections[i] = init_cq_collection(ibv_dev_ctxs[i], i, num_endpoint_types, endpoint_types);
		if (dev_cq_collections[i] == NULL){
			fprintf(stderr, "Error: could not initialize qp collection\n");
			return NULL;
		}
	}

	self_net -> dev_cq_collections = dev_cq_collections;

	// 5.) Create SRQ for device that QPs can use depending on type
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
	
	// 6.) Create Self_Node for self which contains information about ports and Queue Pairs
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
