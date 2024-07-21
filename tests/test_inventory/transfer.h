#ifndef TRANSFER_H
#define TRANSFER_H

#include "common.h"
#include "inventory.h"
#include "mempool.h"
#include "table.h"
#include "fingerprint.h"

typedef struct network_destination {
	// can have a table mapping node id + port num => ah
	// create this table upon system intialization
	struct ibv_ah * ah;
	// should be specfied within the transfer requests
	// the remote_qp_num refers to the qp_num of the
	// Data QP aqcuired for exclusive access on the receiver's end 
	uint32_t remote_qp_num;
	uint32_t remote_qkey;
} Network_Destination;

// When remain_packets reaches zero, can mark the is_available bit within obj_location
// and then can move to run queue
typedef struct ongoing_transfer {
	uint32_t transfer_id;
	uint32_t size_bytes;
	uint32_t total_packets;
	uint32_t remain_packets;
	uint16_t data_packet_size;
	struct ibv_qp * data_qp;
} Ongoing_Transfer;

// Needed when polling CQ to update the packet_counting
typedef struct Transfers {
	// mapping from wr_id => ongoing_transfer
	// found by masking the lower 32 bits of wr_id (wr_id >> 32)
	Table * recvs;
	Table * sends;
	// Keeping ID tracking for easy lookup on polling CQ
	// The recv and send have different dedicated Data CQs
	// (i.e. their wr_ids can overlap, because different handlers)
	pthread_mutex_t recv_id_lock;
	// this gets incremented by each recv request 
	uint64_t recv_id;
	pthread_mutex_t send_id_lock;
	// this gets incremented by each send request
	uint64_t send_id;
} Transfers;

// Data CQ WR_ID encoding:
// top 32 bits are the transfer id, bottom 32 bits are the packet number within transfer

// here data_packet_size doesn't inlcude the 40 bytes for GRH

// here recv_data_qp must have been acquired for exlusive access first, then after completion can release it
int recv_data(Transfers * transfers, struct ibv_qp * recv_data_qp, Mem_Reservation * reservation, uint16_t data_packet_size);

// network destination based upon the DATA_REQUEST control message
int send_data(Transfers * transfers, struct ibv_qp * send_data_qp, Mem_Reservation * reservation, uint16_t data_packet_size, Network_Destination * network_destination);


// Network Location

#endif