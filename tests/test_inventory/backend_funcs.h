#ifndef BACKEND_FUNCS_H
#define BACKEND_FUNCS_H

#include "common.h"
#include "memory_client.h"
#include "cublas_helper.h"
#include "backend_profile.h"

#include <cuda.h>


typedef struct stream_group {
	char stream_name[PATH_MAX];
	int dev_id;
	int prio;
	uint64_t stream_workspace_bytes;
	void * compute_stream_ref;
	void * inbound_stream_ref;
	void * outbound_stream_ref;
	void * compute_event_ref;
	void * inbound_event_ref;
	void * outbound_event_ref;
	Mem_Ref workspace_mem_ref;
} Stream_Group;

typedef struct backend_funcs {
	Memory * memory;
	CUmodule module;
	CUfunction add_fp16;
	CUfunction transpose_fp16;
	// simpler to use this instread of
	// linking with cublas
	// but not convinced this works/might be way worse
	CUfunction matmul_fp16;
	CUfunction naive_matmul_fp16;
	CUfunction rms_norm_fp16;
	CUfunction rope_fp16;
	// copying the keys is alreay handled within rope...
	CUfunction copy_kv_to_seq_context_fp16;
	CUfunction attention_fp16;
	CUfunction silu_hadamard_fp16;
	CUfunction condense_rows_fp16;
	CUfunction softmax_fp16;
	CUfunction softmax_fp16_to_float;
	CUfunction cross_entropy_loss_fp16;

	// max opt-in memory (not including the required static smem per function)
	int dev_max_smem;
	int matmul_fp16_smem;
	int rms_norm_max_threads_per_block;
	int rms_norm_smem;
	// impacts how seq batching decisions are made
	int attn_max_smem;
	int attn_max_threads_per_block;
	// how many warps to launch with
	int attn_warps_per_block;

	// for now just 1 per device...
	// would probably be better to have 1
	// per pipeline stage
	// this stuff is initialized in relationship to pipeline
	// probably should be moved to a different struct....
	// should be one stream per stage with priority set
	// so that further stages within a device will have 
	// precedence
	int num_devices;
	CUcontext * contexts;
	// make default compute streams for every device
	CUstream default_compute_streams[MAX_LOCAL_DEVS];

	// only 1 layer moving per pool at a time
	CUstream layer_inbound_streams[MAX_LOCAL_DEVS];
	// here the first num_devices will be populated
	// with corersponding device id's from cuda perspective
	int dev_ids[MAX_LOCAL_DEVS];
	int compute_streams_per_device[MAX_LOCAL_DEVS];
	int total_compute_streams;
	
	// outer array is max number of devices
	// inner array is number of stream groups per device
	Stream_Group * stream_groups[MAX_LOCAL_DEVS];

	// all active streams will have wokrspace allocated
	cublasLtHandle_t cublas_handles[MAX_LOCAL_DEVS];
	uint64_t stream_workspace_bytes;
	// every stream wtihin a pool gets their own workspace
	Mem_Ref * cublas_workspace_mem[MAX_LOCAL_DEVS];
} Backend_Funcs;


Backend_Funcs * init_backend_funcs(Memory * memory);


#endif