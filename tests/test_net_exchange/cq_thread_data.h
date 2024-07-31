#ifndef CQ_THREAD_DATA_H
#define CQ_THREAD_DATA_H

#include "common.h"
#include "net.h"
#include "exchange.h"
#include "work_pool.h"

typedef struct cq_thread_data{
	EndpointType endpoint_type;
	int ib_device_id;
	struct ibv_cq_ex * cq;
	bool is_recv_cq;
	Net_World * net_world;
	// The completition queue will be producing on each of these fifos
	// each of the respective workers will be consuming from these fifos

	// The completition queue is responsible for reading the control message
	// header which indicates the class the control message should be "routed to"
	// Within work_pool there are fifo's per-work class the the cq handler should
	// produce message to
	Work_Pool * work_pool;
} Cq_Thread_Data;

#endif
