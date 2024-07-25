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

CQ * init_cq(struct ibv_context * ibv_dev_ctx, QueuePairUsageType qp_usage_type){

	CQ * cq = (CQ *) malloc(sizeof(CQ));
	if (cq == NULL){
		fprintf(stderr, "Error: malloc failed to allocate cq\n");
		return NULL;
	}

	// The CQ Usage Type is assoicated with a corresponding QP Usage Type
	// Meaning this CQ is used on QPs of a given type
	cq -> cq_usage_type = qp_usage_type;

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

CQ_Collection * init_cq_collection(struct ibv_context * ibv_dev_ctx, int device_id, int num_qp_types, QueuePairUsageType * qp_usage_types){

	CQ_Collection * cq_collection = (CQ_Collection *) malloc(sizeof(CQ_Collection));
	if (cq_collection == NULL){
		fprintf(stderr, "Error: malloc failed to allocate cq collection\n");
		return NULL;
	}

	cq_collection -> ib_device_id = device_id;

	// Have One CQ for each qp type per device
	// May want to have more...?
	CQ ** send_cqs = (CQ **) malloc(num_qp_types * sizeof(CQ *));
	CQ ** recv_cqs = (CQ **) malloc(num_qp_types * sizeof(CQ *));
	if ((send_cqs == NULL) || (recv_cqs == NULL)){
		fprintf(stderr, "Error: malloc failed to allocate cq containers\n");
		return NULL;
	}

	for (int i = 0; i < num_qp_types; i++){
		send_cqs[i] = init_cq(ibv_dev_ctx, qp_usage_types[i]);
		recv_cqs[i] = init_cq(ibv_dev_ctx, qp_usage_types[i]);
		if ((send_cqs[i] == NULL) || (recv_cqs[i] == NULL)){
			fprintf(stderr, "Error: failed to initialize cq for device #%d\n", device_id);
			return NULL;
		}
	}

	cq_collection -> send_cqs = send_cqs;
	cq_collection -> recv_cqs = recv_cqs;

	return cq_collection;
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


QP * init_qp(Self_Port * port, QueuePairUsageType qp_usage_type, struct ibv_context * ibv_dev_ctx, struct ibv_pd * ibv_pd, 
				struct ibv_cq_ex * send_ibv_cq, struct ibv_cq_ex * recv_ibv_cq, struct ibv_srq * ibv_srq){

	int ret;

	QP * qp = (QP *) malloc(sizeof(QP));
	if (qp == NULL){
		fprintf(stderr, "Error: malloc failed to allocate qp\n");
		return NULL;
	}

	qp -> qp_port = port;
	qp -> qp_usage_type = qp_usage_type;


	// 1.) CREATE IBV_QP Struct: MAIN FORM OF COMMUNICATION!!!
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
		return NULL;
	}

	qp -> ibv_qp = ibv_qp;

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
		return NULL;
	}
	
	
	// 3.) Retrieve and set qp_num to be conveniently accessible
	qp -> qp_num = ibv_qp -> qp_num;

	

	return qp;
}




QP_Collection * init_qp_collection(Self_Net * self_net, Self_Port * port, int device_id, 
								int num_qp_types, QueuePairUsageType * qp_usage_types, bool * to_use_srq_by_type, int * num_qps_per_type){


	QP_Collection * qp_collection = (QP_Collection *) malloc(sizeof(QP_Collection));
	if (qp_collection == NULL){
		fprintf(stderr, "Error: malloc failed to allocate qp collection\n");
		return NULL;
	}

	qp_collection -> ib_device_id = device_id;
	qp_collection -> num_qp_types = num_qp_types;
	qp_collection -> qp_usage_types = qp_usage_types;
	qp_collection -> to_use_srq_by_type = to_use_srq_by_type;
	qp_collection -> num_qps_per_type = num_qps_per_type;

	QP *** queue_pairs = (QP ***) malloc(sizeof(QP **));
	if (queue_pairs == NULL){
		fprintf(stderr, "Error: malloc failed to allocate queue pairs container\n");
		return NULL;
	}

	// 1.) Initializing IBV_QPs!

	// opened device context associated with this port
	struct ibv_context * ibv_dev_ctx = (self_net -> ibv_dev_ctxs)[device_id];
	
	struct ibv_pd * ibv_pd = (self_net -> dev_pds)[device_id];

	struct ibv_srq * dev_srq = (self_net -> dev_srqs)[device_id];

	

	QueuePairUsageType qp_usage_type;
	int num_qps;
	bool to_use_srq;

	// FOR NOW ASSUME QP TYPES AND CQ TYPES MAP 1-to-1!
	CQ_Collection * cq_collection = (self_net -> dev_cq_collections)[device_id];
	CQ ** send_cqs = cq_collection -> send_cqs;
	CQ ** recv_cqs = cq_collection -> recv_cqs;

	struct ibv_cq_ex * send_ibv_cq;
	struct ibv_cq_ex * recv_ibv_cq;

	struct ibv_srq * ibv_srq;

	for (int i = 0; i < num_qp_types; i++){
		num_qps = num_qps_per_type[i];

		queue_pairs[i] = (QP **) malloc(num_qps * sizeof(QP *));
		if (queue_pairs[i] == NULL){
			fprintf(stderr, "Error: malloc failed to allocated queue pairs array for type: %d\n", i);
			return NULL;
		}


		qp_usage_type = qp_usage_types[i];
		
		to_use_srq = to_use_srq_by_type[i];
		if (to_use_srq){
			ibv_srq = dev_srq;
		}
		else{
			ibv_srq = NULL;
		}

		// WOULD WANT TO CHANGE IF THIS QP TYPES ARE DIFFERENT THAN CQ TYPES!
		send_ibv_cq = send_cqs[i] -> ibv_cq;
		recv_ibv_cq = recv_cqs[i] -> ibv_cq;

		for (int cnt = 0; cnt < num_qps; cnt++){
			queue_pairs[i][cnt] = init_qp(port, qp_usage_type, ibv_dev_ctx, ibv_pd, send_ibv_cq, recv_ibv_cq, ibv_srq);
			if (queue_pairs[i][cnt] == NULL){
				fprintf(stderr, "Error: failed to init qp for type: %d, and cnt: %d\n", i, cnt);
				return NULL;
			}
		}
	}

	qp_collection -> queue_pairs = queue_pairs;

	return qp_collection;
}
 
