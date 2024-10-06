#ifndef INVENTORY_MESSAGES_H
#define INVENTORY_MESSAGES_H

#include "common.h"
#include "config.h"


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


typedef struct transfer_initiate {
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	// ADD QP INFO HERE!
	// The receiver will initiate transfer
	// and specify the endpoint index they
	// expect the sender to send to
	int endpoint_ind;
} Transfer_Initiate;

typedef struct transfer_response {
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	int is_confirmed;
} Transfer_Response;


typedef struct inventory_query {
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	int is_reply;
	int has_object;
} Inventory_Query;


#define INVENTORY_MESSAGE_MAX_SIZE_BYTES CONTROL_MESSAGE_CONTENT_MAX_SIZE_BYTES - sizeof(InventoryMessageType)


typedef struct inventory_message {
	InventoryMessageType message_type;
	uint8_t message[INVENTORY_MESSAGE_MAX_SIZE_BYTES];
} Inventory_Message;




#endif