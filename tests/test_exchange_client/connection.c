#include "connection.h"
#include <errno.h>

#define TO_PRINT 0

// GUIDED BY: 
//  - Roland Drier: https://www.youtube.com/watch?v=JtT0uTtn2EA"
//  - Mellanox/Nvidia Docs: https://docs.nvidia.com/networking/display/rdmaawareprogrammingv17/programming+examples+using+ibv+verbs#src-34256583_ProgrammingExamplesUsingIBVVerbs-Main"
//      - Jason Gunthorpe


// returns id of new connection added to all_connections, -1 if error
// If Passive side (server side) => connection_server is populated, and connection_client -> cm_id is rdma_accept
// If active side (client side) => connection_server is NULL and connection_client -> cm_id is sent to rdma_connect
int init_connection(RDMAConnectionType connection_type, ConnectionServer * conn_server, ConnectionClient * conn_client, struct ibv_qp * server_qp, struct ibv_qp * client_qp, struct rdma_conn_param * conn_params, Connection ** ret_connection){
    
    int ret;
    
    Connection * connection = (Connection *) malloc(sizeof(Connection));
    if (connection == NULL){
        fprintf(stderr, "Error: malloc failed when trying to init connection\n");
        return -1;
    }
    
    int is_server = (conn_server -> cm_id != NULL);

    // 1.) Retrieve Verbs Context
    //
    // If server side, retrieve from server
    // If passive side, retrieve from conn_cm_id
    
    struct ibv_context *verbs_context;

    if (is_server){
        verbs_context = conn_server -> cm_id -> verbs;
    }
    else{
        verbs_context = conn_client -> cm_id -> verbs;
    }
    
        // 2.) Create Protection Domain
    struct ibv_pd* pd = ibv_alloc_pd(verbs_context);
    if (pd == NULL) {
        fprintf(stderr, "Error: could not allocate pd\n");
        return -1;
    }
    
    connection -> pd = pd;


    // 3.) Create Completition Queue
    int num_cq_entries = 100;
    /* "The pointer cq_context will be used to set user context pointer of the cq structure" */
    void * cq_context = connection;
    
    struct ibv_cq_init_attr_ex cq_attr;
    memset(&cq_attr, 0, sizeof(cq_attr));
    cq_attr.cqe = num_cq_entries;
    cq_attr.cq_context = cq_context;
    struct ibv_cq_ex* cq =  ibv_create_cq_ex(verbs_context, &cq_attr);

    connection -> cq = cq;


    // 4.) (Optionally) Create Queue Pair
    if ((is_server) && (server_qp != NULL)) {
        conn_client -> cm_id -> qp = server_qp;
    }
    else if ((!is_server) && (client_qp != NULL)) {
        conn_client -> cm_id -> qp = client_qp;
    }

    else {

        enum ibv_qp_type qp_type;
        if (connection_type == RDMA_RC){
            qp_type = IBV_QPT_RC;
        }
        if (connection_type == RDMA_UD){
            qp_type = IBV_QPT_UD;
        }

        struct ibv_qp_init_attr_ex qp_attr;
        memset(&qp_attr, 0, sizeof(qp_attr));

        qp_attr.pd = pd; // Setting Protection Domain
        qp_attr.qp_type = qp_type; // Using Reliable-Connection
        qp_attr.sq_sig_all = 1;       // if not set 0, all work requests submitted to SQ will always generate a Work Completion.
        qp_attr.send_cq = ibv_cq_ex_to_cq(cq);         // completion queue can be shared or you can use distinct completion queues.
        qp_attr.recv_cq = ibv_cq_ex_to_cq(cq);         // completion queue can be shared or you can use distinct completion queues.
        qp_attr.cap.max_send_wr = 10;  // increase if you want to keep more send work requests in the SQ.
        qp_attr.cap.max_recv_wr = 10;  // increase if you want to keep more receive work requests in the RQ.
        qp_attr.cap.max_send_sge = 1; // increase if you allow send work requests to have multiple scatter gather entry (SGE).
        qp_attr.cap.max_recv_sge = 1; // increase if you allow receive work requests to have multiple scatter gather entry (SGE).
        //qp_attr.cap.max_inline_data = 1000;
        uint64_t send_ops_flags;
        if (connection_type == RDMA_RC){
            send_ops_flags = IBV_QP_EX_WITH_RDMA_WRITE | IBV_QP_EX_WITH_RDMA_READ | IBV_QP_EX_WITH_SEND |
                                    IBV_QP_EX_WITH_ATOMIC_CMP_AND_SWP | IBV_QP_EX_WITH_ATOMIC_FETCH_AND_ADD;
        }
        // UD queue pairs can only do Sends, not RDMA or Atomics
        else{
            send_ops_flags = IBV_QP_EX_WITH_SEND;
        }
        qp_attr.send_ops_flags |= send_ops_flags;
        qp_attr.comp_mask |= IBV_QP_INIT_ATTR_SEND_OPS_FLAGS | IBV_QP_INIT_ATTR_PD;

        // Actually use librdmacm to create queue pair and attach to cm_id
        ret = rdma_create_qp_ex(conn_client -> cm_id, &qp_attr);
        if (ret != 0){
            fprintf(stderr, "Error: could not create queue pair\n");
            return -1;
        }
    }

    /* IGNORING MAILBOX AND DOORBELL STUFF FOR NOW!

    // 5.) Setup local mailbox
    
    // set mailbox to 1GB for now...
    // ib verbs lengths are uint32_t
    uint32_t mailbox_memory_size = 1 << 30;
    ret = setup_local_mailbox(connection, mailbox_memory_size);
    if (ret != 0){
        fprintf(stderr, "Error: could not set up local mailbox\n");
        return -1;
    }

    // 6.) Post the receive request from other side to be able to gather addr/rkey/size info on their mailbox
    RemoteMailbox * remote_mailbox = (RemoteMailbox *) malloc(sizeof(RemoteMailbox));
    struct ibv_mr * remote_mailbox_mr;
    ret = register_virt_memory(connection -> pd, (void *) remote_mailbox, sizeof(RemoteMailbox), &remote_mailbox_mr);
    if (ret != 0){
            fprintf(stderr, "Error: could not register remote mailbox receive buffer with ib verbs\n");
            return -1;
    }
    connection -> dest_mailbox = remote_mailbox;
    connection -> remote_mailbox_local_mr = remote_mailbox_mr;


    uint64_t remote_id;
    if (is_server){
            remote_id = conn_client -> id;
    }
    else{
            remote_id = conn_server -> id;
    }
    
    // both sides use the queue pairs from conn_client -> cm_id
    ret = post_recv_work_request(conn_client -> cm_id, remote_mailbox_mr, remote_id);
    if (ret != 0){
        fprintf(stderr, "Error: could not post receive work request to handle the other side sending mailbox mr info\n");
        return -1;
    }

    // 7.) Include memory region for receiving remote data as part of compare & swaps (orig data before compare op)
    void * remote_lock_val_mem = malloc(sizeof(uint64_t));
    struct ibv_mr * remote_lock_val_mr;
    ret = register_virt_memory(connection -> pd, remote_lock_val_mem, sizeof(uint64_t), &remote_lock_val_mr);
    if (ret != 0){
        fprintf(stderr, "Error: could not register remote lock val mr\n");
        return NULL;
    }

    connection -> remote_lock_val_mem = remote_lock_val_mem;
    connection -> remote_lock_val_mr = remote_lock_val_mr;
    
    */


    // 8.) Connect / Accept Conection
    // If active-side (client) then connect
    // If passive-side (server) then accept


    // // for this simple example, do doing any sync with posting send/recv so just retry a lot...
    // conn_params.rnr_retry_count=100;
    // // ensure that connection can deal with RDMA read and atomic
    // //
    // // The maximum number of outstanding RDMA read and atomic 
    // // operations that the local side will accept from the remote side.
    // // must be <= device attribute of max_qp_rd_atom
    // conn_params.responder_resources = 1;

    // // The maximum number of outstanding RDMA read and atomic
    // // operations that the local side will have to the remote side.
    // // must be <= device attribute of max_qp_init_rd_atom
    // conn_params.initiator_depth=1;
    
    if (is_server){
        ret = rdma_accept(conn_client -> cm_id, conn_params);
        if (ret != 0){
            fprintf(stderr, "Error: rdma_accept failed\n");
            return -1;
        }
    }
    else{
        ret = rdma_connect(conn_client -> cm_id, conn_params);
        if (ret != 0){
            fprintf(stderr, "Error: could not do rdma_connect\n");
            return -1;
        }
    }

    // set the cm_id from within the connection struct to retrieve queue pairs
    // both active and passive used the client cm_id (sever-side clien cm_id is retrieved through RDMA CM event)
    connection -> cm_id = conn_client -> cm_id;
    connection -> connection_type = connection_type;

    // POPULATE THE REST OF THE CONNECTION FIELDS!
    
    uint64_t src_id, dest_id;
    char *src_ip, *src_port, *dest_ip, *dest_port;
    if (is_server){
        src_id = conn_server -> id;
        src_ip = strdup(conn_server -> ip);
        src_port = strdup(conn_server -> port);
        dest_id = conn_client -> id;
        dest_ip = strdup(conn_client -> ip);
        dest_port = strdup(conn_client -> port);
    }
    else{
        src_id = conn_client -> id;
        src_ip = strdup(conn_client -> ip);
        src_port = strdup(conn_client -> port);
        dest_id = conn_server -> id;
        dest_ip = strdup(conn_server -> ip);
        dest_port = strdup(conn_server -> port);
    }
    
    connection -> src_id = src_id;
    connection -> dest_id = dest_id;
    connection -> src_ip = src_ip;
    connection -> src_port = src_port;
    connection -> dest_ip = dest_ip;
    connection -> dest_port = dest_port;

    *ret_connection = connection;

    return 0;
}


