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


// HASH FUNCTION STUFF

static uint8_t pearson_table[] = { 64, 67, 45, 157, 253, 237, 236, 112, 17, 69, 61, 182, 173, 44, 235, 153, 102, 223, 251, 95, 166, 136, 160, 25, 60, 198, 146, 62, 87, 200, 71, 169, 220, 5, 131, 133, 138, 99, 73, 48, 129, 96, 130, 139, 233, 246, 248, 41, 150, 175, 98, 214, 74, 177, 66, 219, 105, 78, 65, 32, 94, 16, 240, 14, 68, 174, 37, 81, 238, 107, 88, 135, 13, 180, 132, 28, 155, 222, 228, 70, 92, 232, 163, 168, 103, 167, 190, 91, 206, 205, 24, 179, 161, 255, 83, 31, 208, 12, 148, 189, 79, 106, 51, 137, 122, 159, 178, 224, 72, 191, 225, 11, 18, 193, 8, 126, 84, 50, 114, 140, 247, 215, 9, 221, 171, 152, 196, 59, 242, 35, 244, 56, 213, 164, 76, 209, 245, 226, 197, 75, 158, 216, 147, 211, 52, 186, 1, 195, 201, 46, 172, 165, 85, 170, 185, 0, 254, 7, 80, 144, 141, 6, 89, 217, 128, 86, 30, 121, 113, 184, 199, 252, 110, 57, 188, 54, 231, 183, 187, 156, 63, 47, 39, 118, 124, 90, 207, 43, 116, 2, 26, 212, 202, 234, 203, 218, 93, 210, 34, 55, 29, 101, 192, 21, 40, 49, 249, 82, 100, 230, 204, 20, 125, 239, 145, 108, 111, 27, 176, 4, 53, 241, 229, 3, 143, 77, 15, 149, 23, 154, 58, 36, 109, 123, 134, 119, 162, 104, 243, 127, 22, 42, 10, 19, 227, 115, 117, 33, 250, 38, 97, 151, 120, 194, 142, 181 };

// THE FOLLOWING OPTIONS ASSUMES BOTH AN INPUT AND OUT SPACE of x bits where hash_func_x
// They will be used to map indexes within each level of the Fast_Tree

// Mixing hash function
// Taken from "https://github.com/shenwei356/uint64-hash-bench?tab=readme-ov-file"
// Credit: Thomas Wang
uint64_t hash_func_64(void * key_ref, uint64_t table_size) {
	uint64_t key = *((uint64_t *) key_ref);
	key = (key << 21) - key - 1;
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);
	return key % table_size;
}


uint64_t hash_func_32(void * key_ref, uint64_t table_size) {
	uint32_t key = *((uint32_t *) key_ref);
	// Take from "https://gist.github.com/badboy/6267743"
	// Credit: Robert Jenkins
	key = (key+0x7ed55d16) + (key<<12);
   	key = (key^0xc761c23c) ^ (key>>19);
   	key = (key+0x165667b1) + (key<<5);
   	key = (key+0xd3a2646c) ^ (key<<9);
   	key = (key+0xfd7046c5) + (key<<3);
   	key = (key^0xb55a4f09) ^ (key>>16);
	return key % table_size;
}

// Using Pearson hashing for 8 and 16 bits

// Ref: https://64nops.wordpress.com/wp-content/uploads/2020/11/pearson.acm33.1990-3.pdf
//		- https://64nops.wordpress.com/2020/12/10/hash-algorithms/
uint64_t hash_func_16(void * key_ref, uint64_t table_size){
	
	uint16_t key = *((uint16_t *) key_ref);
	
	uint8_t hash1 = pearson_table[pearson_table[key & 0x00FF] ^ (key & 0xFF00)];
	uint8_t hash2 = pearson_table[pearson_table[(key & 0x00FF) ^ 1] ^ (key & 0xFF00)];
	return ((hash2 << 8) + hash1) % table_size;
}

// this pearson hash can also be used for 16 bits
// with a table of 65k

// Ref: https://bannister.us/weblog/2005/a-diversion-into-hash-tables-and-binary-searches
uint64_t hash_func_8(void * key_ref, uint64_t table_size){
	uint8_t key = *((uint8_t *) key_ref);
	return pearson_table[key] % table_size;
}