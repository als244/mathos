#ifndef VERBS_OPS_H
#define VERBS_OPS_H

#include "common.h"

// FUNCTIONS TO EXPORT

// RDMA Source Code:
//	- https://github.com/linux-rdma/rdma-core/tree/master


// RDMA TUTORIAL

// Source of Information:
//	- https://www.youtube.com/watch?v=JtT0uTtn2EA&themeRefresh=1
//	- Roland Dreier & Jason Gunthrope


// Major IB Verbs objects

// The "_ex" variants are the modern API

// ibv_pd: protection domain (container)
//	- contains memory regions
//	- contains work queues
//		- operations in these work queues can only 
//			access memory from same protection domain
// ibv_qp_ex: queue pair
//	- Receive and send work queues for posting requests
//	- Two directions of a connection
// ibv_cq_ex: completition queue
//	- Each work queue (receive and send) is attached to a CQ
// ibv_mr: memory regions
//	- Represents memory buffer (start address + length)
//	- Has local key for use in local requests (L_key)
//	- Has remote key for use in remote requests (R_key)
//		- Shared with remote side so they use is to refer to this mr


// High Level Steps

// 1.) Register memory
// 2.) Create and connect a QP with librdmacm
//		- enables creating connection with IP addrs
// 3.) Post receive work request(s)
// 4.) Post send work request(s)
// 5.) Poll for completition of work requests


// Examples of using this API

// - Perftest:
//		- https://github.com/linux-rdma/perftest/blob/master/src/perftest_communication.c

typedef enum rdma_connection_type {
	RDMA_RC = 0,
	RDMA_UD = 1
} RDMAConnectionType;


// FUNCTIONS TO EXPORT

int register_virt_memory(struct ibv_pd * pd, void * addr, size_t size_bytes, struct ibv_mr ** ret_mr);
int register_dmabuf_memory(struct ibv_pd * pd, int fd, size_t size_bytes, uint64_t offset, uint64_t iova, struct ibv_mr ** ret_mr);

int post_recv_work_request(struct ibv_qp * qp, uint64_t addr, uint32_t length, uint32_t lkey, uint64_t wr_id);
int post_recv_work_request_sge(struct ibv_qp * qp, int num_sge, uint64_t * addr_list, uint64_t * length_list, uint32_t * lkey_list, uint64_t wr_id);

int post_send_work_request(struct ibv_qp * qp, uint64_t addr, uint32_t length, uint32_t lkey, uint64_t wr_id);

int post_cmp_swap_send_work_request(struct ibv_qp * qp, struct ibv_mr * mr, uint64_t wr_id, uint32_t rkey, uint64_t remote_addr, uint64_t compare_val, uint64_t swap_val);


int poll_cq(struct ibv_cq_ex * cq, uint64_t duration_ns);
int block_for_wr_comp(struct ibv_cq_ex * cq, uint64_t target_wr_id);

#endif