int establish_connection(Connection * connection, struct rdma_cm_event * event, RDMAConnectionType connection_type){
    uint32_t remote_qpn = 0;
    uint32_t remote_qkey = 0;
    struct ibv_ah * ah = NULL;
    if (connection_type == RDMA_UD){
        remote_qpn = (event -> param).ud.qp_num;
        remote_qkey = (event -> param).ud.qkey;
        ah = ibv_create_ah(connection -> pd, &event->param.ud.ah_attr);
        if (ah == NULL){
            fprintf(stderr, "Error: could not create ah when establishing connection\n");
            return -1;
        }
    }
    connection -> remote_qpn = remote_qpn;
    connection -> remote_qkey = remote_qkey;
    connection -> ah = ah;

    return 0;
}





// Libibverbs Header: https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/verbs.h#L972
// Librdmacm Header: https://github.com/ofiwg/librdmacm/blob/master/include/rdma/rdma_cma.h#L50

// Ensure that conn_server and conn_client are allocated/populated based on some known configuration of IDs/IPs/active vs. passive
// will return the index of added connection upon success, -1 upon error


// AFTER SUCCESSFUL CONNECTION NEED TO SETUP MAILBOXES!
// ASSUME THAT UPON CONNECTION SETUP THAT BOTH SIDES POST A RECEIVE WORK REQUEST THAT WILL TRANSFER MEMORY REGION DATA!
int handle_connection_events(RDMAConnectionType connection_type, ConnectionServer * conn_server, ConnectionClient * conn_client, struct rdma_event_channel * channel, 
                                struct ibv_qp * server_qp, struct ibv_qp * client_qp, struct rdma_addrinfo * rai, Connection ** ret_connection){
    
    int ret;
    struct rdma_cm_event * event;
    int timeout_ms = 2000;

    int is_done = 0;

    printf("Waiting for CM events...\n");


    struct rdma_conn_param conn_params;
    memset(&conn_params, 0, sizeof(conn_params));

    struct ibv_port_attr port_attr;

    while (!is_done){

        // WAIT FOR EVENT 

        ret = rdma_get_cm_event(channel, &event);
        if (ret != 0){
            fprintf(stderr, "Error: could not get cm event\n");
            return -1;
        }

        // DISPATCH EVENT

        switch(event -> event){
            case RDMA_CM_EVENT_ADDR_RESOLVED:
                /* call rdma_resolve_route() */
                // Active Side (non-blocking call)
                printf("Saw addr_resolved event\n");
                ret = rdma_resolve_route(conn_client->cm_id, timeout_ms);
                break;
            case RDMA_CM_EVENT_ROUTE_RESOLVED:
                /* call rdma_create_qp() and rdma_connect() */
                // Active Side
                // Here conn_server -> cm_id == NULL
                printf("Saw route_resolved event\n");
                ret = init_connection(connection_type, conn_server, conn_client, server_qp, client_qp, &conn_params, ret_connection);
                break;
            case RDMA_CM_EVENT_CONNECT_REQUEST:
                // Passive / Server side
		        // Here conn_client -> cm_id is not-populated so ensure to do so
                conn_client -> cm_id = event -> id;
                printf("Saw connect_request event\n");
                ret = init_connection(connection_type, conn_server, conn_client, server_qp, client_qp, &conn_params, ret_connection);
                break;
            case RDMA_CM_EVENT_ESTABLISHED:
                /* can start communication! Returning from this connection handling loop */
                // Both Sides for RC, only Active side for UD
                // set the loop to be done...
                printf("Saw established\n");
                if (connection_type == RDMA_UD){
                    ret = establish_connection(*ret_connection, event, connection_type);
                }
                is_done = 1;
                break;
            case RDMA_CM_EVENT_UNREACHABLE:
                /* should error handle */
                printf("Saw unreachable\n");
                break;
            case RDMA_CM_EVENT_REJECTED:
                /* should error handle */
                printf("Saw rejected. Status: %d\n", event -> status);
                break;
            case RDMA_CM_EVENT_DISCONNECTED:
                /* should handle disconnection */
                printf("Saw disconnected\n");
                break;
            default:
                printf("Saw rdma_cm_event_type = %d\n", event -> event);
                break;
        }

        // THERE WAS A PROBLEM IN DISPACT, RETURN
        if (ret != 0){
            fprintf(stderr, "Error: could not handle event\n");
            return -1;
        }

        // free resources tied to event
        rdma_ack_cm_event(event);



    }

    printf("Connection Established!\n");

    return 0;
}


