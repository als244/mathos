#ifndef CUDA_KERNELS_H
#define CUDA_KERNELS_H

#ifndef CUDA_H
#define CUDA_H
#include <cuda.h>
#endif

#include <cuda_fp16.h>
#include <mma.h>

#include "common.h"
#include "cuda_ptx_macros.h"





#define CONST_ZERO_FP16 0x0000U
#define CONST_ONE_FP16 0x3C00U

#define CONST_ZERO_DEV_FP16 __ushort_as_half((unsigned short)0x0000U)
#define CONST_ONE_DEV_FP16 __ushort_as_half((unsigned short) 0x3C00U)
#define POS_INF_DEV_FP16 __ushort_as_half((unsigned short) 0x7C00U)
#define NEG_INF_DEV_FP16 __ushort_as_half((unsigned short) 0xFC00U)

#define CONST_FLOAT_INF 0x7f800000
#define CONST_FLOAT_NEG_INF 0xff800000


#define ROUND_UP_TO_MULTIPLE(x, multiple) (((x + multiple - 1) / multiple) * multiple)




// General math

extern "C" __global__ void convert_dev_floats(int N, float * float_ptr, __half * half_ptr);

extern "C" __global__ void add_fp16_kernel(size_t N, __half * d_A, __half * d_B, __half * d_out);
extern "C" __global__ void add_fp16_vec_kernel(size_t N, __half2 * d_A, __half2 * d_B, __half2 * d_out);
extern "C" __global__ void add_fp32_kernel(size_t N, float * d_A, float * d_B, float * d_out);

// Out of place tranpose
extern "C" __global__ void transpose_fp16_kernel(int n_orig_rows, int n_orig_cols, const __half * __restrict__ in, __half * __restrict__ out);

extern "C" __global__ void matmul_fp16_kernel(const half *__restrict__ A, const half *__restrict__ B, half *__restrict__ C,
                                     size_t M, size_t N, size_t K, float alpha, float beta);

extern "C" __global__ void naive_matmul_fp16_kernel(int M, int K, int N, float alpha, __half *A, __half *B, float beta, __half *C);

// Pipeline specific
extern "C" __global__ void rms_norm_fp16_kernel(float eps, int n_rows, int n_cols, __half * rms_weight, __half * X, __half * out, float * sq_sums);

extern "C" __global__ void rope_fp16_kernel(int theta, uint64_t N, int model_dim, int head_dim, int num_kv_heads, int * seq_positions, __half * X_q, __half * X_k);

extern "C" __global__ void copy_kv_to_seq_context_fp16_kernel(uint64_t N, int total_tokens, int kv_dim, __half * keys, __half * values, int * seq_positions, uint64_t * seq_context_ptrs, int * seq_context_sizes);

extern "C" __global__ void attention_fp16_kernel(int model_dim, int q_group_dim, int kv_dim, int head_dim, uint64_t * block_configs, int * seq_positions, __half * queries, uint64_t * seq_context_ptrs, int * seq_context_sizes, __half * out);

extern "C" __global__ void silu_hadamard_fp16_kernel(uint64_t N, __half * X_w1, __half * X_w3, __half * out);


extern "C" __global__ void condense_rows_fp16_kernel(uint64_t N, int n_rows, int n_cols, __half * X_in, __half * X_out, int * row_remapping);

extern "C" __global__ void softmax_fp16_kernel(int n_cols, __half * X);
extern "C" __global__ void softmax_fp16_to_float_kernel(int n_cols, __half * X_in, float * out, uint32_t * arg_maxs);


// BACKWARDS KERNELS
extern "C" __global__ void cross_entropy_loss_fp16_kernel(int n_rows, int n_cols, __half * pred_logits, uint32_t * labels);


extern "C" __global__ void rms_norm_bwd_weight_fp16_kernel(float eps, int n_rows, int n_cols, __half * X_inp, float * sq_sums, __half * upstream_dX, __half * dW);
extern "C" __global__ void rms_norm_bwd_inp_fp16_kernel(float eps, int n_rows, int n_cols, __half * rms_weight, __half * X_inp, float * sq_sums, __half * upstream_dX, __half * dX);

#endif