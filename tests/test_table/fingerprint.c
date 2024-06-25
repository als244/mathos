#include "fingerprint.h"

#define SEED 1

void print_hex(unsigned char * data, int num_bytes){
	for (int i = 0; i < num_bytes; i++){
		printf("%02hhux", data[i]);
	}
	printf("\n");
}

void print_sha256(unsigned char * data){
	// sha256 = 256 bits = 32 bytes
	int num_bytes = 32;
	for (int i = 0; i < num_bytes; i++){
		printf("%02x", data[i]);
	}
	printf("\n");
}

uint64_t sha256_to_least_sig64(unsigned char * digest_start){
	unsigned char * least_sig_start = digest_start + HASH_DIGEST_LEN_BYTES - sizeof(uint64_t);
	uint64_t result = 0;
    for(int i = 0; i < 8; i++){
        result <<= 8;
        result |= (uint64_t)least_sig_start[i];
    }
    return result;
}

void do_sha256(void * data, uint64_t num_bytes, unsigned char * ret_digest){

	// setting random seed
	
	srand(SEED);
	
	/* ACTUALLY DO SHA-256 */

	// Optional to pass in hash_digest, if so then it fills that buffer, otherwise allocates a new one...
	//unsigned char * res = SHA256((const unsigned char *) data, byte_count, NULL);
	SHA256((const unsigned char *) data, num_bytes, ret_digest);
	
	//clock_gettime(CLOCK_MONOTONIC, &end);

	//timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
	//timestamp_end = end.tv_sec * 1e9 + end.tv_nsec;

	//uint64_t elapsed_ns = timestamp_end - timestamp_start;

	//printf("%lu\n", elapsed_ns);

	/* PRINTING HASH DIGEST */

	/*
	// Print hex format	
	for (int i = 0; i < digest_bytes; i++){
		printf("%02x", hash_digest[i]);
	}
	printf("\n");
	*/


	return;
}