// First one side posts a receive work request, then the other side posts a send work request!


/* FOR NOW DROPPED THE SGE AND NUM_SGE ARGS AND ASSUMEING 1 SGE ENTRY*/

// Receive work request = ready for incoming message & when it arrives want local RDMA stack to deliver data to specific buffer
//  - scatter list is for placing dating into dis-continguous buffers
//  - work request id is copied into completion queue entry for understanding what things have been done

// if sge == NULL assume that we will have 1 entry going to specific address

// The receive work request that is posted is waiting until a matching send work request (by work request id) comes in
int post_recv_work_request(struct rdma_cm_id * cm_id, struct ibv_mr * mr, uint64_t wr_id){
    
    int ret;

    struct ibv_recv_wr wr;
    memset(&wr, 0, sizeof(struct ibv_recv_wr));

    struct ibv_recv_wr * bad_wr = NULL;

    struct ibv_sge my_sge;
    memset(&my_sge, 0, sizeof(struct ibv_sge));
    my_sge.addr = (uint64_t) mr -> addr;
    my_sge.length = (uint32_t) mr -> length;
    my_sge.lkey = mr -> lkey;
    int num_sge = 1;
  
    wr.sg_list = &my_sge;
    wr.num_sge = num_sge;
    wr.wr_id = wr_id;
    wr.next = NULL;


    ret = ibv_post_recv(cm_id -> qp, &wr, &bad_wr);
    if (ret != 0){
        fprintf(stderr, "Error: could note post receive work request\n");
        return -1;
    }

    return 0;
}



