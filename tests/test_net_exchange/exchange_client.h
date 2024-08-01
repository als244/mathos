#ifndef EXCHANGE_CLIENT_H
#define EXCHANGE_CLIENT_H


#include "common.h"
#include "fingerprint.h"
#include "exchange.h"
#include "net.h"
#include "sys.h"



int submit_exchange_order(System * system, uint8_t * fingerprint, ExchMessageType exch_message_type);


#endif