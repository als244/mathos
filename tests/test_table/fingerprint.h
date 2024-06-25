#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include "common.h"

#include <openssl/sha.h>

#define HASH_DIGEST_LEN_BYTES 32

void print_hex(unsigned char * digest, int num_bytes);
void print_sha256(unsigned char * data);
uint64_t sha256_to_least_sig64(unsigned char * data);
void do_sha256(void * data, uint64_t num_bytes, unsigned char * ret_digest);

#endif