// Posting a send work request starts immediately and consumes a receive'd work request
int post_send_work_request(struct rdma_cm_id * cm_id, struct ibv_mr * mr, uint64_t wr_id) {

    int ret;

    struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(cm_id -> qp);
    ibv_wr_start(qp_ex);

    qp_ex -> wr_id = wr_id;
    qp_ex -> wr_flags = 0; /* ordering/fencing etc. */
	
    // PEFORM Send
    ibv_wr_send(qp_ex);

    ibv_wr_set_sge(qp_ex, mr -> lkey, (uint64_t) mr -> addr, (uint32_t) mr -> length);
    /* can send discontiguous buffers by using ibv_wr_set_sge_list() */
    ret = ibv_wr_complete(qp_ex);

    if (ret != 0){
        fprintf(stderr, "Error: issue with ibv_wr_complete\n");
        return -1;
    }

    return 0;
}


int post_rdma_read_work_request(struct rdma_cm_id * cm_id, struct ibv_mr * mr, uint64_t wr_id, uint32_t rkey, uint64_t remote_addr){

	int ret;

	struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(cm_id -> qp);
        ibv_wr_start(qp_ex);

        qp_ex -> wr_id = wr_id;
        qp_ex -> wr_flags = 0; /* ordering/fencing etc. */

        // PERFORM RDMA Read
        ibv_wr_rdma_read(qp_ex, rkey, remote_addr);

	// read from remote_addr into this mr
        ibv_wr_set_sge(qp_ex, mr -> lkey, (uint64_t) mr -> addr, (uint32_t) mr -> length);
        /* can send discontiguous buffers by using ibv_wr_set_sge_list() */
        ret = ibv_wr_complete(qp_ex);

        if (ret != 0){
                fprintf(stderr, "Error: issue with ibv_wr_complete\n");
                return -1;
        }

        return 0;


}

