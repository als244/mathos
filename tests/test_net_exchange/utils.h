#ifndef UTILS_H
#define UTILS_H

#include "common.h"
#include "config.h"

char * byte_arr_to_hex_str(uint64_t arr_len, uint8_t * byte_arr);

char * message_class_to_str(CtrlMessageClass message_class);

#endif