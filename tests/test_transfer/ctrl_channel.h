#ifndef CHANNEL_H
#define CHANNEL_H

#include "common.h"
#include "config.h"
#include "fifo.h"
#include "verbs_ops.h"

typedef enum ctrl_channel_type {
	SEND_CTRL_CHANNEL,
	RECV_CTRL_CHANNEL,
	SHARED_RECV_CTRL_CHANNEL
} CtrlChannelType;

typedef struct channel {
	// within produced_channel type determines if we will post a send/recv
	CtrlChannelType channel_type;
	uint8_t ib_device_id;
	// the endpoint_ind corresponding to index within self_net -> endpoints
	// for shared receive channels this endpoint_ind will be set to 0
	uint32_t endpoint_ind;
	// this channel_id is encoded based upon channel_type + ib_device_id + endpoint_ind:
	//	- see encode_channel_id()
	// used as upper 32 bit of WR_ID
	//	- this channel_id specified to the completion queue handlers which channel to consume
	// the lower 32 bits of WR_ID will be determined by the insertion location into the fifo
	uint32_t channel_id;
	// pd correspondign to the device_id
	// needed in order to register memory
	struct ibv_pd * pd;
	// the registered region of fifo -> buffer upon initialization
	struct ibv_mr * channel_mr;
	// the QP to use for posting sends / recvs if srq is non-null
	struct ibv_qp * qp;
	// the SRQ to use for posting receives (if QP is null)
	struct ibv_srq * srq;
	// where the items will actually be inserted
	Fifo * fifo;
} Ctrl_Channel;


// If channel type is SEND_CHANNEL:
//	- then qp must be non-null an srq is ignored
// If channel type is RECV_CHANNEL:
//	- in recevies will be posted on the qp
// IF channel type is SHARED_RECV_CHANNEL:
//	- qp is ignored (can be null) and receives will be posted on srq
Ctrl_Channel * init_ctrl_channel(CtrlChannelType channel_type, uint32_t max_items, uint8_t ib_device_id, struct ibv_pd * pd, 
									uint32_t endpoint_ind, struct ibv_qp * qp, struct ibv_srq * srq, bool to_populate_recvs);

// for posting sends, send_dest needs to be populated
// the 
// should be NULL for posting receives

int post_recv_ctrl_channel(Ctrl_Channel * channel);
int post_send_ctrl_channel(Ctrl_Channel * channel, Control_Message * ctrl_message, struct ibv_ah * ah, uint32_t remote_qp_num, uint32_t remote_qkey);

Control_Message * extract_ctrl_channel(Ctrl_Channel * channel);

#endif