#include "function.h"

// Assumes memory has been allocated for ret_fingerprint already
int encode_function(Function * function, uint8_t * ret_fingerprint) {

	SHA256_CTX ctx;
	SHA256_Init(&ctx);

	// first hash the meta data
	SHA256_Update(&ctx, function -> metadata, sizeof(Function_Metadata));
	
	// now hash all the argument fingerprints
	int num_args = function -> metadata -> num_args;
	uint64_t fingerprint_bytes = (uint64_t) function -> fingerprint_bytes;
	uint8_t ** argument_fingerprints = function -> argument_fingerprints;

	for (int i = 0; i < num_args; i++){
		SHA256_Update(&ctx, argument_fingerprints[i], fingerprint_bytes);
	}

	SHA256_Final(ret_fingerprint, &ctx);

	return 0;
}

Function * init_function(FunctionType function_type, DataType data_type, int num_args, uint8_t ** argument_fingerprints, uint8_t fingerprint_bytes){

	Function * f = (Function *) malloc(sizeof(Function));
	if (f == NULL){
		fprintf(stderr, "Error: malloc failed when trying to allocate function\n");
		return NULL;
	}

	Function_Metadata * meta = (Function_Metadata *) malloc(sizeof(Function_Metadata));
	if (meta == NULL){
		fprintf(stderr, "Error: malloc failed when trying to allocation function metadata\n");
		return NULL;
	}


	meta -> function_type = function_type;
	meta -> data_type = data_type;
	meta -> num_args = num_args;
	f -> metadata = meta;
	f -> argument_fingerprints = argument_fingerprints;
	f -> fingerprint_bytes = fingerprint_bytes;

	f -> output_fingerprint = malloc(fingerprint_bytes);
	encode_function(f, f -> output_fingerprint);

	return f;
}


