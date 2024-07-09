#include "data_channel.h"

int data_packet_cmp(void * data_packet, void * other_packet) {
	uint32_t id_a = ((Data_Packet *) data_packet) -> packet_id;
	uint32_t id_b = ((Data_Packet *) other_packet) -> packet_id;
	return id_a - id_b;
}

uint64_t data_packet_hash_func(void * data_packet, uint64_t table_size) {
	uint32_t key = ((Data_Packet *) data_packet) -> packet_id;
	// Take from "https://gist.github.com/badboy/6267743"
	// Credit: Robert Jenkins
	key = (key+0x7ed55d16) + (key<<12);
   	key = (key^0xc761c23c) ^ (key>>19);
   	key = (key+0x165667b1) + (key<<5);
   	key = (key+0xd3a2646c) ^ (key<<9);
   	key = (key+0xfd7046c5) + (key<<3);
   	key = (key^0xb55a4f09) ^ (key>>16);
	return (uint64_t) key % table_size;
}

int transfer_cmp(void * transfer, void * other_transfer) {
	uint32_t id_a = ((Transfer *) transfer) -> start_id;
	uint32_t id_b = ((Transfer *) other_transfer) -> start_id;
	return id_a - id_b;
}

uint64_t transfer_hash_func(void * transfer, uint64_t table_size) {
	uint32_t key = ((Transfer *) transfer) -> start_id;
	// Take from "https://gist.github.com/badboy/6267743"
	// Credit: Robert Jenkins
	key = (key+0x7ed55d16) + (key<<12);
   	key = (key^0xc761c23c) ^ (key>>19);
   	key = (key+0x165667b1) + (key<<5);
   	key = (key+0xd3a2646c) ^ (key<<9);
   	key = (key+0xfd7046c5) + (key<<3);
   	key = (key^0xb55a4f09) ^ (key>>16);
	return (uint64_t) key % table_size;
}

