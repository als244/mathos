#ifndef CUBLAS_HELPER_H
#define CUBLAS_HELPER_H

#include "common.h"
#include "memory_client.h"

#ifndef CUDA_H
#define CUDA_H
#include <cuda.h>
#endif

#include <cublasLt.h>

typedef struct cublas_matmul_desc {
	// normally will only initialize
	// with N and K and let every stage
	// alter M/Adesc
	int M;
	int K;
	int N;
	cublasComputeType_t compute_type;
	cudaDataType scale_type;
	cublasLtMatmulDesc_t matmul_desc;
	cublasLtMatrixLayout_t Adesc;
	cublasLtMatrixLayout_t Bdesc;
	cublasLtMatrixLayout_t Cdesc;
	cublasLtMatrixLayout_t Ddesc;
	cublasLtMatmulPreference_t pref;
	cublasLtMatmulAlgo_t algo;
	int has_algo;
} Cublas_Matmul_Desc;

int create_cublas_matmul_descriptor(cublasLtHandle_t handle, Cublas_Matmul_Desc  * desc, int M, int K, int N, DataType A_dt, DataType B_dt, DataType C_dt,
									bool is_a_row_major, bool is_b_row_major, bool is_c_row_major, bool use_fp32_accum, uint64_t workspace_bytes);

int destroy_cublas_matmul_descriptor(Cublas_Matmul_Desc * desc);

int do_cublas_matmul_fp16(CUstream compute_stream, cublasLtHandle_t handle, void * workspace, uint64_t workspace_bytes, int M, int K, int N, 
							void * alpha, void * A, bool is_a_row_major, void * B, bool is_b_row_major, void * beta, void * C, bool is_c_row_major,
							bool use_fp32_accum, Cublas_Matmul_Desc * supplied_desc, Cublas_Matmul_Desc * save_desc);


#endif