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


void copy_byte_arr_to_hex_str(char * out_str, uint64_t arr_len, uint8_t * byte_arr) {
	
	char * cur_dest = out_str;
	for (uint64_t i = 0; i < arr_len; i++){
		sprintf(cur_dest, "%02x", byte_arr[i]);
		cur_dest += 2;
	}
	out_str[arr_len * 2] = '\0';
	return;
}



void copy_id_list_to_str(char * out_str, uint32_t num_ids, uint32_t * id_list) {

	char * cur_dest = out_str;
	
	int total_len = 0;
	int num_written;

	for (uint32_t i = 0; i < num_ids; i++){
		if (i != num_ids - 1){
			num_written = sprintf(cur_dest, "%u, ", id_list[i]);
		}
		else{
			num_written = sprintf(cur_dest, "%u", id_list[i]);
		}
		cur_dest += num_written;
		total_len += num_written;
	}
	out_str[total_len] = '\0';
	return;
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


uint8_t log_uint64_base_2(uint64_t n) {
	return (uint8_t) (63 - __builtin_clzll(n));
}

