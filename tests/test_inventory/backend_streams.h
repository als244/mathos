#ifndef BACKEND_STREAMS_H
#define BACKEND_STREAMS_H

#include "common.h"

#include "memory_client.h"

#include "cuda_memory.h"
#include "cublas_helper.h"
#include "backend_profile.h"
#include "backend_funcs.h"


#include <cuda.h>

int init_stream_group(Memory * memory, Stream_Group * stream_group_ref, char * stream_name, int dev_id, int prio, uint64_t stream_workspace_bytes);


// typical usage would be to have 1 stream per pipeline stage and not supply custom prios 
//	- (default behavior is further stages within a given device will have precendence == lower set prio value)
// supplied prios is optional, but if it is passed in must have length = total_compute_streams
int init_backend_streams(Backend_Funcs * backend_funcs, int num_devices, int * dev_ids, CUcontext * contexts, int total_compute_streams, int * compute_streams_per_device, int * supplied_prios, uint64_t stream_workspace_bytes);


// might need to add context which needs to be set before recording stream (if multi pool == multi-gpu)...?
int backend_add_dependent_stream(void * cur_stream_ref, void * stream_to_wait_on_ref, void * to_wait_on_event_ref);


#endif