int post_rdma_write_work_request(struct rdma_cm_id * cm_id, struct ibv_mr * mr, uint64_t wr_id, uint32_t rkey, uint64_t remote_addr){

        int ret;

        struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(cm_id -> qp);
        ibv_wr_start(qp_ex);

        qp_ex -> wr_id = wr_id;
        qp_ex -> wr_flags = 0; /* ordering/fencing etc. */

        // PERFORM RDMA Write
        ibv_wr_rdma_write(qp_ex, rkey, remote_addr);

	// write to remote_addr from this MR
        ibv_wr_set_sge(qp_ex, mr -> lkey, (uint64_t) mr -> addr, (uint32_t) mr -> length);
        /* can send discontiguous buffers by using ibv_wr_set_sge_list() */
        ret = ibv_wr_complete(qp_ex);

        if (ret != 0){
                fprintf(stderr, "Error: issue with ibv_wr_complete\n");
                return -1;
        }

        return 0;


}


// FROM : https://www.rdmamojo.com/2013/01/26/ibv_post_send/

int post_cmp_swap_send_work_request(struct rdma_cm_id * cm_id, struct ibv_mr * mr, uint64_t wr_id, uint32_t rkey, uint64_t remote_addr, uint64_t compare_val, uint64_t swap_val) {

	int ret;

	// value of original data (remote side) before compare is being written to mr 
	/* non qp-ex way of constucting WR
	
	struct ibv_sge sg;
        struct ibv_send_wr wr;
        struct ibv_send_wr *bad_wr;

	memset(&sg, 0, sizeof(sg));
	sg.addr = (uint64_t) mr -> addr;
	sg.length = mr -> length;
	sg.lkey = mr -> lkey;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = wr_id;
	wr.sg_list = &sg;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
	wr.send_flags = IBV_SEND_SIGNALED;
	
	wr.wr.atomic.remote_addr = remote_addr;
	wr.wr.atomic.rkey = rkey;
	wr.wr.atomic.compare_add = compare_val;
	wr.wr.atomic.swap = swap_val;	
	ret = ibv_post_send(qp, &wr, &bad_wr);
	if (ret != 0){
		fprintf(stderr, "Error: could not post compare and swap send work request\n");
		return -1;
	}
	*/

	struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(cm_id -> qp);
        ibv_wr_start(qp_ex);
	
	qp_ex -> wr_id = wr_id;
    	qp_ex -> wr_flags = 0; /* ordering/fencing etc. */
	
	// PERFORM Compare and Swap 
	ibv_wr_atomic_cmp_swp(qp_ex, rkey, remote_addr, compare_val, swap_val);

	ibv_wr_set_sge(qp_ex, mr -> lkey, (uint64_t) mr -> addr, (uint32_t) mr -> length);
    	/* can send discontiguous buffers by using ibv_wr_set_sge_list() */
    	ret = ibv_wr_complete(qp_ex);

    	if (ret != 0){
        	fprintf(stderr, "Error: issue with ibv_wr_complete\n");
        	return -1;
    	}

	return 0;


}

