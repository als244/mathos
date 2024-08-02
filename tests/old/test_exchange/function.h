#ifndef FUNCTION_H
#define FUNCTION_H

#include "common.h"
#include "fingerprint.h"

typedef enum function_type {
	MATMUL = 0,
	MATVEC = 1,
	CONV = 2,
} FunctionType;

typedef enum data_type {
	FP8 = 0,
	FP16 = 1,
	FP32 = 2,
	FP64 = 3,
	INT8 = 4,
	INT16 = 5,
	INT32 = 6,
	INT64 = 7,
	UINT8 = 8,
	UINT16 = 9,
	UINT32 = 10,
	UINT64 = 11,
	BOOL = 12
} DataType;

typedef struct function_metadata {
	FunctionType function_type;
	DataType data_type;
	int num_args;
} Function_Metadata;


typedef struct function {
	Function_Metadata * metadata;
	// here is an array of num_args where each element is a fingerprint
	unsigned char ** argument_fingerprints;
	// size of each fingerprint
	uint8_t fingerprint_bytes;
	// still need to deal with format for each function type and args...
	// i.e. dealing with dimensionalities and constant arguments
	// need to know the input / output dimensions based on some info!
	unsigned char * output_fingerprint;
} Function;



Function * init_function(FunctionType function_type, DataType data_type, int num_args, unsigned char ** argument_fingerprints, uint8_t fingerprint_bytes);


int encode_function(Function * function, unsigned char * ret_fingerprint);

#endif