#include "fingerprint.h"

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

	
	/* ACTUALLY DO SHA-256 */

	// Optional to pass in hash_digest, if so then it fills that buffer, otherwise allocates a new one...
	//unsigned char * res = SHA256((const unsigned char *) data, byte_count, NULL);
	SHA256((const unsigned char *) data, num_bytes, ret_digest);

	return;
}