// called from within init_self_port_container()
// upon port initialization QP_Collection is populated based on num_qp_types and num_qps_per_type

// if error set port.ib_device_id = -1
Self_Port init_self_port(Self_Net * self_net, int device_id, uint8_t port_num, uint32_t logical_port_ind,
					int num_qp_types, QueuePairUsageType * qp_usage_types, bool * to_use_srq_by_type, int * num_qps_per_type) {

	int ret;

	Self_Port port;
	port.ib_device_id = device_id;
	// the physical port num on specific IB device
	// needed in order to create address handles to this destination
	port.port_num = port_num;
	// index into Self_Node -> Ports
	port.logical_port_ind = logical_port_ind;

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
	port.sm_lid = port_attr.sm_lid;
	port.sm_sl = port_attr.sm_sl;

	// 3.) Deal with Partitioning
	//		- This is for either QoS or Security
	//		- SKIPPING FOR NOW


	// 4.) TODO: Obtain Local CPUs for the device associated with this port
	// - use the sysfs path within struct ibv_device -> ibdev_path
	//		- then read the file "local_cpus"
	//			- this file contains comma seperated uint32's representing bit-mask of local cpus
	

	// 5.) Setup QP collection!
	//		- Here we probably want a different data structure to maintain seperate lists for each type
	//		- But maybe we build this QP pool structure at a higher level (node-level) 
	//			that maintains data qp locks/free list

	// Within qp_collection init qp is called which actually generates qps
	// We will have a send + receive completition queue for each qp type
	QP_Collection * qp_collection = init_qp_collection(self_net, &port, device_id, num_qp_types, qp_usage_types, to_use_srq_by_type, num_qps_per_type);
	if (qp_collection == NULL){
		fprintf(stderr, "Error: failed to init qp_collection\n");
		port.ib_device_id = -1;
		return port;
	}

	port.qp_collection = qp_collection;

	return port;

}


// called from within init_self_node()
// At initialization time port -> ah is left blank and populated during setup_world_net()
Self_Port * init_self_port_container(Self_Net * self_net, int total_ports, uint64_t * active_port_bitmasks, int num_qp_types, QueuePairUsageType * qp_usage_types, bool * to_use_srq_by_type, int * num_qps_per_type) {

	// Obtain device info from Self_Net
	int num_ib_devices = self_net -> num_ib_devices;
	int * num_ports_per_dev = self_net -> num_ports_per_dev;

	// 1.) Create packed array for all ports
	
	Self_Port * ports = (Self_Port *) malloc(total_ports * sizeof(Self_Port *));
	if (ports == NULL){
		fprintf(stderr, "Error: malloc failed for allocating ports container\n");
		return NULL;
	}


	uint32_t cur_logical_port_ind = 0;

	// physical port start at num = 1...?
	// port 0 reserved for subnet manager...?
	uint8_t device_num_ports;

	int bitmask_arr_ind;
	int bit_ind;
	for (int device_id = 0; device_id < num_ib_devices; device_id++){
		device_num_ports = (uint8_t) num_ports_per_dev[device_id];
		// in InfiniBand physical port numbers start at 1
		for (uint8_t phys_port_num = 1; phys_port_num < device_num_ports + 1; phys_port_num++){
			ports[cur_logical_port_ind] = init_self_port(self_net, device_id, phys_port_num, cur_logical_port_ind, 
												num_qp_types, qp_usage_types, to_use_srq_by_type, num_qps_per_type);

			// Within init_self_port, decided to report error as changing the device_id to be -1
			if (ports[cur_logical_port_ind].ib_device_id == -1){
				fprintf(stderr, "Error: failed to initialize port for device #%d, phys port num #%d\n", 
							device_id, phys_port_num);
				return NULL;
			}

			// Creating port (& all QPs attached to it) succeeded

			// If port is active, set the value in bitmask
			if (ports[cur_logical_port_ind].state == IBV_PORT_ACTIVE){
				bitmask_arr_ind = cur_logical_port_ind / 64;
				bit_ind = cur_logical_port_ind % 64;
				// set the bit to 1
				active_port_bitmasks[bitmask_arr_ind] |= (1 << bit_ind);
			}

			cur_logical_port_ind += 1;
		}
	}

	return ports;
}


