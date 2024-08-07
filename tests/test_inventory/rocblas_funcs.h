#ifndef ROCBLAS_FUNCS_H
#define ROCBLAS_FUNCS_H

#include "common.h"

#include "hip/hip_runtime_api.h"
#include "rocblas/rocblas.h"


int initialize_stream(int device_id, unsigned int cu_count, hipStream_t * ret_stream);

int do_rocblas_matmul(hipStream_t cu_mask_stream, size_t M, size_t K, size_t N, void * d_A, void * d_B, void * d_C, uint64_t * ret_elapsed_ns);


#endif