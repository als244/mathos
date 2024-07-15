#include "verbs_ops.h"
#include <errno.h>

#define TO_PRINT 0

// GUIDED BY: 
//  - Roland Drier: https://www.youtube.com/watch?v=JtT0uTtn2EA"
//  - Mellanox/Nvidia Docs: https://docs.nvidia.com/networking/display/rdmaawareprogrammingv17/programming+examples+using+ibv+verbs#src-34256583_ProgrammingExamplesUsingIBVVerbs-Main"
//      - Jason Gunthorpe

int post_recv_work_request(struct ibv_qp * qp, uint64_t addr, uint32_t length, uint32_t lkey, uint64_t wr_id) {
    
    int ret;

    struct ibv_recv_wr wr;
    memset(&wr, 0, sizeof(struct ibv_recv_wr));

    struct ibv_recv_wr * bad_wr = NULL;

    struct ibv_sge my_sge;
    memset(&my_sge, 0, sizeof(struct ibv_sge));
    my_sge.addr = addr;
    my_sge.length = length;
    my_sge.lkey = lkey;
    int num_sge = 1;
  
    wr.sg_list = &my_sge;
    wr.num_sge = num_sge;
    wr.wr_id = wr_id;
    wr.next = NULL;

    if (qp -> srq){
        ret = ibv_post_srq_recv(qp -> srq, &wr, &bad_wr);
    }
    else{
        ret = ibv_post_recv(qp, &wr, &bad_wr);
    }
    
    if (ret != 0){
        fprintf(stderr, "Error: could note post receive work request\n");
        return -1;
    }

    return 0;
}

int post_send_work_request(struct ibv_qp * qp, uint64_t addr, uint32_t length, uint32_t lkey, uint64_t wr_id) {

    int ret;

    struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(qp);
    ibv_wr_start(qp_ex);

    qp_ex -> wr_id = wr_id;
    qp_ex -> wr_flags = 0; /* ordering/fencing etc. */
    
    // PEFORM Send
    ibv_wr_send(qp_ex);

    ibv_wr_set_sge(qp_ex, lkey, addr, length);
    /* can send discontiguous buffers by using ibv_wr_set_sge_list() */
    ret = ibv_wr_complete(qp_ex);

    if (ret != 0){
        fprintf(stderr, "Error: issue with ibv_wr_complete\n");
        return -1;
    }

    return 0;
}


int post_recv_work_request_mr(struct ibv_qp * qp, struct ibv_mr * mr, uint64_t wr_id){
    
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


    ret = ibv_post_recv(qp, &wr, &bad_wr);
    if (ret != 0){
        fprintf(stderr, "Error: could note post receive work request\n");
        return -1;
    }

    return 0;
}

// Posting a send work request starts immediately and consumes a receive'd work request
int post_send_work_request_mr(struct ibv_qp * qp, struct ibv_mr * mr, uint64_t wr_id) {

    int ret;

    struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(qp);
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




int post_rdma_read_work_request(struct ibv_qp * qp, struct ibv_mr * mr, uint64_t wr_id, uint32_t rkey, uint64_t remote_addr){

	int ret;

	struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(qp);
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

int post_rdma_write_work_request(struct ibv_qp * qp, struct ibv_mr * mr, uint64_t wr_id, uint32_t rkey, uint64_t remote_addr){

        int ret;

        struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(qp);
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
int post_cmp_swap_send_work_request(struct ibv_qp * qp, struct ibv_mr * mr, uint64_t wr_id, uint32_t rkey, uint64_t remote_addr, uint64_t compare_val, uint64_t swap_val) {

	int ret;

	struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(qp);
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

    //enum ibv_access_flags access_flags = IBV_ACCESS_LOCAL_WRITE;
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
