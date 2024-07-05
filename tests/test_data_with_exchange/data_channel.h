#ifndef DATA_CHANNEL_H
#define DATA_CHANNEL_H

#include "common.h"
#include "channel.h"

#define PATH_MTU 4096

typedef struct data_channel_item {
	uint64_t id;
	uint64_t start_wr_id;
	uint64_t data_bytes;
} Data_Channel_Item;


typedef struct data_channel {
	Channel * channel;
	// PATH-MTU for UD Queue Pairs
	uint64_t data_packet_bytes;
} Data_Channel;

Data_Channel * init_data_channel();

#endif