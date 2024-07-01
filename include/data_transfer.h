#ifndef DATA_TRANSFER_H
#define DATA_TRANSFER_H

#include "common.h"
#include "table.h"
#include "channel.h"


typedef struct data_link {
	uint64_t peer_id;
	Channel * in_requests;
	// used if we want to send data_response messages 
	// (in case of errors such as "no_fingerprint" or "too_busy" or "complete", etc.)
	Channel * out_requests;
} Data_Link;

typedef struct data_controller {
	// hashing from peer_id to struct data_link to get the channels upon which messages are sent
	Table * data_links;
	// table of outstanding request wr_id -> fingerprint 
	// (needed for handler upon completition to interact with scheduling queues)
	Table * outstanding_requests;
	// the data links will be sharded across qps
	int num_data_qps;
	struct ibv_qp ** data_qps;
} Data_Controller;


Data_Controller * init_data_controller(uint64_t max_data_links, uint64_t max_outstanding_requests, int num_data_qps);


int setup_data_link(Data_Controller * data_controller, uint64_t self_id, uint64_t peer_id, uint16_t in_capacity, uint16_t out_capacity, int qp_id);

#endif