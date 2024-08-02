#ifndef INVENTORY_MESSAGES_H
#define INVENTORY_MESSAGES_H

#include "common.h"
#include "config.h"

#define INVENTORY_MESSAGE_MAX_SIZE_BYTES 100


typedef enum inventory_message_type {
	FINGERPRINT_MATCH,
	TRANSFER_INITIATE,
	TRANSFER_RESPONSE,
	INVENTORY_Q
} InventoryMessageType;


// THIS WILL ACTUALLY BE AN INVENTORY_CLASS message!!
//	- NEEDS INVENTORY MANAGER TO HANDLE IT!
//	- the exchange generates a message with the data, but labels the ctrl_message_class data_class
//	- so the other end will process it with their data worker
typedef struct fingerprint_match {
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	uint32_t num_nodes;
	uint32_t node_ids[MAX_FINGERPRINT_MATCH_LOCATIONS];
} Fingerprint_Match;


typedef struct inventory_message {
	InventoryMessageType message_type;
	uint8_t message[INVENTORY_MESSAGE_MAX_SIZE_BYTES];
} Inventory_Message;




#endif