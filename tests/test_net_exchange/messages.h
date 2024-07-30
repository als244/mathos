#ifndef MESSAGE_H
#define MESSAGE_H

#include "common.h"

#include "exchange_messages.h"

#define CONTROL_MESSAGE_CONTENT_MAX_SIZE_BYTES 128

// This is imported from config.h!


// USED FOR JOIN_NET

typedef struct node_config {
	uint32_t node_id;
	// equivalent to inet_addr(char * ip_addr)
	// s_addr is in network-byte order (not host order)
	uint32_t s_addr;
} Node_Config;

// Header for join response
typedef struct join_response_h {
	uint32_t node_id;
	uint32_t max_nodes;
	uint32_t cur_node_cnt;
	uint32_t min_init_nodes;
	// in case the connecting node didn't set its ip,
	// let it know what it was so it can start its rdma_init server
	uint32_t s_addr;
} Join_Response_H;

// The header is sent/recv first, followed by the node_config_arr
typedef struct join_response {
	Join_Response_H header;
	// will be of size node_cnt sent in the header
	Node_Config * node_config_arr;
} Join_Response;



// RDMA MESSAGING TYPES AND STRUCTURES

typedef enum enpoint_type {
	CONTROL_ENDPOINT,
	DATA_ENDPOINT,
	ALERT_MULTICAST_ENDPOINT
} EndpointType;


#define MAX_WORK_CLASS_IND 6

// The control message type indicates
// to the completion queue handler
// what worker they should hand off to
typedef enum ctrl_message_class {
	EXCHANGE_CLASS,
	// for data transfer intiation / response
	// actually data transfer will not be done over control QPs
	INVENTORY_CLASS,
	REQUEST_CLASS,
	INGEST_CLASS,
	ALERT_CLASS,
	CONFIG_CLASS,
	SYSTEM_CLASS
} CtrlMessageClass;


typedef struct ctrl_message_h {
	uint32_t source_node_id;
	// this might be a bit redunant
	// (encoded within the address handle when sending)
	// however it is convenient to have when certain
	// modules (exchange + sched) generate response
	// messages and want a different worker thread
	// to be responsible for sending them out
	// Thus the worker thread can look up the 
	// the destination directly within the message
	// they are supposed to send out
	uint32_t dest_node_id;
	CtrlMessageClass message_class;
	// maximum message size is path_mtu <= 4096 
	// not sure if this field is needed...?
	//uint16_t message_len;
} Ctrl_Message_H;

typedef struct ctrl_message {
	Ctrl_Message_H header;
	// to be interpreted based upon header -> message_type
	uint8_t contents[CONTROL_MESSAGE_CONTENT_MAX_SIZE_BYTES];
} Ctrl_Message;

typedef struct recv_ctrl_message {
	struct ibv_grh grh;
	Ctrl_Message ctrl_message;
} Recv_Ctrl_Message;




#endif