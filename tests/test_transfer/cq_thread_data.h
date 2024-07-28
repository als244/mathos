#ifndef CQ_THREAD_DATA_H
#define CQ_THREAD_DATA_H

#include "common.h"
#include "net.h"

typedef struct cq_thread_data{
	struct ibv_cq_ex * cq;
	Net_World * net_world;
} Cq_Thread_Data;

#endif
