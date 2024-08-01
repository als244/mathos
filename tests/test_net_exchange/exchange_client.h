#ifndef EXCHANGE_CLIENT_H
#define EXCHANGE_CLIENT_H


#include "common.h"
#include "fingerprint.h"
#include "exchange.h"
#include "net.h"



int submit_bid(Net_World * net_world, uint8_t * fingerprint);

int submit_offer(Net_World * net_world, uint8_t * fingerprint);

int submit_future(Net_World * net_world, uint8_t * fingerprint);


#endif