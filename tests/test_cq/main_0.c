#define SERVER_ID 0
#define SERVER_IP "192.168.50.23"
#define SERVER_PORT "8481"
#define CLIENT_ID 1
#define CLIENT_IP "192.168.50.32"

#include "communicate.h"

int main(int argc, char * argv[]){
	
	int ret;

        // OPEN IB DEVICE CONTEXT. USE DEVICE 0 FOR TESTING
        // Needed for initializing exchange & exchanges_client PD, CQ, and QPs

        int num_devices;
        struct ibv_device ** devices = ibv_get_device_list(&num_devices);
        if (devices == NULL){
                fprintf(stderr, "Error: could not get ibv_device list\n");
                return -1;
        }

        if (num_devices == 0){
                fprintf(stderr, "Error: no rdma device fonud\n");
                return -1;
        }

        struct ibv_device * device = devices[0];

        struct ibv_context * ibv_ctx = ibv_open_device(device);
        if (ibv_ctx == NULL){
                fprintf(stderr, "Error: could not open device to get ibv context\n");
                return -1;
        }


	// 1.) PD based on inputted configuration
        struct ibv_pd * pd = ibv_alloc_pd(ibv_ctx);
        if (pd == NULL) {
                fprintf(stderr, "Error: could not allocate pd for exchanges_client\n");
                return -1;
        }

        // 2.) CQ based on inputted configuration
        int num_cq_entries = 1U << 3;

        /* "The pointer cq_context will be used to set user context pointer of the cq structure" */

        // SHOULD BE THE EXCHANGE_CLIENT COMPLETITION HANDLER
        void * cq_context = NULL;

        struct ibv_cq_init_attr_ex cq_attr;
        memset(&cq_attr, 0, sizeof(cq_attr));
        cq_attr.cqe = num_cq_entries;
        cq_attr.cq_context = cq_context;

        // every cq will be in its own thread...
        // uint32_t cq_create_flags = IBV_CREATE_CQ_ATTR_SINGLE_THREADED;
        // cq_attr.flags = cq_create_flags;

        struct ibv_cq_ex * cq = ibv_create_cq_ex(ibv_ctx, &cq_attr);
        if (cq == NULL){
                fprintf(stderr, "Error: could not create cq for exchanges_client\n");
                return -1;
        }

	 RDMAConnectionType connection_type = RDMA_RC;
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

        // Device cap of 2^15 for each side of QP's outstanding work requests...
        qp_attr.cap.max_send_wr = 1U << 8;  // increase if you want to keep more send work requests in the SQ.
        qp_attr.cap.max_recv_wr = 1U << 8;  // increase if you want to keep more receive work requests in the RQ.
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

        struct ibv_qp * qp = ibv_create_qp_ex(ibv_ctx, &qp_attr);
        if (qp == NULL){
                fprintf(stderr, "Error: could not create qp for exchanges_client\n");
                return -1;
        }

	Connection * conn;
	
	ret = setup_connection(RDMA_RC, 1, SERVER_ID, SERVER_IP, SERVER_PORT, pd, qp, cq, CLIENT_ID, CLIENT_IP, NULL, NULL, NULL, &conn);
	if (ret != 0){
		fprintf(stderr, "Error: could not setup connection");
		return -1;
	}



	// NOW WE ARE CONNECTED LET'S POST RECVS
	
	uint64_t size_bytes = 10;
	uint64_t recv_buffer = (uint64_t) malloc(size_bytes);
	
	struct ibv_mr * recv_mr;
	ret = register_virt_memory(pd, (void *) recv_buffer, size_bytes, &recv_mr);
	if (ret != 0){
		fprintf(stderr, "Error: could not register virt memory region\n");
		return -1;
	}
	
	ret = post_recv_work_request(qp, recv_buffer, 1, recv_mr -> lkey, 0);
	if (ret != 0){
		fprintf(stderr, "Error: could not post recv request: 0\n");
		return -1;
	}

	ret = post_recv_work_request(qp, recv_buffer + 5, 1, recv_mr -> lkey, 1);
	if (ret != 0){
		fprintf(stderr, "Error: could not post recv request: 1\n");
		return -1;
	}

	uint64_t poll_duration_ns = 10 * 1e9;

	ret = poll_cq(cq, poll_duration_ns);
	if (ret != 0){
		fprintf(stderr, "Error: polling failed\n");
		return -1;
	}

	return 0;

}
