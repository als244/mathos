#ifndef CTRL_HANDLER_H
#define CTRL_HANLDER_H

#include "common.h"
#include "ctrl_channel.h"
#include "self_net.h"
#include "net.h"
#include "cq_thread_data.h"

// Just before returning from intialization, init_set spawns 
// handler threads for all the CQs (whose threads have been allocated within self_net)

typedef struct ctrl_handler {
	struct ibv_cq_ex * cq;
	Net_World * net_world;
} Ctrl_Handler;


// Called from within cq_handler.c => run_cq_thread
void * run_ctrl_handler(void * _ctrl_handler);


#endif