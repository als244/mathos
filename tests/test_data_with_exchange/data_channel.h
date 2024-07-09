#ifndef DATA_CHANNEL_H
#define DATA_CHANNEL_H

#include "common.h"
#include "table.h"
#include "channel.h"

#define PATH_MTU 4096


typedef struct data_request {
	uint32_t transfer_start_id;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
} Data_Request;


typedef struct data_packet{
	// packet-id / transfer id's can be shared among different receiver/sender pairs
	// (but only make sense in the context of a individual pair)
	uint32_t packet_id: 24;
	// refers to the wr id of first packet in transfer
	uint32_t transfer_start_id: 24;
	// number of bytes within packet. <= data_channel -> packet_max_bytes
	// most likely 1024 or 4096...
	uint16_t packet_bytes;
} Data_Packet;


typedef struct transfer {
	// starting packet-id of transfer (analogous to channel count)
	uint32_t start_id: 24;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	// either sending addr for outgoing transfers or recv addr for incoming
	void * addr;
	uint32_t data_bytes;
	uint32_t lkey;
	bool is_inbound;
	// upon every completition must grab lock and decrement these values
	// when reamin_packets = 0, remove from table and notify scheduler 
	pthread_mutex_t transfer_lock;
	uint32_t remain_bytes;
	uint32_t remain_packets;
} Transfer;

typedef struct transfer_complete {
	uint32_t start_id: 24;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	void * addr;
	uint32_t data_bytes;
} Transfer_Complete;


typedef struct data_channel {
	uint32_t self_id;
	uint32_t peer_id;
	// PATH-MTU for UD Queue Pairs
	// Sender and Receiver must both agree on this value
	uint16_t packet_max_bytes;

	// upper bound on size of packets table
	uint32_t max_packets;
	// mapping from data_channel_item -> id to transfer
	// this is looked up upon every completition
	Table * packets_table;
	// upper bound on size of transfers table
	uint32_t max_transfers;
	// mapping from start transfer id -> tracker of remaining completitions
	// for every completition queue entry, look up associated start wr id
	// and decrementent reamining counnt
	// when 0, all parital transfers complete and object is ready => notify scheduler
	Table * transfers_table;
	// needed to register the memory region
	struct ibv_pd * pd;
	// needed for posting requests!
	struct ibv_qp * qp;
	// needed for handling events
	// this cq is almost certainly shared among many channels
	struct ibv_cq_ex * cq;
	// only applies to inbound. outbound data channels have known wr_id to send with
	bool is_inbound;
	// must grab lock when reserving a new incoming transfer so that the recvs can be posted
	pthread_mutex_t transfer_start_id_lock;
	// NEEDS TO BE DONE BETTER. SHOULD BE TRACKING RANGES OF AVAILABILITY!
	// just doing simple for now to test...
	// will be an error if wrapping around 24-bits and ids starting at 0 are still ongoing...
	// FOR NOW... THE MAXIMUM NUMBER OF OUTSTANDING PACKETS IS 2^24 ~= 16 million ~= 16 GB
	// Can be clever about repopulating receive wr_ids after consuming in order to circumvent...
	uint32_t transfer_start_id: 24;
	// equal to the number of bits allocated for channel count as part of wr id
	// tells when to wrap around when encoding wr_id's
	uint32_t max_packet_id;
	
} Data_Channel;


// packet id's can be shared among different channels, but only have meaning within a given channel
uint32_t decode_packet_id(uint64_t wr_id);


Data_Channel * init_data_channel(uint32_t self_id, uint32_t peer_id, uint32_t packet_max_bytes, uint32_t max_packets, uint32_t max_packet_id, uint32_t max_transfers, bool is_inbound, struct ibv_pd * pd, struct ibv_qp * qp, struct ibv_cq_ex * cq);

// 1.) initializes a transfer and inserts into transfers table
// 2.) initializes sequence of data packets while inserting into data packets table
// 3.) Creates queue of IBV_WR sends and posts them with ibv_wr_complete
//		- here the start_id is known based on the DATA_INITIATE message from the ultimate receiver of this transfer
//		- it is used to agree on packet ordering and location with addr for each packet
// returns 0 on success, -1 on error
int submit_out_transfer(Data_Channel * data_channel, uint8_t * fingerprint, void * addr, uint32_t data_bytes, uint32_t lkey, uint32_t start_id);


// 1.) Determines number of packets needed for transfer, grabs start_transfer_id lock, 
//		determines if sufficient recv request room, and then increments start_transfer_id by num_packets
// 2.) initializes a transfer and inserts into transfers table
// 3.) initializes sequence of data packets while inserting into data packets table
// 4.) Creates linked list of recv work requests corresponding to packets and calls ibv_post_recv()
// 5.) Optionally returns the starting packet id of transfer
// returns 0 on success, -1 on error
int submit_in_transfer(Data_Channel * data_channel, uint8_t * fingerprint, void * recv_addr, uint32_t data_bytes, uint32_t lkey, uint32_t * ret_start_id);

// Called upon data controller work completition of data_packet type
// 1.) Look up packet in packets table based on packed_id
// 2.) Retrieve the corresponding transfer based on transfer_start_id 
// 3.) Look up transfer in transfers table
// 4.) Acquire transfer lock
// 5.) Decrement remain_bytes and remain_packets
// 		- a.) If remain_packets == 0 => notify scheduler (for now just print) and remove/free transfer
// 6.) Release transfer lock
// 7.) Remove/free packet  
int ack_packet_local(Data_Channel * data_channel, uint32_t packet_id, Transfer_Complete ** ret_transfer_complete);






#endif