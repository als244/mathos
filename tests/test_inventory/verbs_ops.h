#ifndef VERBS_OPS_H
#define VERBS_OPS_H

#include "common.h"

int register_virt_memory(struct ibv_pd * pd, void * addr, size_t size_bytes, struct ibv_mr ** ret_mr);


int post_send_work_request(struct ibv_qp * qp, uint64_t addr, uint32_t length, uint32_t lkey, uint64_t wr_id, struct ibv_ah * ah, uint32_t remote_qp_num, uint32_t remote_qkey);
int post_send_batch_work_request(struct ibv_qp * qp, uint64_t num_items, uint64_t addr_start, uint32_t item_length, uint32_t lkey, uint64_t wr_id_start, struct ibv_ah * ah, uint32_t remote_qp_num, uint32_t remote_qkey);


int post_recv_work_request(struct ibv_qp * qp, uint64_t addr, uint32_t length, uint32_t lkey, uint64_t wr_id);
int post_srq_work_request(struct ibv_srq * srq, uint64_t addr, uint32_t length, uint32_t lkey, uint64_t wr_id);

int post_recv_batch_work_requests(struct ibv_qp * qp, uint32_t num_items, uint64_t addr_start, uint32_t item_length, uint32_t lkey, uint64_t wr_id_start);
int post_srq_batch_work_requests(struct ibv_srq * srq, uint32_t num_items, uint64_t addr_start, uint32_t item_length, uint32_t lkey, uint64_t wr_id_start);


int poll_cq(struct ibv_cq_ex * cq, uint64_t duration_ns);
int block_for_wr_comp(struct ibv_cq_ex * cq, uint64_t target_wr_id);
uint64_t block_for_batch_wr_comp(struct ibv_cq_ex * cq, uint64_t num_completetions);

#endif