Data_Channel * init_data_channel(uint32_t self_id, uint32_t peer_id, uint32_t packet_max_bytes, uint32_t max_packets, uint32_t max_packet_id, uint32_t max_transfers, bool is_inbound, struct ibv_pd * pd, struct ibv_qp * qp, struct ibv_cq_ex * cq) {

	int ret;

	Data_Channel * data_channel = (Data_Channel *) malloc(sizeof(Data_Channel));
	if (data_channel == NULL){
		fprintf(stderr, "Error: malloc failed allocating data channel\n");
		return NULL;
	}

	data_channel -> self_id = self_id;
	data_channel -> peer_id = peer_id;
	data_channel -> packet_max_bytes = packet_max_bytes;
	data_channel -> is_inbound = is_inbound;
	data_channel -> pd = pd;
	data_channel -> qp = qp;
	data_channel -> cq = cq;
	data_channel -> transfer_start_id = 0;

	
	// SETTING DEFAULT TABLE PARAMS HERE...
	// should really change location / args to config this better

	float load_factor = 0.5f;
	float shrink_factor = 0.1f;

	// tracking on-going packets in-flight

	// setting this arbitrary, but can tune according to memory/re-size latency tradeoff...
	uint64_t min_packets = 1UL << 10;
	Hash_Func hash_func_packet = &data_packet_hash_func;
	Item_Cmp item_cmp_packet = &data_packet_cmp;

	Table * packets_table = init_table(min_packets, max_packets, load_factor, shrink_factor, hash_func_packet, item_cmp_packet);
	if (packets_table == NULL){
		fprintf(stderr, "Error: could not initialize packets table\n");
		return NULL;
	}

	data_channel -> packets_table = packets_table;

	// tracking on-going transfers

	// setting this arbitrary, but can tune according to memory/re-size latency tradeoff...
	uint64_t min_transfers = 1UL << 10;
	Hash_Func hash_func_transfer = &transfer_hash_func;
	Item_Cmp item_cmp_transfer = &transfer_cmp;

	Table * transfers_table = init_table(min_transfers, max_transfers, load_factor, shrink_factor, hash_func_transfer, item_cmp_transfer);
	if (transfers_table == NULL){
		fprintf(stderr, "Error: could not initialize transfers table\n");
		return NULL;
	}

	data_channel -> transfers_table = transfers_table;	


	ret = pthread_mutex_init(&(data_channel -> transfer_start_id_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init transfer start id lock\n");
		return NULL;
	}

	data_channel -> max_packet_id = max_packet_id;

	return data_channel;
}

// gets transfer id, initialize transfers data, adds to table



Transfer * init_transfer(uint32_t start_id, uint8_t * fingerprint, void * addr, uint32_t data_bytes, uint32_t lkey, bool is_inbound, uint32_t num_packets){
	Transfer * transfer = (Transfer *) malloc(sizeof(Transfer));
	if (transfer == NULL){
		fprintf(stderr, "Error: malloc failed allocating transfer\n");
		return NULL;
	}

	transfer -> start_id = start_id;
	transfer -> addr = addr;
	transfer -> data_bytes = data_bytes;
	transfer -> lkey = lkey;
	transfer -> is_inbound = is_inbound;
	transfer -> remain_bytes = data_bytes;
	transfer -> remain_packets = num_packets;

	memcpy(transfer -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);

	int ret = pthread_mutex_init(&(transfer -> transfer_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init transfer lock\n");
		return NULL;
	}

	return transfer;
}


Data_Packet * init_data_packet(uint32_t packet_id, uint32_t transfer_start_id, uint16_t packet_bytes){
	Data_Packet * data_packet = (Data_Packet *) malloc(sizeof(Data_Packet));
	if (data_packet == NULL){
		fprintf(stderr, "Error: malloc failed allocating data packet\n");
		return NULL;
	}

	data_packet -> packet_id = packet_id;
	data_packet -> transfer_start_id = transfer_start_id;
	data_packet -> packet_bytes = packet_bytes;

	return data_packet;
}


int submit_out_transfer(Data_Channel * data_channel, uint8_t * fingerprint, void * addr, uint32_t data_bytes, uint32_t lkey, uint32_t start_id){

	int ret;

	// bool is_inbound = data_channel -> is_inbound;
	// assert(is_inbound == False)
	uint32_t sender_id = data_channel -> self_id;

	uint32_t packet_max_bytes = data_channel -> packet_max_bytes;
	uint32_t num_packets = ceil((double) data_bytes / (double) packet_max_bytes);
	uint32_t start_packet_id = start_id;

	// MAYBE NOT REALLY NECESSARY TO MAINTAIN OUT-BOUND ONGOING TRANSFERS...?
	// doing it for potentially later implementing fault-recovery and re-transmission...
	Transfer * transfer = init_transfer(start_packet_id, fingerprint, addr, data_bytes, lkey, false, num_packets);
	if (transfer == NULL){
		fprintf(stderr, "Error: could not initialize transfer when submitting outbound transfer\n");
		return -1;
	}

	if (find_item_table(data_channel -> transfers_table, transfer) != NULL){
		fprintf(stderr, "Error: transfer with id: %u, already in table\n", start_packet_id);
		return -1;
	}

	ret = insert_item_table(data_channel -> transfers_table, transfer);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert transfer into transfers table\n");
		return -1;
	}

	// OUTBOUND TRANSFERS DON'T CARE ABOUT CHANNEL COUNT LOCK BECAUSE CHANNEL COUNT IS PROVIDED

	uint32_t max_packet_id = data_channel -> max_packet_id;
	uint32_t last_packet_bytes = data_bytes - ((num_packets - 1) * packet_max_bytes);

	// MAYBE NOT REALLY NECESSARY TO MAINTAIN OUT-BOUND ONGOING TRANSFERS...?
	// doing it for potentially later implementing fault-recovery and re-transmission...
	uint32_t cur_channel_cnt = start_packet_id;
	uint32_t packet_bytes;
	Data_Packet * data_packet;
	for (uint32_t i = 0; i < num_packets; i++){
		// ensure wrap around so the encoding is correct
		if (cur_channel_cnt >= max_packet_id){
			cur_channel_cnt = 0;
		}
		// the last packet may have less bytes so treat it unique
		if (i == num_packets - 1){
			packet_bytes = last_packet_bytes;
		}
		else{
			packet_bytes = packet_max_bytes;
		}

		data_packet = init_data_packet(cur_channel_cnt, start_packet_id, packet_bytes);
		if (data_packet == NULL){
			fprintf(stderr, "Error: could not initialize data packet\n");
			return -1;
		}

		// Ensure packet-id is not already in table
		if (find_item_table(data_channel -> packets_table, data_packet) != NULL){
			fprintf(stderr, "Error: packet-id of %u, already in packets table\n", cur_channel_cnt);
			return -1;
		}

		ret = insert_item_table(data_channel -> packets_table, data_packet);
		if (ret != 0){
			fprintf(stderr, "Error: could not insert packet into table\n");
			return -1;
		}

		cur_channel_cnt += 1;
	}

	// START LOOP OF DOING IB-VERBS POST SENDS...
	
	// going from packet_id (channel count) to wr_id
	uint64_t encoded_wr_id;
	// advance addr with every packet
	uint64_t cur_addr = (uint64_t) addr;
	// reset channel count 
	cur_channel_cnt = start_packet_id;
	
	struct ibv_qp_ex * qp_ex = ibv_qp_to_qp_ex(data_channel -> qp);
    ibv_wr_start(qp_ex);
	
	
	for (uint32_t i = 0; i < num_packets; i++) {
		// ensure wrap around so the encoding is correct
		if (cur_channel_cnt >= max_packet_id){
			cur_channel_cnt = 0;
		}
		
		// the last packet may have less bytes so treat it unique
		if (i == num_packets - 1){
			packet_bytes = last_packet_bytes;
		}
		else{
			packet_bytes = packet_max_bytes;
		}

		encoded_wr_id = encode_wr_id(sender_id, cur_channel_cnt, DATA_PACKET);
		qp_ex -> wr_id = encoded_wr_id;
    	qp_ex -> wr_flags = 0; /* ordering/fencing etc. */
    	printf("Posting send with wr id: %lu\n", encoded_wr_id);
   		// Queue sends
    	ibv_wr_send(qp_ex);
    	// TODO: WHEN MOVING TO UD Queue PAIRS NEED TO ADD
    	// ibv_wr_set_ud_addr(qp_ex, ah, remote_qpn, remote_qkey)
    	ibv_wr_set_sge(qp_ex, lkey, cur_addr, packet_bytes);
		cur_channel_cnt += 1;
		cur_addr += packet_bytes;
	}
    // Call completete to post all the sends 
    ret = ibv_wr_complete(qp_ex);
    if (ret != 0){
        fprintf(stderr, "Error: issue with ibv_wr_complete within outbound transfer\n");
        return -1;
    }

	return 0;
}


int submit_in_transfer(Data_Channel * data_channel, uint8_t * fingerprint, void * recv_addr, uint32_t data_bytes, uint32_t lkey, uint32_t * ret_start_id) {

	int ret;

	// bool is_inbound = data_channel -> is_inbound;
	// assert(is_inbound == True)
	uint32_t sender_id = data_channel -> peer_id;

	uint32_t packet_max_bytes = data_channel -> packet_max_bytes;
	uint32_t num_packets = ceil((double) data_bytes / (double) packet_max_bytes);
	uint32_t max_packet_id = data_channel -> max_packet_id;
	

	// grab lock to determine start transfer id
	pthread_mutex_lock(&(data_channel -> transfer_start_id_lock));

	uint32_t cur_transfer_start_id = data_channel -> transfer_start_id;

	// TODO: ensure there is enough contiguous room for all the packets
	//			- for now, just holding the lock until packets are inserted into table

	// increment transfer start id by number of packets
	// for now just looping around, but this is very inefficient due to asynchrony of removal
	// TODO: should have a better data structure to track ranges (rb-tree)
	uint32_t next_transfer_start_id = (cur_transfer_start_id + num_packets) % max_packet_id;

	// should really be able to set new value for transfer start id
	// & release lock here (if the error handling is covered)
	//	- instead, fow now, relying on failover from inserting duplicate transfer/packet id's

	uint32_t start_packet_id = cur_transfer_start_id;

	// MAYBE NOT REALLY NECESSARY TO MAINTAIN OUT-BOUND ONGOING TRANSFERS...?
	// doing it for potentially later implementing fault-recovery and re-transmission...
	Transfer * transfer = init_transfer(start_packet_id, fingerprint, recv_addr, data_bytes, lkey, true, num_packets);
	if (transfer == NULL){
		fprintf(stderr, "Error: could not initialize transfer when submitting outbound transfer\n");
		pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
		return -1;
	}

	if (find_item_table(data_channel -> transfers_table, transfer) != NULL){
		fprintf(stderr, "Error: transfer with id: %u, already in table\n", start_packet_id);
		pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
		return -1;
	}

	ret = insert_item_table(data_channel -> transfers_table, transfer);
	if (ret != 0){
		fprintf(stderr, "Error: could not insert transfer into transfers table\n");
		pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
		return -1;
	}

	// OUTBOUND TRANSFERS DON'T CARE ABOUT CHANNEL COUNT LOCK BECAUSE CHANNEL COUNT IS PROVIDED

	
	uint32_t last_packet_bytes = data_bytes - ((num_packets - 1) * packet_max_bytes);

	// MAYBE NOT REALLY NECESSARY TO MAINTAIN OUT-BOUND ONGOING TRANSFERS...?
	// doing it for potentially later implementing fault-recovery and re-transmission...
	uint32_t cur_channel_cnt = start_packet_id;
	uint32_t packet_bytes;
	Data_Packet * data_packet;
	for (uint32_t i = 0; i < num_packets; i++){
		// ensure wrap around so the encoding is correct
		if (cur_channel_cnt >= max_packet_id){
			cur_channel_cnt = 0;
		}
		// the last packet may have less bytes so treat it unique
		if (i == num_packets - 1){
			packet_bytes = last_packet_bytes;
		}
		else{
			packet_bytes = packet_max_bytes;
		}

		data_packet = init_data_packet(cur_channel_cnt, start_packet_id, packet_bytes);
		if (data_packet == NULL){
			fprintf(stderr, "Error: could not initialize data packet\n");
			pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
			return -1;
		}

		// Ensure packet-id is not already in table
		if (find_item_table(data_channel -> packets_table, data_packet) != NULL){
			fprintf(stderr, "Error: packet-id of %u, already in packets table\n", cur_channel_cnt);
			pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
			return -1;
		}

		ret = insert_item_table(data_channel -> packets_table, data_packet);
		if (ret != 0){
			fprintf(stderr, "Error: could not insert packet into table\n");
			pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
			return -1;
		}

		cur_channel_cnt += 1;
	}

	// START LOOP OF DOING IB-VERBS POST Recvs...

	// going from packet_id (channel count) to wr_id
	uint64_t encoded_wr_id;
	// advance addr with every packet
	uint64_t cur_addr = (uint64_t) recv_addr;
	// reset channel count 
	cur_channel_cnt = start_packet_id;

	// REALLY SHOULD CONSIDER USING ibv_post_srq_recv() instead...
	// can have shared receive request queue among many different connections
	// the wr_id encoding ensures that the wr_id's will be unique among different cocnnections

	// Building linked list of receive work requests...
	struct ibv_recv_wr * recv_wr_head = malloc(sizeof(struct ibv_recv_wr));
	if (recv_wr_head == NULL){
		fprintf(stderr, "Error: malloc failed allocating recv wr\n");
		pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
		return -1;
	}
	struct ibv_sge * sg = malloc(num_packets * sizeof(struct ibv_sge));
	if (sg == NULL){
		fprintf(stderr, "Error: malloc failed allocating sg entries for recvs for inbound-transfer\n");
		pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
		return -1;
	}

	struct ibv_recv_wr * cur_wr = recv_wr_head;
	for (uint32_t i = 0; i < num_packets; i++){

		// ensure wrap around so the encoding is correct
		if (cur_channel_cnt >= max_packet_id){
			cur_channel_cnt = 0;
		}
		
		// the last packet may have less bytes so treat it unique
		if (i == num_packets - 1){
			packet_bytes = last_packet_bytes;
		}
		else{
			packet_bytes = packet_max_bytes;
		}

		encoded_wr_id = encode_wr_id(sender_id, cur_channel_cnt, DATA_PACKET);
		printf("Posting receive with wr id: %lu\n", encoded_wr_id);

		// set values for sge for this wr_id
		sg[i].addr = cur_addr;
		sg[i].length = packet_bytes;
		sg[i].lkey = lkey;

		cur_wr -> wr_id = encoded_wr_id;
		cur_wr -> sg_list = &sg[i];
		cur_wr -> num_sge = 1;

		// if not the last packet then
		if (i < num_packets - 1){
			cur_wr -> next = malloc(sizeof(struct ibv_recv_wr));
			if (cur_wr -> next == NULL){
				fprintf(stderr, "Error: malloc failed allocating recv wr\n");
				pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
				return -1;
			}
		}	
		else{
			cur_wr -> next = NULL;
		}
		
		// update values for next packet
		cur_channel_cnt += 1;
		cur_addr += packet_bytes;
		cur_wr = cur_wr -> next;
	}

	// Now we have a linked list of receive work-requests we can submit
	struct ibv_recv_wr * bad_wr;

	// if using shared-receive queue should have this part of data_channel struct
	// and instead use ibv_post_srq_recv()
	struct ibv_qp * qp = data_channel -> qp;
	ret = ibv_post_recv(qp, recv_wr_head, &bad_wr);
	if (ret != 0){
		fprintf(stderr, "Error: ibv_post_recv failed to post receives for inbound data transfer\n");
		pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));
		return -1;
	}

	// Transfer and all outstanding packets have been successfully inserted without duplicates
	// & all post_recvs submitted successfully,
	// So we can update value for next transfer id and release the transfer id lock
	data_channel -> transfer_start_id = next_transfer_start_id;
	pthread_mutex_unlock(&(data_channel -> transfer_start_id_lock));

	// set the optional return value
	if (ret_start_id != 0){
		*ret_start_id = start_packet_id;
	}

	return 0;
}