int block_for_wr_comp(struct ibv_cq_ex * cq, uint64_t target_wr_id){

	int ret;

	struct ibv_poll_cq_attr poll_qp_attr = {};

	ret = ibv_start_poll(cq, &poll_qp_attr);

    	// If Error after start, do not call "end_poll"
    	if ((ret != 0) && (ret != ENOENT)){
        	fprintf(stderr, "Error: could not start poll for completition queue\n");
        	return -1;
    	}

	// if ret = 0, then ibv_start_poll already consumed an item
    	int seen_new_completition;

	uint64_t wr_id;
	enum ibv_wc_status status;

	while (1) {
		
		// return is 0 if a new item was cosumed, otherwise it equals ENOENT
        	if (ret == 0){
            		seen_new_completition = 1;
        	}
        	else{
            		seen_new_completition = 0;
        	}

        	// Consume the completed work request
        	wr_id = cq -> wr_id;
        	status = cq -> status;

		if (seen_new_completition){
			printf("Saw completion of wr_id = %ld\n\tStatus: %d\n", wr_id, status);
            		if (status != IBV_WC_SUCCESS){
                		fprintf(stderr, "Error: work request id %ld had error\n", wr_id);
            		}
			
			if (wr_id == target_wr_id){
				printf("Target wr id found. Exiting poll\n");
				break;
			}
		}

		// Check for next completed work request...
        	ret = ibv_next_poll(cq);

        	if ((ret != 0) && (ret != ENOENT)){
            		// If Error after next, call "end_poll"
            		ibv_end_poll(cq);
            		fprintf(stderr, "Error: could not do next poll for completition queue\n");
            		return -1;
        	}
	}

	ibv_end_poll(cq);
   	return 0;
}


// Docs: "https://man7.org/linux/man-pages/man3/ibv_create_cq_ex.3.html"
int poll_cq(struct ibv_cq_ex * cq, uint64_t duration_ns) {

    int ret;

    struct ibv_poll_cq_attr poll_qp_attr = {};

    struct timespec start, cur;
    uint64_t timestamp_start, timestamp_cur;

    clock_gettime(CLOCK_REALTIME, &start);
    timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;

    ret = ibv_start_poll(cq, &poll_qp_attr);

    // If Error after start, do not call "end_poll"
    if ((ret != 0) && (ret != ENOENT)){
        fprintf(stderr, "Error: could not start poll for completition queue\n");
        return -1;
    }

    // if ret = 0, then ibv_start_poll already consumed an item
    int seen_new_completition;

    int is_done = 0;
    
    enum ibv_wc_status status;
    uint64_t wr_id;
    while (!is_done){

        // return is 0 if a new item was cosumed, otherwise it equals ENOENT
        if (ret == 0){
            seen_new_completition = 1;
        }
        else{
            seen_new_completition = 0;
        }
        
        // Consume the completed work request
        wr_id = cq -> wr_id;
        status = cq -> status;
        // other fields as well...
        if (seen_new_completition){
            /* DO SOMETHING WITH wr_id! */
            printf("Saw completion of wr_id = %ld\n\tStatus: %d\n", wr_id, status);

            if (status != IBV_WC_SUCCESS){
                fprintf(stderr, "Error: work request id %ld had error\n", wr_id);
                // DO ERROR HANDLING HERE!
            }
        }

        // Check for next completed work request...
        ret = ibv_next_poll(cq);

        if ((ret != 0) && (ret != ENOENT)){
            // If Error after next, call "end_poll"
            ibv_end_poll(cq);
            fprintf(stderr, "Error: could not do next poll for completition queue\n");
            return -1;
        }

        // See if the elapsed poll time is greater than what the duration was set to, if so done
        clock_gettime(CLOCK_REALTIME, &cur);
        timestamp_cur = cur.tv_sec * 1e9 + cur.tv_nsec;
        if (timestamp_cur - timestamp_start > duration_ns){
            is_done = 1;
        }
    }

    ibv_end_poll(cq);

    return 0;
}


// assume addr has already been allocated with size size_bytes
int register_virt_memory(struct ibv_pd * pd, void * addr, size_t size_bytes, struct ibv_mr ** ret_mr){

    // ALSO CONSIDER:
    // IBV_ACCESS_HUGETLB & IBV_ACCESS_ON_DEMAND
    enum ibv_access_flags access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC;

    struct ibv_mr * mr = ibv_reg_mr(pd, addr, size_bytes, access_flags);
    if (mr == NULL){
        fprintf(stderr, "Error: ibv_reg_mr failed in get_system_memory\n");
        return -1;
    }

    *ret_mr = mr;

    return 0;
}


int register_dmabuf_memory(struct ibv_pd * pd, int fd, size_t size_bytes, uint64_t offset, uint64_t iova, struct ibv_mr ** ret_mr){

    // ALSO CONSIDER:
    // IBV_ACCESS_HUGETLB & IBV_ACCESS_ON_DEMAND
    enum ibv_access_flags access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC ;

    struct ibv_mr * mr = ibv_reg_dmabuf_mr(pd, offset, size_bytes, iova, fd, access_flags);
    if (mr == NULL){
        fprintf(stderr, "Error: ibv_reg_mr failed in register_dmabuf_memory\n");
        return -1;
    }

    *ret_mr = mr;

    return 0;
}

