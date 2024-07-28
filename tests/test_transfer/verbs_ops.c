#include "verbs_ops.h"

int register_virt_memory(struct ibv_pd * pd, void * addr, size_t size_bytes, struct ibv_mr ** ret_mr) {

	// ALSO CONSIDER:
	// IBV_ACCESS_HUGETLB & IBV_ACCESS_ON_DEMAND
	
	enum ibv_access_flags access_flags = IBV_ACCESS_LOCAL_WRITE;

	//enum ibv_access_flags access_flags = IBV_ACCESS_LOCAL_WRITE;
	struct ibv_mr * mr = ibv_reg_mr(pd, addr, size_bytes, access_flags);
	if (mr == NULL){
		fprintf(stderr, "Error: ibv_reg_mr failed in get_system_memory\n");
		return -1;
	}

	*ret_mr = mr;

	return 0;
}


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

	ret = ibv_post_recv(qp, &wr, &bad_wr);
	if (ret != 0){
		fprintf(stderr, "Error: could note post receive work request\n");
		return -1;
	}

	return 0;
}

int post_srq_work_request(struct ibv_srq * srq, uint64_t addr, uint32_t length, uint32_t lkey, uint64_t wr_id) {
	
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

	ret = ibv_post_srq_recv(srq, &wr, &bad_wr);
	if (ret != 0){
		fprintf(stderr, "Error: could note post srq work request\n");
		return -1;
	}

	return 0;
}




int post_send_work_request(struct ibv_qp * qp, uint64_t addr, uint32_t length, uint32_t lkey, uint64_t wr_id, struct ibv_ah * ah, uint32_t remote_qp_num, uint32_t remote_qkey) {

	int ret;

	struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(qp);
	ibv_wr_start(qp_ex);

	qp_ex -> wr_id = wr_id;
	qp_ex -> wr_flags = 0; /* ordering/fencing etc. */
	
	// PEFORM Send
	ibv_wr_send(qp_ex);

	ibv_wr_set_sge(qp_ex, lkey, addr, length);

	// UD Details (Address Header/Remote QP Num)
	// retrieved from send_dest
	// send_dest was created from get_send_dest within net.c
	ibv_wr_set_ud_addr(qp_ex, ah, remote_qp_num, remote_qkey);

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

	// uint32_t qp_num, src_qp;
	// uint64_t hca_timestamp_ns, wallclock_timestamp_ns;

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
			printf("Saw completion of wr_id = %lu\n\tStatus: %d\n", wr_id, status);
			if (status != IBV_WC_SUCCESS){
				fprintf(stderr, "Error: work request id %lu had error\n", wr_id);
			}

			// THESE OUTPUTS SEEM WRONG??
			// Maybe only works on infinband, or need to use thread domains...?
			
			// qp_num = ibv_wc_read_qp_num(cq);
			// src_qp = ibv_wc_read_src_qp(cq);
			// hca_timestamp_ns = ibv_wc_read_completion_ts(cq);
			// wallclock_timestamp_ns = ibv_wc_read_completion_wallclock_ns(cq);
			// printf("\t\tQP Num: %u\n\t\tSrc QP Num: %u\n\t\tHCA Timestamp: %lu\n\t\tWallclock Timestamp: %lu\n\n");
			

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


void decode_wr_id(uint64_t wr_id, uint8_t * ret_channel_type, uint8_t * ret_ib_device_id, uint32_t * ret_endpoint_id) {

	// only care about the upper 32 bits
	uint32_t channel_id = wr_id >> 32;
	printf("Channel ID: %u\n", channel_id);

	// endpoint id is the lower 32 bits
	*ret_channel_type = (uint8_t) (channel_id >> (32 - 2));
	
	// first shift channel to clear out control channel bits
	// now shift back to clear out the endpoint bits
	*ret_ib_device_id = (uint8_t) (channel_id << 2) >> (2 + 20);
	
	// shift up to clear out, then shift back
	*ret_endpoint_id = (channel_id << (2 + 8)) >> (2 + 10);

	return;
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

	uint8_t channel_type;
	uint8_t ib_device_id;
	uint32_t endpoint_id;

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
			printf("Saw completion of wr_id = %lu\n\tStatus: %d\n", wr_id, status);

			decode_wr_id(wr_id, &channel_type, &ib_device_id, &endpoint_id);

			printf("Decoding of wr_id:\n\tChannel Type: %u\n\tIB Device ID: %u\n\tEndpoint Ind: %u\n\n", channel_type, ib_device_id, endpoint_id);

			if (status != IBV_WC_SUCCESS){
				fprintf(stderr, "Error: work request id %lu had error\n", wr_id);
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