// called from within init_self_net()
// However this data structure (and not self_nets) will be shared with other nodes
// (and created as part of each node's world_net)
Self_Node * init_self_node(Self_Net * self_net, int num_qp_types, QueuePairUsageType * qp_usage_types, bool * to_use_srq_by_type, int * num_qps_per_type) {

	Self_Node * self_node = (Self_Node *) malloc(sizeof(Self_Node));
	if (self_node == NULL){
		fprintf(stderr, "Error: malloc failed for initializing node net\n");
		return NULL;
	}

	int num_ib_devices = self_net -> num_ib_devices;
	int * num_ports_per_dev = self_net -> num_ports_per_dev;
	int total_ports = 0;
	for (int i = 0; i < num_ib_devices; i++){
		total_ports += num_ports_per_dev[i];
	}

	self_node -> total_ports = total_ports;

	int total_qps_per_port = 0;
	for (int i = 0; i < num_qp_types; i++){
		total_qps_per_port += num_qps_per_type[i];
	}

	self_node -> total_qps = total_ports * total_qps_per_port;

	int bitmask_arr_size = ceil((float) total_ports / 64.0f);

	uint64_t * active_port_bitmasks = (uint64_t *) malloc(bitmask_arr_size * sizeof(uint64_t));
	// initially set 0 active ports
	for (int i = 0; i < bitmask_arr_size; i++){
		active_port_bitmasks[i] = 0;
	}


	Self_Port * ports = init_self_port_container(self_net, total_ports, active_port_bitmasks, num_qp_types, qp_usage_types, to_use_srq_by_type, num_qps_per_type);
	if (ports == NULL){
		fprintf(stderr, "Error: failed to initialize ports from init_self_node\n");
		return NULL;
	}

	self_node -> ports = ports;
	
	// this array was set within init_self_port_container
	self_node -> active_port_bitmasks = active_port_bitmasks;

	return self_node;
}


// responsible for opening ib_devices/getting contexts and allocating ib structs
// also responsible for intializing a self_node for itself that it will share with others
Self_Net * init_self_net(int num_qp_types, QueuePairUsageType * qp_usage_types, bool * to_use_srq_by_type, int * num_qps_per_type, char * ip_addr){

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
		dev_cq_collections[i] = init_cq_collection(ibv_dev_ctxs[i], i, num_qp_types, qp_usage_types);
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
	Self_Node * self_node = init_self_node(self_net, num_qp_types, qp_usage_types, to_use_srq_by_type, num_qps_per_type);
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
	if (ip_addr != NULL){
		struct in_addr in_addr;
		ret = inet_aton(ip_addr, &in_addr);
		// Note: inet_aton returns 0 on error
		if (ret == 0){
			fprintf(stderr, "Error: invalid IPv4 address of: %s\n", ip_addr);
			return NULL;
		}
		self_net -> s_addr = (uint32_t) (in_addr.s_addr);
	}
	else{
		self_net -> s_addr = 0;
	}
	

	return self_net;

}

Self_Net * default_worker_config_init_self_net(char * ip_addr) {

	int num_qp_types = 3;
	QueuePairUsageType qp_usage_types[3] = {CONTROL_QP, DATA_QP, MASTER_QP};
	bool to_use_srq_by_type[3] = {true, false, true};
	int num_qps_per_type[3] = {1, 1, 1};

	// returns NULL upon error
	return init_self_net(num_qp_types, qp_usage_types, to_use_srq_by_type, num_qps_per_type, ip_addr);
}


Self_Net * default_master_config_init_self_net(char * ip_addr) {

	int num_qp_types = 1;
	QueuePairUsageType qp_usage_types[1] = {MASTER_QP};
	bool to_use_srq_by_type[1] = {true};
	int num_qps_per_type[1] = {1};

	// returns NULL upon error
	return init_self_net(num_qp_types, qp_usage_types, to_use_srq_by_type, num_qps_per_type, ip_addr);
}
