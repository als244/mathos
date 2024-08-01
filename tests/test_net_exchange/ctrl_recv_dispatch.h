#ifndef CTRL_RECV_DISPATCHER_H
#define CTRL_RECV_DISPATCHER_H


#include "common.h"
#include "fifo.h"
#include "self_net.h"
#include "net.h"
#include "cq_thread_data.h"
#include "work_pool.h"

// to print message class
#include "utils.h"


typedef struct ctrl_recv_dispatcher_thread_data {
	int ib_device_id;
	Fifo * dispatcher_fifo;
	Net_World * net_world;
	Work_Pool * work_pool;
} Ctrl_Recv_Dispatcher_Thread_Data;


void * run_recv_ctrl_dispatcher(void * _ctrl_recv_dispatcher_thread_data);

#endif