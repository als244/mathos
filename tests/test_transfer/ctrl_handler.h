#ifndef CTRL_HANDLER_H
#define CTRL_HANLDER_H

#include "common.h"
#include "ctrl_channel.h"
#include "net.h"

// Just before returning from intialization, init_set spawns 
// handler threads for all the CQs (whose threads have been allocated within self_net)
void * run_ctrl_handler(void * _net_world);


#endif