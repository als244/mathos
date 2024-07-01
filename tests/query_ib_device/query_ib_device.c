#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <infiniband/verbs.h>

int main(int argc, char *argv[]){

	int ret;

	int num_devices;
	struct ibv_device ** devices = ibv_get_device_list(&num_devices);
	if (devices == NULL){
		fprintf(stderr, "Error: could not get ibv_device list\n");
		return -1;
	}

	printf("Found %d devices\n", num_devices);
	struct ibv_device * device;
	struct ibv_context * dev_ctx;
	struct ibv_device_attr dev_attr;

	// Ref: "https://www.rdmamojo.com/2012/07/13/ibv_query_device/"
	
	// number of physical ports
	int phys_port_cnt;
	// max number of mr's supported by device
	int max_mr;
	// size (in bytes) of largest contiguous memory block that can be registered by device
	uint64_t max_mr_size;
	// memory page sizes supported by this device
	uint64_t page_size_cap;
	// max numnber of qps of UD/UC/RC
	int max_qp;
	// max number of outstanding work requests on any send or recv queue
	int max_qp_wr;
	// max number of SRQs supported by device
	int max_srq;
	// max number of oustanding work requests in an SRQ
	int max_srq_wr;
	// max number of cqs supported by this device
	int max_cq;
	// max number of cq entries in each cq supported by device
	int max_cqe;
	// max number of multi cast groups
	int max_mcast_grp;
	// max number of qp's per multi cast group
	int max_mcast_qp_attach;
	// maximum number of total QP's that can be attached to multicast groups
	int max_total_mcast_qp_attach;
	// maximum number of outstanding RDMA reads & atomic ops that can be outstanding per QP, with this dev as target
	int max_qp_rd_atom;
	// maxmimum depth per QP for initiation of RDMA read & atomic operations by this device
	int max_qp_init_rd_atom;
	// total maximum number of resources used for RDMA read & atomic ops with this dev as target
	int max_res_rd_atom;
	// maximum number of pd's
	int max_pd;
	// maximum number of memory windows
	int max_mw;
	// max number of address handles
	int max_ah;
	// max number of partitions
	int max_pkeys;
	

	for (int dev_num = 0; dev_num < num_devices; dev_num++){
		printf("\n\nDevice %d. Attributes:\n", dev_num);
		device = devices[dev_num];
		
		dev_ctx = ibv_open_device(device);
		if (dev_ctx == NULL){
			fprintf(stderr, "Error: could not open device\n");
			return -1;
		}

		ret = ibv_query_device(dev_ctx, &dev_attr);
		if (ret != 0){
			fprintf(stderr, "Error: could not query device\n");
			return -1;
		}

		phys_port_cnt = dev_attr.phys_port_cnt;
		max_mr = dev_attr.max_mr;
		max_mr_size = dev_attr.max_mr_size;
		page_size_cap = dev_attr.page_size_cap;
		max_qp = dev_attr.max_qp;
		max_qp_wr = dev_attr.max_qp_wr;
		max_srq = dev_attr.max_srq;
		max_srq_wr = dev_attr.max_srq_wr;
		max_cq = dev_attr.max_cq;
		max_cqe = dev_attr.max_cqe;
		max_mcast_grp = dev_attr.max_mcast_grp;
		max_mcast_qp_attach = dev_attr.max_mcast_qp_attach;
		max_total_mcast_qp_attach = dev_attr.max_total_mcast_qp_attach;
		max_qp_rd_atom = dev_attr.max_qp_rd_atom;
		max_qp_init_rd_atom = dev_attr.max_qp_init_rd_atom;
		max_res_rd_atom = dev_attr.max_res_rd_atom;
		max_pd = dev_attr.max_pd;
		max_mw = dev_attr.max_mw;
		max_ah = dev_attr.max_ah;
		max_pkeys = dev_attr.max_pkeys;

		printf(" \
				Phys Port Cnt: %d\n \
				Max MR: %d\n \
				Max MR Size: %lu\n \
				Page Size Cap: %lu\n \
				Max QP: %d\n \
				Max QP Work Requests: %d\n \
				Max SRQ: %d\n \
				Max SRQ Work Requests: %d\n \
				Max CQ: %d\n \
				Max CQE: %d\n \
				Max Multi-Cast Groups: %d\n \
				Max Multi-Cast QP Attach: %d\n \
				Max Total Mult-Cast QP Attach: %d\n \
				Max QP Read/Atom: %d\n \
				Max QP Init Read/Atom: %d\n \
				Max Resources Read/Atom: %d\n \
				Max PD: %d\n \
				Max MW: %d\n \
				Max AH: %d\n \
				Max Partition Keys: %d\n\n", phys_port_cnt, max_mr, max_mr_size, page_size_cap,
												 max_qp, max_qp_wr, max_srq, max_srq_wr, max_cq, max_cqe,
												 max_mcast_grp, max_mcast_qp_attach, max_total_mcast_qp_attach,
												 max_qp_rd_atom, max_qp_init_rd_atom, max_res_rd_atom,
												 max_pd, max_mw, max_ah, max_pkeys);

	}


}