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