// IGNORING MAILBOX STUFF FOR NOW...

// int setup_local_mailbox(Connection * connection, uint32_t memory_size) {

// 	// Ensure to allocate enough space for mailbox 'header' metadata + memory
// 	Mailbox * mailbox = (Mailbox *) malloc(sizeof(Mailbox) + memory_size);
// 	if (mailbox == NULL){
// 		fprintf(stderr, "Error: malloc failed in setup local mailbox\n");
// 		return -1;
// 	}

// 	mailbox -> incoming_data_lock = 0;
// 	mailbox -> is_metadata_set = 0;
//        	mailbox -> is_recv_posted = 0;
// 	TransferInit transfer_init;
// 	memset(&transfer_init, 0, sizeof(TransferInit));
// 	mailbox -> transfer_init = transfer_init;
// 	mailbox -> memory_size = memory_size;
// 	mailbox -> memory = (void *) mailbox + sizeof(Mailbox);

// 	// Set the local mailbox region
// 	connection -> local_mailbox  = mailbox;

// 	// Register mailbox with IB verbs to get addr and rkey to send to other side of connection
// 	struct ibv_pd * pd = connection -> pd;
// 	size_t mailbox_size = sizeof(Mailbox) + memory_size;
// 	printf("Memory size: %ld\n", memory_size);
// 	printf("Mailbox size: %ld\n", mailbox_size);

// 	// IBV_ACCESS_HUGETLB & IBV_ACCESS_ON_DEMAND
//     	enum ibv_access_flags access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC;

// 	struct ibv_mr * mr = ibv_reg_mr(pd, (void *) mailbox, mailbox_size, access_flags);
// 	if (mr == NULL){
//         	fprintf(stderr, "Error: ibv_reg_mr failed in setup mailbox\n");
//         	return -1;
//     	}

// 	connection -> src_mr_mailbox = mr;

// 	return 0;
// }


