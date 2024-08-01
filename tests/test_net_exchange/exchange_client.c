#include "exchange_client.h"


// Returns 0 on error (master node id)
uint32_t determine_exchange(System * system, uint8_t * fingerprint) {

	Net_World * net_world = system -> net_world;

	uint32_t num_exchanges;

	// For now assuming each node has an exchange and there are no others
	//	- realistically makes sense to have dedicated exchange nodes however
	//		- but many clusters are built such that each node is uniform and there isn't special topology 
	//			or memory-heavy + network-heavy + compute-weak nodes
	
	num_exchanges = get_count_table(net_world -> nodes);
	uint64_t least_sig64 = fingerprint_to_least_sig64(fingerprint, FINGERPRINT_NUM_BYTES);

	// because Master Node has ID 0 and it doesn't have an exchange
	// we want to add 1 to the destination (this is valid because the
	// the highest node id will actually be num_exchanges + 1)
	uint64_t dest_exchange = (least_sig64 % num_exchanges) + 1;
	return dest_exchange;
}


int submit_exchange_order(System * system, uint8_t * fingerprint, ExchMessageType exch_message_type) {

	int ret;

	Net_World * net_world = system -> net_world;
	
	uint32_t self_id = net_world -> self_node_id;
	Exchange * self_exchange = system -> exchange;


	uint32_t target_exchange_id = determine_exchange(system, fingerprint);


	// 1.) Build the control message


	Ctrl_Message exch_ctrl_message;
	exch_ctrl_message.header.source_node_id = self_id;
	exch_ctrl_message.header.dest_node_id = target_exchange_id;
	exch_ctrl_message.header.message_class = EXCHANGE_CLASS;

	// Set the exchange message within control message class

	// cast the contents buffer within control message to exchange message so we can easily write to it
	Exch_Message * exch_message = (Exch_Message *) (&exch_ctrl_message.contents);
	exch_message -> message_type = exch_message_type;
	memcpy(exch_message -> fingerprint, fingerprint, FINGERPRINT_NUM_BYTES);


	

	// 2.) Now need to check if we should post to self or send a control message
	//		- if posting to self we might have to send out control messages in response to matches

	if (target_exchange_id == self_id){

		// a.) Actually post to self exchange

		uint32_t num_triggered_ctrl_messages;
		Ctrl_Message * triggered_ctrl_messages;

		ret = do_exchange_function(self_exchange, &exch_ctrl_message, &num_triggered_ctrl_messages, &triggered_ctrl_messages);
		if (ret != 0){
			fprintf(stderr, "Error: when submitting an exchange message to self exchange, do_exchange_function failed\n");
			return -1;
		}

		// b.) Potentially send triggered responses
		//		(and free the triggered_ctrl_messages array that was allocated within do_exchange_function)

		// now send out all the triggered messages
		// this may block depending on size of send queue...
		for (uint32_t i = 0; i < num_triggered_ctrl_messages; i++){

			ret = post_send_ctrl_net(net_world, &(triggered_ctrl_messages[i]));
			if (ret != 0){
				fprintf(stderr, "Error: post_send_ctrl_net failed when sending out triggered ctrl messages from exchange (triggered message #%u)\n", i);
				// still free the array that was allocated within do_exchange_function
				free(triggered_ctrl_messages);
				return -1;
			}
		}
		free(triggered_ctrl_messages);
	}
	else{

		// just send out the contol message
		ret = post_send_ctrl_net(net_world, &exch_ctrl_message);
		if (ret != 0){
			fprintf(stderr, "Error: post_send_ctrl_net failed when submitting an exchange order (type %d) from node id %u -> node id %u\n", exch_message_type, self_id, target_exchange_id);
			return -1;
		}
	}
	return 0;
}

