#include "utils.h"

char * byte_arr_to_hex_str(uint64_t arr_len, uint8_t * byte_arr) {

	char * out_str = malloc(arr_len * 2 + 1);
	
	char * cur_dest = out_str;
	for (uint64_t i = 0; i < arr_len; i++){
		sprintf(cur_dest, "%02x", byte_arr[i]);
		cur_dest += 2;
	}
	out_str[arr_len * 2] = '\0';
	return out_str;
}


char * message_class_to_str(CtrlMessageClass message_class) {

	switch(message_class){
		case EXCHANGE_CLASS:
			return "EXCHANGE_CLASS";
		case INVENTORY_CLASS:
			return "INVENTORY_CLASS";
		case REQUEST_CLASS:
			return "REQUEST_CLASS";
		case INGEST_CLASS:
			return "INGEST_CLASS";
		case CONFIG_CLASS:
			return "CONFIG_CLASS";
		case ALERT_CLASS:
			return "ALERT_CLASS";
		case SYSTEM_CLASS:
			return "SYSTEM_CLASS";
		default:
			return "UNKNOWN_CLASS";
	}
}

void uint64_to_str_with_comma(char *buf, uint64_t val) {
    if (val < 1000) {
        sprintf(buf+strlen(buf), "%lu", val);
        return;
    }
    uint64_to_str_with_comma(buf, val / 1000);
    sprintf(buf+strlen(buf), ",%03lu", val % 1000);
    return;
}