// BLOCKS UNTIL CONNECTION IS SET UP!
// due to "handle_connection_events"
int setup_connection(RDMAConnectionType connection_type, int is_server, uint64_t server_id, char * server_ip, char * server_port, struct ibv_qp * server_qp, 
                        uint64_t client_id, char * client_ip, char * client_port, struct ibv_qp * client_qp, Connection ** ret_connection){

    int ret;

    // 1.) Create ConnectionServer and ConnectionClient
    ConnectionServer * conn_server = (ConnectionServer *) malloc(sizeof(ConnectionServer));
    ConnectionClient * conn_client = (ConnectionClient *) malloc(sizeof(ConnectionClient));
    if ((conn_server == NULL) || (conn_client == NULL)){
	   fprintf(stderr, "Error: malloc failed when creating conn_server and conn_client\n");
	   return -1;	
    }
	
    // SETTING ALL VALUES EXCEPT struct rdma_cm_id *
    // if server then conn_server will be overwritten with value
    // if client then conn_client will be overwritten with value
    conn_server -> id = server_id;
    conn_server -> ip = server_ip;
    conn_server -> port = server_port;
    conn_server -> cm_id = NULL;

    conn_client -> id = client_id;
    conn_client -> ip = client_ip;
    conn_client -> port = client_port;
    conn_client -> cm_id = NULL;

    // 2.) Initialize event channel
    struct rdma_event_channel * channel = rdma_create_event_channel();
    if (channel == NULL){
        fprintf(stderr, "Error: could not create rdma event channel, exiting...\n");
        return -1;
    }

    // 3.) Deal with RDMA IP addressing with librdmacm

    struct rdma_addrinfo hints;
    struct rdma_addrinfo *rai;

    memset(&hints, 0, sizeof(struct rdma_addrinfo));
    
    int port_space;

    // could have also used
    if (connection_type == RDMA_RC){
        port_space = RDMA_PS_TCP;
    }
    else if (connection_type == RDMA_UD){
        port_space = RDMA_PS_UDP;
    }
    else{
        fprintf(stderr, "Error: connection type not available\n");
        return -1;
    }

    hints.ai_port_space = port_space;

    if (is_server){
        hints.ai_flags = RAI_PASSIVE;
    }

    ret = rdma_getaddrinfo(server_ip, server_port, &hints, &rai);
    if (ret != 0){
        fprintf(stderr, "Error: could not do rdma_getaddrinfo, exiting...\n");
        return -1;
    }

    // 3.) Connection establishment
    if (is_server) {
        
        // a.) create CM ID
        ret = rdma_create_id(channel, &conn_server -> cm_id, conn_server, port_space);
        if (ret != 0){
            fprintf(stderr, "Error: could not create cm_id, exiting...\n");
            return -1;
        }

        // b.) Bind CM ID to local addr
        ret = rdma_bind_addr(conn_server -> cm_id, rai->ai_src_addr);
        if (ret != 0){
            fprintf(stderr, "Error: could not bind addr, exiting...\n");
            return -1;
        }

        // c.) Actually listen
        int backlog = 0; // not sure what this does...
        ret = rdma_listen(conn_server -> cm_id, backlog);
        if (ret != 0){
            fprintf(stderr, "Error: could not do rdma_listen, exiting...\n");
            return -1;
        }
    }
    // ACTIVE SIDE
    else{

        // a.) create CM ID
        ret = rdma_create_id(channel, &conn_client -> cm_id, conn_client, port_space);
        if (ret != 0){
            fprintf(stderr, "Error: could not create cm_id, exiting...\n");
            return -1;
        }

        
        // b.) Resolve remote address and bind to the passive (= server) side
        int timeout_ms = 20000;

        // NON-BLOCKING CALL!
        ret = rdma_resolve_addr(conn_client -> cm_id, rai->ai_src_addr, rai->ai_dst_addr, timeout_ms);
        if (ret != 0){
            fprintf(stderr, "Error: could not resolve addr, exiting...\n");
            return -1;
        }
    }

    // Responsible for initiatizating connections
    // Upon connection creation post a receive work request from the other side's id to retrieve their mailbox mr info

    ret = handle_connection_events(connection_type, conn_server, conn_client, channel, server_qp, client_qp, rai, ret_connection);
    if (ret != 0){
        fprintf(stderr, "Error, could not handle connection events, exiting...\n");
        return -1;
    }


    /* IGNORING THE MAILBOX STUFF FOR NOW

    Connection * conn = *ret_connection;

    // NOW THAT CONNECTION IS SET UP, POST SEND WORK REQUEST SHARING LOCAL MAILBOX

    RemoteMailbox * local_mailbox_to_share = (RemoteMailbox *) malloc(sizeof(RemoteMailbox));
    if (local_mailbox_to_share == NULL){
	fprintf(stderr, "Error: malloc failed when creating local version of remote mailbox struct\n");
	return -1;	
    }
	
    local_mailbox_to_share -> rkey = conn -> src_mr_mailbox -> rkey;
    local_mailbox_to_share -> addr = (uint64_t) conn -> src_mr_mailbox -> addr;
    local_mailbox_to_share -> total_size = conn -> src_mr_mailbox -> length;
    
    struct ibv_mr * local_remote_mailbox_mr;
    ret = register_virt_memory(conn -> pd, (void *) local_mailbox_to_share, sizeof(RemoteMailbox), &local_remote_mailbox_mr);
    if (ret != 0){
	    fprintf(stderr, "Error: could not register local version of remote mailbox to send with ib verbs\n");
	    return -1;
    }

    uint64_t local_id;
    if (is_server){
        local_id = conn_server -> id;
    }
    else{
        local_id = conn_client -> id;
    }

    conn -> local_remote_mailbox_mr = local_remote_mailbox_mr;

    ret = post_send_work_request(conn -> cm_id, local_remote_mailbox_mr, local_id);
    if (ret != 0){
    	fprintf(stderr, "Error: failed to post send work request sharing local mailbox MR with connection\n");
	return -1;
    }

    // POLL FOR COMPLETITION TO RETRIEVE OTHER SIDE'S MAILBOX
    
    // remote id is the wr_id of the posted recv wr to obtain other side's mailbox addr and rkey info
    uint64_t remote_id;
    if (is_server){
        remote_id = conn_client -> id;
    }
    else{
        remote_id = conn_server -> id;
    }

    ret = block_for_wr_comp(conn -> cq, remote_id);
    if (ret != 0){
	    fprintf(stderr, "Error: could not block for completition of wr_id = %ld\n", remote_id);
	    return -1;
    }

    */

    return 0;
}
