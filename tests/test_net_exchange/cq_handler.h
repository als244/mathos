#ifndef CQ_HANDLER_H
#define CQ_HANDLER_H


#include "common.h"
#include "ctrl_handler.h"
#include "net.h"
#include "cq_thread_data.h"


// Wrapper to dispatch all cq threads to the appropriate handler
int activate_cq_threads(Net_World * net_world);


#endif