// Called upon data controller work completition of data_packet type
// 1.) Look up packet in packets table based on packed_id
// 2.) Retrieve the corresponding transfer based on transfer_start_id 
// 3.) Look up transfer in transfers table
// 4.) Acquire transfer lock
// 5.) Decrement remain_bytes and remain_packets
// 		- a.) If remain_packets == 0 => notify scheduler (for now just print) and remove/free transfer
// 6.) Release transfer lock
// 7.) Remove/free packet  
int ack_packet_local(Data_Channel * data_channel, uint32_t packet_id, Transfer_Complete ** ret_transfer_complete){

	// 1.) Look up packet
	Data_Packet target_data_packet;
	target_data_packet.packet_id = packet_id;

	Data_Packet * data_packet = find_item_table(data_channel -> packets_table, &target_data_packet);
	if (data_packet == NULL){
		fprintf(stderr, "Error: could not find packet with id: %u when doing ack_packet_local\n", packet_id);
		return -1;
	}
 
	uint32_t transfer_start_id = data_packet -> transfer_start_id;
	uint16_t packet_bytes = data_packet -> packet_bytes;

	// 2.) Look up transfer
	Transfer target_transfer;
	target_transfer.start_id = transfer_start_id;

	Transfer * transfer = find_item_table(data_channel -> transfers_table, &target_transfer);
	if (transfer == NULL){
		fprintf(stderr, "Error: could not find transfer with starting id: %u when doing ack_packet_local from packet_id: %u\n", transfer_start_id, packet_id);
		return -1;
	}


	// 3.) Modify transfer remaining and see if complete
	pthread_mutex_lock(&(transfer -> transfer_lock));

	if (packet_bytes > (transfer -> remain_bytes)){
		fprintf(stderr, "Error: remaining bytes would be set to negative. Something went wrong\n");
		return -1;
	}

	transfer -> remain_bytes -= packet_bytes;
	transfer -> remain_packets -= 1;

	Transfer_Complete * transfer_complete = NULL;
	if (transfer -> remain_packets == 0){

		// assert remain bytes is now 0...
		if (transfer -> remain_bytes > 0){
			fprintf(stderr, "Error: all packets are completed but remaining bytes > 0\n");
			return -1;
		}

		// a.) Create and set a completition return value
		transfer_complete = (Transfer_Complete *) malloc(sizeof(Transfer_Complete));
		if (transfer_complete == NULL){
			fprintf(stderr, "Error: malloc failed to allocate transfer complete struct\n");
			return -1;
		}
		transfer_complete -> start_id = transfer_start_id;
		memcpy(transfer_complete -> fingerprint, transfer -> fingerprint, FINGERPRINT_NUM_BYTES);
		transfer_complete -> addr = transfer -> addr;
		transfer_complete -> data_bytes = transfer -> data_bytes;

		// b.) Now can remove and destroy transfer
		Transfer * removed_transfer = remove_item_table(data_channel -> transfers_table, &target_transfer);
		// ASSERT (removed_transfer == transfer from above)
		if (removed_transfer == NULL){
			fprintf(stderr, "Error: could not remove transfer from table, something went wrong\n");
			return -1;
		}
		pthread_mutex_destroy(&(removed_transfer -> transfer_lock));
		free(removed_transfer);
	}
	else{
		// Can unlock the remaining packets counter and free packet..?
		pthread_mutex_unlock(&(transfer -> transfer_lock));
	}

	// Now can remove packet from the packets table
	Data_Packet * removed_data_packet = remove_item_table(data_channel -> packets_table, &target_data_packet);
	// ASSERT (removed_data_packet == data_packet from above)
	if (removed_data_packet == NULL){
		fprintf(stderr, "Error: could not remove packet from table, something went wrong\n");
		return -1;
	}

	free(removed_data_packet);

	// SHOULD NEVER BE NULL BUT CHECKING JUST IN CASE
	if (ret_transfer_complete != NULL){
		// will be set to newly allocated memory if completition, otherwise Null 
		*ret_transfer_complete = transfer_complete;
	}

	return 0;
}


// COULD BECOME A MACRO TO OVERCOME ~10ns function call overhead...
uint32_t decode_packet_id(uint64_t wr_id){
	// Message Type takes up top 8 bits so clear that out, then shift right sender_id bits + message_size bits
	int message_type_bits = 8;
	int sender_id_bits = 32;
	return (wr_id << (message_type_bits)) >> (message_type_bits + sender_id_bits);
}


