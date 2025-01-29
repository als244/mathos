#ifndef CUDA_MEMORY_H
#define CUDA_MEMORY_H

#include <cuda.h>

#include "common.h"
#include "memory.h"
#include "backend_profile.h"

typedef enum stream_direction {
	STREAM_INBOUND,
	STREAM_OUTBOUND,
	STREAM_CUSTOM
} StreamDirection;

// Probably should convert this into system-wide struct (nothing specific to this hsa backend)

// However potentially want to leave opportunity for other backends to manage memory differently (i.e. physical chunks and mappings)
typedef struct cuda_user_page_table {
	// This is using our systems lingo
	// Num devices does not include CPU
	int num_devices;
	// Each of these are array of length num_devices
	uint64_t * num_chunks;
	uint64_t * chunk_size;
	// outer index is the device number (length num_devices)
	// inner index is the starting VA for entire device allocation
	// 	- can reference individual chunks by multiplying chunk_id * chunk_size for device
	void ** virt_memories;
} Cuda_User_Page_Table;


typedef struct cuda_memory {
	// this includes the CPU as an agent
	int num_devices;
	CUcontext * contexts;
	Cuda_User_Page_Table * user_page_table;
	CUstream * inbound_streams;
	CUstream * outbound_streams;
} Cuda_Memory;



int cuda_add_device_memory(Cuda_Memory * cuda_memory, int device_id, uint64_t num_chunks, uint64_t chunk_size);

int cuda_copy_to_host_memory(Cuda_Memory * cuda_memory, int src_device_id, void * src_addr, uint64_t length, void * ret_contents);

int cuda_async_copy_to_host_memory(Cuda_Memory * cuda_memory, int src_device_id, void * src_addr, uint64_t length, void * ret_contents, void * stream_ref);

int cuda_copy_to_device_memory(Cuda_Memory * cuda_memory, void * contents, int dest_device_id, void * dest_addr, uint64_t length);

int cuda_async_copy_to_device_memory(Cuda_Memory * cuda_memory, void * contents, int dest_device_id, void * dest_addr, uint64_t length, void * stream_ref);
int cuda_async_dev_copy_to_device_memory(Cuda_Memory * cuda_memory, void * contents, int dest_device_id, void * dest_addr, uint64_t length, void * stream_ref);

int init_backend_memory(Memory * memory, uint64_t dev_num_chunks, uint64_t dev_chunk_size);

// can be glitchy if the program exists without unregistering using cuMemHostUnregister on the pinned sys_mem_buffer
int unregister_backend_sys_mapping(Memory * memory);

int push_context(CUcontext ctx);
int pop_current_context(CUcontext * ret_ctx);

int add_callback_to_post_complete(Cuda_Memory * cuda_memory, int device_id, StreamDirection stream_direction, CUstream custom_stream, sem_t * sem_to_post);
void CUDA_CB postSemCallback(CUstream stream, CUresult status, void * data);


#endif