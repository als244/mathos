#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include "common.h"

#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/evp.h>

typedef enum fingerprint_type{
	SHA256_HASH = 0, // 32 bytes
	SHA512_HASH = 1, // 64 byte
	SHA1_HASH = 2, // 20 bytes
	MD5_HASH = 3, // 16 bytes
	BLAKE3_HASH = 4, // 32 bytes
} FingerprintType;


void print_hex(unsigned char * digest, int num_bytes);
void print_sha256(unsigned char * digest);
uint64_t digest_to_least_sig64(unsigned char * digest, int digest_num_bytes);
uint8_t get_fingerprint_num_bytes(FingerprintType fingerprint_type);
char * get_fingerprint_type_name(FingerprintType fingerprint_type);
void do_fingerprinting(void * data, uint64_t num_bytes, unsigned char * ret_digest, FingerprintType fingerprint_type);
void do_fingerprinting_evp(void * data, uint64_t num_bytes, unsigned char * ret_digest);

#endif