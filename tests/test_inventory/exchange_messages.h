#ifndef EXCHANGE_MESSAGES_H
#define EXCHANGE_MESSAGES_H

#include "common.h"

typedef enum exch_message_type {
	BID_ORDER,
	BID_CANCEL_ORDER,
	BID_Q,
	BID_Q_RESPONSE,
	OFFER_ORDER,
	OFFER_CONFIRM_MATCH_DATA_ORDER,
	OFFER_CANCEL_ORDER,
	OFFER_Q,
	OFFER_Q_RESPONSE,
	FUTURE_ORDER,
	FUTURE_CANCEL_ORDER,
	FUTURE_Q,
	FUTURE_Q_RESPONSE
} ExchMessageType;



// ALL EXCHANGE COMMUNICATIONS ONLY NEED FINGERPRINT AND SOURCE_ID
//	- source id is found within control message header

typedef struct exch_message {
	ExchMessageType message_type;
	uint64_t content_size;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
} Exch_Message;




#endif