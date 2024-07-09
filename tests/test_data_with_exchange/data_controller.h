#ifndef DATA_CONTROLLER_H
#define DATA_CONTROLLER_H

#include "common.h"
#include "channel.h"
#include "communicate.h"
#include "table.h"
#include "fingerprint.h"
#include "inventory.h"
#include "data_channel.h"

typedef struct data_connection {
	uint32_t peer_id;
	Connection * connection;
	uint32_t capacity_control_channels;
	Channel * in_data_req;
	Channel * out_data_req;
	uint32_t packet_max_bytes;
	Data_Channel * in_data;
	Data_Channel * out_data;
} Data_Connection;

typedef struct data_controller {
	uint32_t self_id;
	// maintians fingerprint -> obj location mappings
	Inventory * inventory;
	// mapping from peer_id to Data_Connection *
	// to obtain metadata/data channels to submit items to
	uint32_t max_connections;
	Table * data_connections_table;
	struct ibv_context * ibv_ctx;
	struct ibv_pd * data_pd;
	// really should have multiple data QPs/CQs, but need to figure out UD queue pair initialization/configuration...
	struct ibv_qp * data_qp;
	// TODO: probably want to have a shared receive queue across the connections....
	struct ibv_cq_ex * data_cq;
	// TEMPORARITY ADDING OTHER PD/CQ/QP for testing
	struct ibv_pd * control_pd;
	struct ibv_qp * control_qp;
	struct ibv_cq_ex * control_cq;
	// number of completion threads should equal number of CQs, likely equal number of QPs...
	// For now setting to 1...
	int num_cqs;
	pthread_t * completion_threads;
	pthread_mutex_t data_controller_lock;
} Data_Controller;


typedef struct data_completion {
	int completion_thread_id;
	Data_Controller * data_controller;
} Data_Completion;


Data_Controller * init_data_controller(uint32_t self_id, Inventory * inventory, uint32_t max_connections, int num_cqs, struct ibv_context * ibv_ctx);

int setup_data_connection(Data_Controller * data_controller, uint32_t peer_id, char * self_ip, char * peer_ip, char * server_port, uint32_t capacity_control_channels, 
	uint32_t packet_max_bytes, uint32_t max_packets, uint32_t max_packet_id, uint32_t max_transfers);

int send_data_request(Data_Controller * data_controller, uint32_t peer_id, uint8_t * fingerprint, void * recv_addr, uint32_t data_bytes, uint32_t lkey, uint32_t * ret_start_id);

#endif
