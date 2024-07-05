#ifndef DATA_CONTROLLER_H
#define DATA_CONTROLLER_H

#include "common.h"
#include "channel.h"
#include "communicate.h"
#include "table.h"
#include "fingerprint.h"
#include "data_channel.h"

typedef struct data_connection {
	uint32_t peer_id;
	Connection * connection;
	Channel * in_data_init;
	Channel * out_data_resp;
	Data_Channel * in_data;
	Data_Channel * out_data;
} Data_Connection;

typedef struct data_controller {
	uint32_t self_id;
	// mapping from peer_id to Data_Connection *
	// to obtain metadata/data channels to submit items to
	Table * data_connections;
	struct ibv_pd * data_pd;
	// really should have multiple data QPs/CQs, but need to figure out UD queue pair initialization/configuration...
	struct ibv_qp * data_qp;
	struct ibv_cq_ex * data_cq;
	// number of completion threads should equal number of CQs, likely equal number of QPs...
	pthread_t * completion_threads;
	pthread_mutex_t data_controller_lock;
} Data_Controller;

Data_Connection * setup_data_connection();

#endif