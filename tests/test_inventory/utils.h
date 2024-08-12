#ifndef UTILS_H
#define UTILS_H

#include "common.h"
#include "config.h"

// allocates a string and assumes caller will free
char * byte_arr_to_hex_str(uint64_t arr_len, uint8_t * byte_arr);

// assumes out_str is allocated
void copy_byte_arr_to_hex_str(char * out_str, uint64_t arr_len, uint8_t * byte_arr);

void copy_id_list_to_str(char * out_str, uint32_t num_ids, uint32_t * id_list);

char * message_class_to_str(CtrlMessageClass message_class);

void uint64_to_str_with_comma(char *buf, uint64_t val);

uint8_t log_uint64_base_2(uint64_t n);

#endif