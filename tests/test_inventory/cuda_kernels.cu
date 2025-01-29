#include "cuda_kernels.h"

inline __device__ __host__ size_t div_ceil(size_t a, size_t b) {
    return (a % b != 0) ? (a / b + 1) : (a / b);
}


extern "C" __global__ void convert_dev_floats(int N, float * float_ptr, __half * half_ptr){

	size_t i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i < N){
		half_ptr[i] = __float2half(float_ptr[i]);
	}
}


extern "C" __global__ void add_fp16_kernel(size_t N, __half * d_A, __half * d_B, __half * d_out) {

	size_t i = blockIdx.x * blockDim.x + threadIdx.x;
	if (i < N){
		d_out[i] = d_A[i] + d_B[i];
	}
}

// should launch with N / 2
extern "C" __global__ void add_fp16_vec_kernel(size_t N, __half2 * d_A, __half2 * d_B, __half2 * d_out) {

	size_t i = blockIdx.x * blockDim.x + threadIdx.x;

	if (i < N / 2){
		d_out[i] = d_A[i] + d_B[i];
	}
}

extern "C" __global__ void add_fp32_kernel(size_t N, float * d_A, float * d_B, float * d_out) {

	size_t i = blockIdx.x * blockDim.x + threadIdx.x;

	if (i < N){
		d_out[i] = d_A[i] + d_B[i];
	}
}

extern "C" __global__ void naive_matmul_fp16_kernel(int M, int K, int N, float alpha, __half *A, __half *B, float beta, __half *C) {

	int x = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;

	if (x < M && y < N){
		float temp = 0;
		for (int i = 0; i < K; i++){
			temp += __half2float(A[x * K + i]) * __half2float(B[i * N + y]);
		}
		C[x * N + y] = __float2half(alpha * temp + beta * __half2float(C[x * N + y]));
	}
}

// REFERENCE / CREDIT...
// FROM: https://github.com/Bruce-Lee-LY/cuda_hgemm/blob/master/src/mma/mma_async_stage4.cu

#define MMA_M 16
#define MMA_N 8
#define MMA_K 16

#define BLOCK_ROWS 256
#define BLOCK_COLS 128

#define WARP_ROWS 64
#define WARP_COLS 64

#define BLOCK_ROW_WARPS 2  // BLOCK_COLS / WARP_COLS
#define BLOCK_COL_WARPS 4  // BLOCK_ROWS / WARP_ROWS

#define BLOCK_ROW_TILES 16  // BLOCK_COLS / MMA_N
#define BLOCK_COL_TILES 16  // BLOCK_ROWS / MMA_M

#define WARP_ROW_TILES 8  // WARP_COLS / MMA_N
#define WARP_COL_TILES 4  // WARP_ROWS / MMA_M

#define WARP_SIZE 32
#define WARPS_PER_BLOCK 8      // BLOCK_ROW_WARPS * BLOCK_COL_WARPS
#define THREADS_PER_BLOCK 256  // WARP_SIZE * WARPS_PER_BLOCK

#define CHUNK_K 2  // 32 / MMA_K

#define THREAD_COPY_BYTES 16

#define CHUNK_LINE_BYTES 64          // CHUNK_K * MMA_K * sizeof(half)
#define CHUNK_COPY_LINES_PER_WARP 8  // WARP_SIZE * THREAD_COPY_BYTES / CHUNK_LINE_BYTES
#define CHUNK_COPY_LINE_LANES 4      // WARP_SIZE / CHUNK_COPY_LINES_PER_WARP

#define AB_SMEM_STRIDE 32  // CHUNK_K * MMA_K

#define C_SMEM_STRIDE 128  // BLOCK_COLS
#define C_SMEM_OFFSET 64   // WARP_COLS

#define BLOCK_STRIDE 16

#define SMEM_BANK_ROWS 2  // 32 * 4 / (AB_SMEM_STRIDE * sizeof(half))

#define PERMUTED_OFFSET 8
#define PERMUTED_COLS 4

#define K_STAGE 4

extern "C" __global__ void matmul_fp16_kernel(const half *__restrict__ A, const half *__restrict__ B, half *__restrict__ C,
                                     size_t M, size_t N, size_t K, float alpha, float beta) {
    const size_t M_tiles = div_ceil(M, MMA_M);
    const size_t N_tiles = div_ceil(N, MMA_N);
    const size_t K_tiles = div_ceil(K, MMA_K);

    const size_t block_tile_i =
        (blockIdx.z % 2) ? ((gridDim.y - blockIdx.y - 1) * BLOCK_COL_TILES) : (blockIdx.y * BLOCK_COL_TILES);
    const size_t block_tile_j = (blockIdx.z * gridDim.x + blockIdx.x) * BLOCK_ROW_TILES;

    if (block_tile_i >= M_tiles || block_tile_j >= N_tiles) {
        return;
    }

    extern __shared__ half smem[][AB_SMEM_STRIDE];

    const size_t warp_id = threadIdx.x / WARP_SIZE;
    const size_t lane_id = threadIdx.x % WARP_SIZE;

    constexpr size_t B_smem_idx_off = BLOCK_ROWS;
    constexpr size_t smem_stage_off = BLOCK_ROWS + BLOCK_COLS;

    half *smem_warp_tile_row_ptr = &smem[0][0] + (warp_id / BLOCK_ROW_WARPS) * C_SMEM_STRIDE * WARP_ROWS;
    const half *smem_warp_stream_ptr = &smem[0][0] + warp_id * MMA_M * 2 * C_SMEM_STRIDE;

    const size_t gmem_idx = (block_tile_i + warp_id * 2) * MMA_M * N + block_tile_j * MMA_N;
    const half *src_gmem_warp_stream_ptr = &C[gmem_idx];

    uint32_t RC[WARP_COL_TILES][WARP_ROW_TILES][2];

#pragma unroll
    for (size_t i = 0; i < WARP_COL_TILES; ++i) {
#pragma unroll
        for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
            RC[i][j][0] = 0;
            RC[i][j][1] = 0;
        }
    }

    const half *A_warp_ptr = &A[block_tile_i * MMA_M * K] + BLOCK_ROWS / WARPS_PER_BLOCK * K * warp_id;
    const half *B_warp_ptr = &B[block_tile_j * MMA_N * K] + BLOCK_COLS / WARPS_PER_BLOCK * K * warp_id;

    constexpr size_t A_smem_iters = BLOCK_ROWS / (CHUNK_COPY_LINES_PER_WARP * WARPS_PER_BLOCK);
    constexpr size_t B_smem_iters = BLOCK_COLS / (CHUNK_COPY_LINES_PER_WARP * WARPS_PER_BLOCK);

    size_t smem_store_idx = 0;
    size_t smem_load_idx = 0;

    size_t smem_store_off = 0;
    size_t smem_load_off = 0;

    size_t A_smem_idx = 0;
    int4 *A_lane_ptr = nullptr;

    size_t B_smem_idx = 0;
    int4 *B_lane_ptr = nullptr;

    A_smem_idx = smem_store_off + BLOCK_ROWS / WARPS_PER_BLOCK * warp_id;
    A_lane_ptr = (int4 *)(A_warp_ptr + (lane_id / CHUNK_COPY_LINE_LANES) * K) + (lane_id % CHUNK_COPY_LINE_LANES);
    A_smem_idx += lane_id / CHUNK_COPY_LINE_LANES;

#pragma unroll
    for (size_t i = 0; i < A_smem_iters; ++i) {
        uint32_t A_smem_lane_addr = __cvta_generic_to_shared(&smem[A_smem_idx][0]) +
                                    ((lane_id % CHUNK_COPY_LINE_LANES +
                                      (A_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                     CHUNK_COPY_LINE_LANES) *
                                        THREAD_COPY_BYTES;

        CP_ASYNC_CG(A_smem_lane_addr, A_lane_ptr, THREAD_COPY_BYTES);

        A_lane_ptr = (int4 *)((half *)A_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
        A_smem_idx += CHUNK_COPY_LINES_PER_WARP;
    }

    B_smem_idx = smem_store_off + B_smem_idx_off + BLOCK_COLS / WARPS_PER_BLOCK * warp_id;
    B_lane_ptr = (int4 *)(B_warp_ptr + (lane_id / CHUNK_COPY_LINE_LANES) * K) + (lane_id % CHUNK_COPY_LINE_LANES);
    B_smem_idx += lane_id / CHUNK_COPY_LINE_LANES;

#pragma unroll
    for (size_t i = 0; i < B_smem_iters; ++i) {
        uint32_t B_smem_lane_addr = __cvta_generic_to_shared(&smem[B_smem_idx][0]) +
                                    ((lane_id % CHUNK_COPY_LINE_LANES +
                                      (B_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                     CHUNK_COPY_LINE_LANES) *
                                        THREAD_COPY_BYTES;

        CP_ASYNC_CG(B_smem_lane_addr, B_lane_ptr, THREAD_COPY_BYTES);

        B_lane_ptr = (int4 *)((half *)B_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
        B_smem_idx += CHUNK_COPY_LINES_PER_WARP;
    }

    CP_ASYNC_COMMIT_GROUP();

    smem_store_idx = (smem_store_idx + 1) % K_STAGE;
    smem_store_off = smem_store_idx * smem_stage_off;

    A_smem_idx = smem_store_off + BLOCK_ROWS / WARPS_PER_BLOCK * warp_id;
    A_lane_ptr = (int4 *)(A_warp_ptr + CHUNK_K * MMA_K + (lane_id / CHUNK_COPY_LINE_LANES) * K) +
                 (lane_id % CHUNK_COPY_LINE_LANES);
    A_smem_idx += lane_id / CHUNK_COPY_LINE_LANES;

#pragma unroll
    for (size_t i = 0; i < A_smem_iters; ++i) {
        uint32_t A_smem_lane_addr = __cvta_generic_to_shared(&smem[A_smem_idx][0]) +
                                    ((lane_id % CHUNK_COPY_LINE_LANES +
                                      (A_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                     CHUNK_COPY_LINE_LANES) *
                                        THREAD_COPY_BYTES;

        CP_ASYNC_CG(A_smem_lane_addr, A_lane_ptr, THREAD_COPY_BYTES);

        A_lane_ptr = (int4 *)((half *)A_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
        A_smem_idx += CHUNK_COPY_LINES_PER_WARP;
    }

    B_smem_idx = smem_store_off + B_smem_idx_off + BLOCK_COLS / WARPS_PER_BLOCK * warp_id;
    B_lane_ptr = (int4 *)(B_warp_ptr + CHUNK_K * MMA_K + (lane_id / CHUNK_COPY_LINE_LANES) * K) +
                 (lane_id % CHUNK_COPY_LINE_LANES);
    B_smem_idx += lane_id / CHUNK_COPY_LINE_LANES;

#pragma unroll
    for (size_t i = 0; i < B_smem_iters; ++i) {
        uint32_t B_smem_lane_addr = __cvta_generic_to_shared(&smem[B_smem_idx][0]) +
                                    ((lane_id % CHUNK_COPY_LINE_LANES +
                                      (B_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                     CHUNK_COPY_LINE_LANES) *
                                        THREAD_COPY_BYTES;

        CP_ASYNC_CG(B_smem_lane_addr, B_lane_ptr, THREAD_COPY_BYTES);

        B_lane_ptr = (int4 *)((half *)B_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
        B_smem_idx += CHUNK_COPY_LINES_PER_WARP;
    }

    CP_ASYNC_COMMIT_GROUP();

    smem_store_idx = (smem_store_idx + 1) % K_STAGE;
    smem_store_off = smem_store_idx * smem_stage_off;

    A_smem_idx = smem_store_off + BLOCK_ROWS / WARPS_PER_BLOCK * warp_id;
    A_lane_ptr = (int4 *)(A_warp_ptr + 2 * CHUNK_K * MMA_K + (lane_id / CHUNK_COPY_LINE_LANES) * K) +
                 (lane_id % CHUNK_COPY_LINE_LANES);
    A_smem_idx += lane_id / CHUNK_COPY_LINE_LANES;

#pragma unroll
    for (size_t i = 0; i < A_smem_iters; ++i) {
        uint32_t A_smem_lane_addr = __cvta_generic_to_shared(&smem[A_smem_idx][0]) +
                                    ((lane_id % CHUNK_COPY_LINE_LANES +
                                      (A_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                     CHUNK_COPY_LINE_LANES) *
                                        THREAD_COPY_BYTES;

        CP_ASYNC_CG(A_smem_lane_addr, A_lane_ptr, THREAD_COPY_BYTES);

        A_lane_ptr = (int4 *)((half *)A_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
        A_smem_idx += CHUNK_COPY_LINES_PER_WARP;
    }

    B_smem_idx = smem_store_off + B_smem_idx_off + BLOCK_COLS / WARPS_PER_BLOCK * warp_id;
    B_lane_ptr = (int4 *)(B_warp_ptr + 2 * CHUNK_K * MMA_K + (lane_id / CHUNK_COPY_LINE_LANES) * K) +
                 (lane_id % CHUNK_COPY_LINE_LANES);
    B_smem_idx += lane_id / CHUNK_COPY_LINE_LANES;

#pragma unroll
    for (size_t i = 0; i < B_smem_iters; ++i) {
        uint32_t B_smem_lane_addr = __cvta_generic_to_shared(&smem[B_smem_idx][0]) +
                                    ((lane_id % CHUNK_COPY_LINE_LANES +
                                      (B_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                     CHUNK_COPY_LINE_LANES) *
                                        THREAD_COPY_BYTES;

        CP_ASYNC_CG(B_smem_lane_addr, B_lane_ptr, THREAD_COPY_BYTES);

        B_lane_ptr = (int4 *)((half *)B_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
        B_smem_idx += CHUNK_COPY_LINES_PER_WARP;
    }

    CP_ASYNC_COMMIT_GROUP();
    CP_ASYNC_WAIT_GROUP(2);

    __syncthreads();

    uint32_t RA[2][WARP_COL_TILES][4];
    uint32_t RB[2][WARP_ROW_TILES][2];

    size_t reg_store_idx = 0;
    size_t reg_load_idx = 1;

#pragma unroll
    for (size_t i = 0; i < WARP_COL_TILES; ++i) {
        size_t A_smem_idx = smem_load_off + (warp_id / BLOCK_ROW_WARPS) * WARP_ROWS + i * MMA_M;
        uint32_t A_smem_lane_addr = __cvta_generic_to_shared(
            &smem[A_smem_idx + lane_id % 16][((lane_id / 16) * 8 + (lane_id % 16 % (PERMUTED_COLS * SMEM_BANK_ROWS)) /
                                                                       SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                                             AB_SMEM_STRIDE]);

        LDMATRIX_X4(RA[reg_store_idx][i][0], RA[reg_store_idx][i][1], RA[reg_store_idx][i][2], RA[reg_store_idx][i][3],
                    A_smem_lane_addr);
    }

#pragma unroll
    for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
        size_t B_smem_idx = smem_load_off + B_smem_idx_off + (warp_id % BLOCK_ROW_WARPS) * WARP_COLS + j * MMA_N;
        uint32_t B_smem_lane_addr =
            __cvta_generic_to_shared(&smem[B_smem_idx + lane_id % 8]
                                          [(((lane_id / 8) % 2) * 8 + (lane_id % 8 % (PERMUTED_COLS * SMEM_BANK_ROWS)) /
                                                                          SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                                           AB_SMEM_STRIDE]);

        LDMATRIX_X2(RB[reg_store_idx][j][0], RB[reg_store_idx][j][1], B_smem_lane_addr);
    }

#pragma unroll
    for (size_t tile_k = CHUNK_K * (K_STAGE - 1); tile_k < K_tiles; tile_k += CHUNK_K) {
        reg_store_idx ^= 1;
        reg_load_idx ^= 1;

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
            size_t A_smem_idx = smem_load_off + (warp_id / BLOCK_ROW_WARPS) * WARP_ROWS + i * MMA_M;
            uint32_t A_smem_lane_addr = __cvta_generic_to_shared(
                &smem[A_smem_idx + lane_id % 16]
                     [(MMA_K + (lane_id / 16) * 8 +
                       (lane_id % 16 % (PERMUTED_COLS * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                      AB_SMEM_STRIDE]);

            LDMATRIX_X4(RA[reg_store_idx][i][0], RA[reg_store_idx][i][1], RA[reg_store_idx][i][2],
                        RA[reg_store_idx][i][3], A_smem_lane_addr);
        }

#pragma unroll
        for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
            size_t B_smem_idx = smem_load_off + B_smem_idx_off + (warp_id % BLOCK_ROW_WARPS) * WARP_COLS + j * MMA_N;
            uint32_t B_smem_lane_addr = __cvta_generic_to_shared(
                &smem[B_smem_idx + lane_id % 8]
                     [(MMA_K + ((lane_id / 8) % 2) * 8 +
                       (lane_id % 8 % (PERMUTED_COLS * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                      AB_SMEM_STRIDE]);

            LDMATRIX_X2(RB[reg_store_idx][j][0], RB[reg_store_idx][j][1], B_smem_lane_addr);
        }

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
#pragma unroll
            for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
                size_t j_s = (i % 2) ? (WARP_ROW_TILES - j - 1) : j;

                HMMA16816(RC[i][j_s][0], RC[i][j_s][1], RA[reg_load_idx][i][0], RA[reg_load_idx][i][1],
                          RA[reg_load_idx][i][2], RA[reg_load_idx][i][3], RB[reg_load_idx][j_s][0],
                          RB[reg_load_idx][j_s][1], RC[i][j_s][0], RC[i][j_s][1]);
            }
        }

        smem_store_idx = (smem_store_idx + 1) % K_STAGE;
        smem_store_off = smem_store_idx * smem_stage_off;

        A_smem_idx = smem_store_off + BLOCK_ROWS / WARPS_PER_BLOCK * warp_id;
        A_lane_ptr = (int4 *)(A_warp_ptr + tile_k * MMA_K + (lane_id / CHUNK_COPY_LINE_LANES) * K) +
                     (lane_id % CHUNK_COPY_LINE_LANES);
        A_smem_idx += lane_id / CHUNK_COPY_LINE_LANES;

#pragma unroll
        for (size_t i = 0; i < A_smem_iters / CHUNK_K; ++i) {
            uint32_t A_smem_lane_addr = __cvta_generic_to_shared(&smem[A_smem_idx][0]) +
                                        ((lane_id % CHUNK_COPY_LINE_LANES +
                                          (A_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                         CHUNK_COPY_LINE_LANES) *
                                            THREAD_COPY_BYTES;

            CP_ASYNC_CG(A_smem_lane_addr, A_lane_ptr, THREAD_COPY_BYTES);

            A_lane_ptr = (int4 *)((half *)A_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
            A_smem_idx += CHUNK_COPY_LINES_PER_WARP;
        }

        B_smem_idx = smem_store_off + B_smem_idx_off + BLOCK_COLS / WARPS_PER_BLOCK * warp_id;
        B_lane_ptr = (int4 *)(B_warp_ptr + tile_k * MMA_K + (lane_id / CHUNK_COPY_LINE_LANES) * K) +
                     (lane_id % CHUNK_COPY_LINE_LANES);
        B_smem_idx += lane_id / CHUNK_COPY_LINE_LANES;

#pragma unroll
        for (size_t i = 0; i < B_smem_iters / CHUNK_K; ++i) {
            uint32_t B_smem_lane_addr = __cvta_generic_to_shared(&smem[B_smem_idx][0]) +
                                        ((lane_id % CHUNK_COPY_LINE_LANES +
                                          (B_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                         CHUNK_COPY_LINE_LANES) *
                                            THREAD_COPY_BYTES;

            CP_ASYNC_CG(B_smem_lane_addr, B_lane_ptr, THREAD_COPY_BYTES);

            B_lane_ptr = (int4 *)((half *)B_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
            B_smem_idx += CHUNK_COPY_LINES_PER_WARP;
        }

        smem_load_idx = (smem_load_idx + 1) % K_STAGE;
        smem_load_off = smem_load_idx * smem_stage_off;

#pragma unroll
        for (size_t i = (CHUNK_K - 1) * A_smem_iters / CHUNK_K; i < A_smem_iters; ++i) {
            uint32_t A_smem_lane_addr = __cvta_generic_to_shared(&smem[A_smem_idx][0]) +
                                        ((lane_id % CHUNK_COPY_LINE_LANES +
                                          (A_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                         CHUNK_COPY_LINE_LANES) *
                                            THREAD_COPY_BYTES;

            CP_ASYNC_CG(A_smem_lane_addr, A_lane_ptr, THREAD_COPY_BYTES);

            A_lane_ptr = (int4 *)((half *)A_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
            A_smem_idx += CHUNK_COPY_LINES_PER_WARP;
        }

#pragma unroll
        for (size_t i = (CHUNK_K - 1) * B_smem_iters / CHUNK_K; i < B_smem_iters; ++i) {
            uint32_t B_smem_lane_addr = __cvta_generic_to_shared(&smem[B_smem_idx][0]) +
                                        ((lane_id % CHUNK_COPY_LINE_LANES +
                                          (B_smem_idx % (CHUNK_COPY_LINE_LANES * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS) %
                                         CHUNK_COPY_LINE_LANES) *
                                            THREAD_COPY_BYTES;

            CP_ASYNC_CG(B_smem_lane_addr, B_lane_ptr, THREAD_COPY_BYTES);

            B_lane_ptr = (int4 *)((half *)B_lane_ptr + CHUNK_COPY_LINES_PER_WARP * K);
            B_smem_idx += CHUNK_COPY_LINES_PER_WARP;
        }

        CP_ASYNC_COMMIT_GROUP();
        CP_ASYNC_WAIT_GROUP(2);

        __syncthreads();

        reg_store_idx ^= 1;
        reg_load_idx ^= 1;

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
            size_t A_smem_idx = smem_load_off + (warp_id / BLOCK_ROW_WARPS) * WARP_ROWS + i * MMA_M;
            uint32_t A_smem_lane_addr =
                __cvta_generic_to_shared(&smem[A_smem_idx + lane_id % 16]
                                              [((lane_id / 16) * 8 + (lane_id % 16 % (PERMUTED_COLS * SMEM_BANK_ROWS)) /
                                                                         SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                                               AB_SMEM_STRIDE]);

            LDMATRIX_X4(RA[reg_store_idx][i][0], RA[reg_store_idx][i][1], RA[reg_store_idx][i][2],
                        RA[reg_store_idx][i][3], A_smem_lane_addr);
        }

#pragma unroll
        for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
            size_t B_smem_idx = smem_load_off + B_smem_idx_off + (warp_id % BLOCK_ROW_WARPS) * WARP_COLS + j * MMA_N;
            uint32_t B_smem_lane_addr = __cvta_generic_to_shared(
                &smem[B_smem_idx + lane_id % 8]
                     [(((lane_id / 8) % 2) * 8 +
                       (lane_id % 8 % (PERMUTED_COLS * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                      AB_SMEM_STRIDE]);

            LDMATRIX_X2(RB[reg_store_idx][j][0], RB[reg_store_idx][j][1], B_smem_lane_addr);
        }

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
#pragma unroll
            for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
                size_t j_s = (i % 2) ? (WARP_ROW_TILES - j - 1) : j;

                HMMA16816(RC[i][j_s][0], RC[i][j_s][1], RA[reg_load_idx][i][0], RA[reg_load_idx][i][1],
                          RA[reg_load_idx][i][2], RA[reg_load_idx][i][3], RB[reg_load_idx][j_s][0],
                          RB[reg_load_idx][j_s][1], RC[i][j_s][0], RC[i][j_s][1]);
            }
        }
    }

#pragma unroll
    for (size_t k_step = 0; k_step < CHUNK_K; ++k_step) {
        reg_store_idx ^= 1;
        reg_load_idx ^= 1;

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
            size_t A_smem_idx = smem_load_off + (warp_id / BLOCK_ROW_WARPS) * WARP_ROWS + i * MMA_M;
            uint32_t A_smem_lane_addr = __cvta_generic_to_shared(
                &smem[A_smem_idx + lane_id % 16]
                     [(((k_step + 1) % CHUNK_K) * MMA_K + (lane_id / 16) * 8 +
                       (lane_id % 16 % (PERMUTED_COLS * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                      AB_SMEM_STRIDE]);

            LDMATRIX_X4(RA[reg_store_idx][i][0], RA[reg_store_idx][i][1], RA[reg_store_idx][i][2],
                        RA[reg_store_idx][i][3], A_smem_lane_addr);
        }

#pragma unroll
        for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
            size_t B_smem_idx = smem_load_off + B_smem_idx_off + (warp_id % BLOCK_ROW_WARPS) * WARP_COLS + j * MMA_N;
            uint32_t B_smem_lane_addr = __cvta_generic_to_shared(
                &smem[B_smem_idx + lane_id % 8]
                     [(((k_step + 1) % CHUNK_K) * MMA_K + ((lane_id / 8) % 2) * 8 +
                       (lane_id % 8 % (PERMUTED_COLS * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                      AB_SMEM_STRIDE]);

            LDMATRIX_X2(RB[reg_store_idx][j][0], RB[reg_store_idx][j][1], B_smem_lane_addr);
        }

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
#pragma unroll
            for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
                size_t j_s = (i % 2) ? (WARP_ROW_TILES - j - 1) : j;

                HMMA16816(RC[i][j_s][0], RC[i][j_s][1], RA[reg_load_idx][i][0], RA[reg_load_idx][i][1],
                          RA[reg_load_idx][i][2], RA[reg_load_idx][i][3], RB[reg_load_idx][j_s][0],
                          RB[reg_load_idx][j_s][1], RC[i][j_s][0], RC[i][j_s][1]);
            }
        }

        if (k_step + 2 == CHUNK_K) {
            smem_load_idx = (smem_load_idx + 1) % K_STAGE;
            smem_load_off = smem_load_idx * smem_stage_off;

            CP_ASYNC_WAIT_GROUP(1);

            __syncthreads();
        }
    }

#pragma unroll
    for (size_t k_step = 0; k_step < CHUNK_K; ++k_step) {
        reg_store_idx ^= 1;
        reg_load_idx ^= 1;

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
            size_t A_smem_idx = smem_load_off + (warp_id / BLOCK_ROW_WARPS) * WARP_ROWS + i * MMA_M;
            uint32_t A_smem_lane_addr = __cvta_generic_to_shared(
                &smem[A_smem_idx + lane_id % 16]
                     [(((k_step + 1) % CHUNK_K) * MMA_K + (lane_id / 16) * 8 +
                       (lane_id % 16 % (PERMUTED_COLS * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                      AB_SMEM_STRIDE]);

            LDMATRIX_X4(RA[reg_store_idx][i][0], RA[reg_store_idx][i][1], RA[reg_store_idx][i][2],
                        RA[reg_store_idx][i][3], A_smem_lane_addr);
        }

#pragma unroll
        for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
            size_t B_smem_idx = smem_load_off + B_smem_idx_off + (warp_id % BLOCK_ROW_WARPS) * WARP_COLS + j * MMA_N;
            uint32_t B_smem_lane_addr = __cvta_generic_to_shared(
                &smem[B_smem_idx + lane_id % 8]
                     [(((k_step + 1) % CHUNK_K) * MMA_K + ((lane_id / 8) % 2) * 8 +
                       (lane_id % 8 % (PERMUTED_COLS * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                      AB_SMEM_STRIDE]);

            LDMATRIX_X2(RB[reg_store_idx][j][0], RB[reg_store_idx][j][1], B_smem_lane_addr);
        }

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
#pragma unroll
            for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
                size_t j_s = (i % 2) ? (WARP_ROW_TILES - j - 1) : j;

                HMMA16816(RC[i][j_s][0], RC[i][j_s][1], RA[reg_load_idx][i][0], RA[reg_load_idx][i][1],
                          RA[reg_load_idx][i][2], RA[reg_load_idx][i][3], RB[reg_load_idx][j_s][0],
                          RB[reg_load_idx][j_s][1], RC[i][j_s][0], RC[i][j_s][1]);
            }
        }

        if (k_step + 2 == CHUNK_K) {
            smem_load_idx = (smem_load_idx + 1) % K_STAGE;
            smem_load_off = smem_load_idx * smem_stage_off;

            CP_ASYNC_WAIT_GROUP(0);

            __syncthreads();
        }
    }

#pragma unroll
    for (size_t k_step = 1; k_step < CHUNK_K; ++k_step) {
        reg_store_idx ^= 1;
        reg_load_idx ^= 1;

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
            size_t A_smem_idx = smem_load_off + (warp_id / BLOCK_ROW_WARPS) * WARP_ROWS + i * MMA_M;
            uint32_t A_smem_lane_addr = __cvta_generic_to_shared(
                &smem[A_smem_idx + lane_id % 16]
                     [(k_step * MMA_K + (lane_id / 16) * 8 +
                       (lane_id % 16 % (PERMUTED_COLS * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                      AB_SMEM_STRIDE]);

            LDMATRIX_X4(RA[reg_store_idx][i][0], RA[reg_store_idx][i][1], RA[reg_store_idx][i][2],
                        RA[reg_store_idx][i][3], A_smem_lane_addr);
        }

#pragma unroll
        for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
            size_t B_smem_idx = smem_load_off + B_smem_idx_off + (warp_id % BLOCK_ROW_WARPS) * WARP_COLS + j * MMA_N;
            uint32_t B_smem_lane_addr = __cvta_generic_to_shared(
                &smem[B_smem_idx + lane_id % 8]
                     [(k_step * MMA_K + ((lane_id / 8) % 2) * 8 +
                       (lane_id % 8 % (PERMUTED_COLS * SMEM_BANK_ROWS)) / SMEM_BANK_ROWS * PERMUTED_OFFSET) %
                      AB_SMEM_STRIDE]);

            LDMATRIX_X2(RB[reg_store_idx][j][0], RB[reg_store_idx][j][1], B_smem_lane_addr);
        }

#pragma unroll
        for (size_t i = 0; i < WARP_COL_TILES; ++i) {
#pragma unroll
            for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
                size_t j_s = (i % 2) ? (WARP_ROW_TILES - j - 1) : j;

                HMMA16816(RC[i][j_s][0], RC[i][j_s][1], RA[reg_load_idx][i][0], RA[reg_load_idx][i][1],
                          RA[reg_load_idx][i][2], RA[reg_load_idx][i][3], RB[reg_load_idx][j_s][0],
                          RB[reg_load_idx][j_s][1], RC[i][j_s][0], RC[i][j_s][1]);
            }
        }
    }

#pragma unroll
    for (size_t i = 0; i < WARP_COL_TILES; ++i) {
#pragma unroll
        for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
            size_t j_s = (i % 2) ? (WARP_ROW_TILES - j - 1) : j;

            HMMA16816(RC[i][j_s][0], RC[i][j_s][1], RA[reg_store_idx][i][0], RA[reg_store_idx][i][1],
                      RA[reg_store_idx][i][2], RA[reg_store_idx][i][3], RB[reg_store_idx][j_s][0],
                      RB[reg_store_idx][j_s][1], RC[i][j_s][0], RC[i][j_s][1]);
        }
    }

    __syncthreads();

#pragma unroll
    for (size_t i = 0; i < WARP_COL_TILES; ++i) {
#pragma unroll
        for (size_t j = 0; j < WARP_ROW_TILES; ++j) {
            half *lane_ptr0 =
                smem_warp_tile_row_ptr + (i * MMA_M + lane_id / 4) * C_SMEM_STRIDE +
                ((warp_id % BLOCK_ROW_WARPS) * C_SMEM_OFFSET + j * MMA_N +
                 (lane_id % 4) * sizeof(uint32_t) / sizeof(half) + ((lane_id / 4) % 8) * PERMUTED_OFFSET) %
                    C_SMEM_STRIDE;
            half *lane_ptr1 =
                smem_warp_tile_row_ptr + (i * MMA_M + lane_id / 4 + 8) * C_SMEM_STRIDE +
                ((warp_id % BLOCK_ROW_WARPS) * C_SMEM_OFFSET + j * MMA_N +
                 (lane_id % 4) * sizeof(uint32_t) / sizeof(half) + ((lane_id / 4 + 8) % 8) * PERMUTED_OFFSET) %
                    C_SMEM_STRIDE;

            *((__half2 *)(lane_ptr0)) = __hmul2(__float2half2_rn(alpha), *((__half2 *) &RC[i][j][0]));
            *((__half2 *)(lane_ptr1)) = __hmul2(__float2half2_rn(alpha), *((__half2 *) &RC[i][j][1]));
            //*((uint32_t *)(lane_ptr0)) = RC[i][j][0];
            //*((uint32_t *)(lane_ptr1)) = RC[i][j][1];
        }
    }

    __syncthreads();

#pragma unroll
    for (size_t i = 0; i < MMA_M; ++i) {
        /*
        *((int4 *)(src_gmem_warp_stream_ptr + (i * 2 + lane_id / 16) * N) + lane_id % 16) =
            *((int4 *)(smem_warp_stream_ptr + (i * 2 + lane_id / 16) * C_SMEM_STRIDE) +
              (lane_id % 16 + (i * 2 + lane_id / 16) % 8) % (C_SMEM_STRIDE * sizeof(half) / THREAD_COPY_BYTES));
    	*/
        
        if ((i * 2 + lane_id / 16) < M){
        	*((__half2 *)((int4 *)(src_gmem_warp_stream_ptr + (i * 2 + lane_id / 16) * N) + lane_id % 16)) = __hmul2(__float2half2_rn(beta), *((__half2 *)((int4 *)(src_gmem_warp_stream_ptr + (i * 2 + lane_id / 16) * N) + lane_id % 16))) + *((__half2 *)((int4 *)(smem_warp_stream_ptr + (i * 2 + lane_id / 16) * C_SMEM_STRIDE) + (lane_id % 16 + (i * 2 + lane_id / 16) % 8) % (C_SMEM_STRIDE * sizeof(half) / THREAD_COPY_BYTES)));
        	*((__half2 *)((int4 *)(src_gmem_warp_stream_ptr + (i * 2 + lane_id / 16) * N) + lane_id % 16) + 1) = __hmul2(__float2half2_rn(beta), *((__half2 *)((int4 *)(src_gmem_warp_stream_ptr + (i * 2 + lane_id / 16) * N) + lane_id % 16) + 1)) + *((__half2 *)((int4 *)(smem_warp_stream_ptr + (i * 2 + lane_id / 16) * C_SMEM_STRIDE) + (lane_id % 16 + (i * 2 + lane_id / 16) % 8) % (C_SMEM_STRIDE * sizeof(half) / THREAD_COPY_BYTES)) + 1);
        	*((__half2 *)((int4 *)(src_gmem_warp_stream_ptr + (i * 2 + lane_id / 16) * N) + lane_id % 16) + 2) = __hmul2(__float2half2_rn(beta), *((__half2 *)((int4 *)(src_gmem_warp_stream_ptr + (i * 2 + lane_id / 16) * N) + lane_id % 16) + 2)) + *((__half2 *)((int4 *)(smem_warp_stream_ptr + (i * 2 + lane_id / 16) * C_SMEM_STRIDE) + (lane_id % 16 + (i * 2 + lane_id / 16) % 8) % (C_SMEM_STRIDE * sizeof(half) / THREAD_COPY_BYTES)) + 2);
        	*((__half2 *)((int4 *)(src_gmem_warp_stream_ptr + (i * 2 + lane_id / 16) * N) + lane_id % 16) + 3) = __hmul2(__float2half2_rn(beta), *((__half2 *)((int4 *)(src_gmem_warp_stream_ptr + (i * 2 + lane_id / 16) * N) + lane_id % 16) + 3)) + *((__half2 *)((int4 *)(smem_warp_stream_ptr + (i * 2 + lane_id / 16) * C_SMEM_STRIDE) + (lane_id % 16 + (i * 2 + lane_id / 16) % 8) % (C_SMEM_STRIDE * sizeof(half) / THREAD_COPY_BYTES)) + 3);
    	}
    }
}



// Out-of-place Transpose!

// Tile dim = 32
// Block rows = 8

// Launch config:

// gridDim.x = ceil(n_orig_rows / 32)
// gridDim.y = ceil(n_orig_cols / 32)

// blockDim.x = 8 * 32
extern "C" __global__ void transpose_fp16_kernel(int n_orig_rows, int n_orig_cols, const __half * __restrict__ in, __half * __restrict__ out) {
	
	// +1 to avoid bank conflict
	__shared__ __half tile[32][32 + 1];
	
	// every thread block will do 32x32 square
	// but within thread block each thread will do 
	// 4 items

	// block_size = 32
	int block_row_start = blockIdx.x * 32;
	int block_col_start = blockIdx.y * 32;

	int thread_id = threadIdx.x;

	int warp_id = thread_id / 32;
	int lane_id = thread_id % 32;

	uint64_t total_size = n_orig_rows * n_orig_cols;

	// if this thread's colu

	// Read from original

	// each thread writes to elements from 4 rows of original
	// into tile and stores them

	// contiguous threads (thread_id's in block) will read contiguous data in this way
	// for good coalescing

	

	// each iteration warp loads a row

	uint64_t cur_ind;

	#pragma unroll
	for (int j = 0; j < 32; j += 8){
		cur_ind = (block_row_start + (warp_id+j))*n_orig_cols + (block_col_start + lane_id);
		if (cur_ind < total_size){
			tile[warp_id+j][lane_id] = in[cur_ind];
		}
	}

	__syncthreads();
	
	// now load the item in tile into transposed memory location
	#pragma unroll

	// each iteration warp stores a column of original, now a row in transposed
	for (int j = 0; j < 32; j += 8){
		cur_ind = (block_col_start +(warp_id+j))*n_orig_rows + (block_row_start + lane_id);
		if (cur_ind < total_size){
			out[cur_ind] = tile[lane_id][warp_id + j];
		}
	}
}

// num_stages is defined by amount of smem avail, so needs to be passed in as arg
extern "C" __global__ void rms_norm_fp16_kernel(float eps, int n_rows, int n_cols, __half * rms_weight, __half * X, __half * out, float * sq_sums) {

	// this gets dynamically allocated the size of model_dim
	extern __shared__ uint8_t sdata[];

	__half * row = (__half *) sdata;
	__half * weights = row + n_cols;

	// every warp will have a reduced value
	__shared__ float reduction_data[32];

	int row_base = blockIdx.x;

	if (row_base >= n_rows){
		return;
	}

	int rows_per_block = n_rows / gridDim.x;
	
	int rows_remain = n_rows % gridDim.x;
	int row_offset;
	if (blockIdx.x < rows_remain){
		// this block will need to do an extra row
		rows_per_block += 1;
		// all prior blocks also had an extra row
		row_offset = row_base * rows_per_block;
	}
	else{
		row_offset = row_base * rows_per_block + rows_remain;
	}

	int thread_id = threadIdx.x;

	int warp_id = thread_id / 32;
	int lane_id = thread_id % 32;

	// Load weights which are shared between all rows (when doing output in item 3...)
	for (uint64_t i = thread_id; i < n_cols; i+=blockDim.x){
		weights[i] = rms_weight[i];
	}
	__syncthreads();

	__half cur_row_val;
	float float_val;
	float running_sum;
	uint64_t row_ind_start;

	// can assume model dim is a multiple of 32...
	unsigned warp_mask = 0xFFFFFFFFU;

	for (int row_id = row_offset; row_id < row_offset + rows_per_block; row_id++){
		row_ind_start = (uint64_t) (row_id) * (uint64_t) n_cols;

		running_sum = 0;

		// 1.) do a per thread loading an initial reduction on max_smem
		for (int i = thread_id; i < n_cols; i+=blockDim.x){
			cur_row_val = X[row_ind_start + i];
			// save for re-scaling
			row[i] = cur_row_val;
			float_val = __half2float(cur_row_val);
			float_val = float_val * float_val;
			running_sum += float_val;
			
		}

		// add this warp's result and place in smem
		for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
			running_sum += __shfl_down_sync(warp_mask, running_sum, warp_offset);
		}

		if (lane_id == 0){
			reduction_data[warp_id] = running_sum;
		}

		__syncthreads();


		// 2.) now combine all the reductions from each thread
		
		if (warp_id == 0){

			running_sum = reduction_data[lane_id];

			for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
				running_sum += __shfl_down_sync(warp_mask, running_sum, warp_offset);
			}

			if (lane_id == 0){
				reduction_data[0] = running_sum;

				// Save down the squared sums of this row
				// so we can easilly compute the backpass...

				// During inference this should be null and not needed
				if (sq_sums){
					sq_sums[row_id] = running_sum;
				}
			}

		}

		__syncthreads();

		
		// now reduction_data[0] has float32 representing total squared sum
		float recip_avg = rsqrtf((reduction_data[0] / (float) n_cols) + eps);

		// 3.) now need to store back all of the row values and mutliply with rms_weight
		__half rms_val;

		for (int i = thread_id; i < n_cols; i+=blockDim.x){
			// copying casting locations as in llama3
			rms_val =  __float2half(__half2float(row[i]) * recip_avg);

			out[row_ind_start + i] = rms_val * weights[i];
		}

		// ensure all threads are complete before we start overwriting row in smem
		__syncthreads();
	}
}

// Because X_inp is in row-major order we should be clever about doing column-wise dot products...

// at the end will do atomicAdds to dW because other blocks will have partial dot products as well

// cannot launch with more threads and n_cols otherwise will be bugs
// # blocks launched is a performance optimization and might be better with less due to less atomicAdds...
// definitely shouldn't launch with more than n_rows
extern "C" __global__ void rms_norm_bwd_weight_fp16_kernel(float eps, int n_rows, int n_cols, __half * X_inp, float * sq_sums, __half * upstream_dX, __half * dW){
	
	// this gets dynamically allocated the size of model_dim
	extern __shared__ uint8_t sdata[];



	// length should be equal to number of rows
	// load in squared sums and then divide by n_cols and take sqrt
	float * recip_avgs = (float *) sdata;

	// working space when computing weight derivs...
	// the dot products will be updated here and when complete
	// will accumulate in dW

	// length equal to the number of columns
	float * weight_derivs = (float *) (recip_avgs + n_rows); 

	int row_base = blockIdx.x;

	if (row_base >= n_rows){
		return;
	}

	int rows_per_block = n_rows / gridDim.x;
	
	int rows_remain = n_rows % gridDim.x;
	int row_offset;
	if (blockIdx.x < rows_remain){
		// this block will need to do an extra row
		rows_per_block += 1;
		// all prior blocks also had an extra row
		row_offset = row_base * rows_per_block;
	}
	else{
		row_offset = row_base * rows_per_block + rows_remain;
	}

	
	int thread_id = threadIdx.x;

	int warp_id = thread_id / 32;
	int lane_id = thread_id % 32;

	

	// retrieve back the recip squared avgs
	for (uint64_t i = thread_id; i < n_rows; i+=blockDim.x){
		recip_avgs[i] = rsqrtf((sq_sums[i] / (float) n_cols) + eps);
	}

	for (uint64_t i = thread_id; i < n_cols; i+=blockDim.x){
		weight_derivs[i] = 0;
	}

	__syncthreads();

	
	// ensure that # threads launched is less than n_cols
	int num_warps = blockDim.x / 32;
	int dims_per_warp = ceilf((float) n_cols / (float) num_warps);

	int warp_iter;
	int cur_dim_offset;

	float cur_recip_avg;

	for (int cur_row = row_offset; cur_row < row_offset + rows_per_block; cur_row++){

		cur_recip_avg = recip_avgs[cur_row];

		// each warp within threadblock will have a different dim_offset
		// and only be respno
		warp_iter = 0;
		cur_dim_offset = dims_per_warp * warp_id + lane_id;
		while ((warp_iter * 32) < (dims_per_warp) && (cur_dim_offset < n_cols)){

			// portion of dot product to update weight at cur_dim_offset
			// because each warp has their own section of dims some can run ahead
			// vs. others and ensure that the shared memory weigth_derivs (portions of column-wise dot product)
			// are still OK...

			// apply chain rule by multiplying with the upstream value...
			weight_derivs[cur_dim_offset] += __half2float(upstream_dX[cur_row * n_cols + cur_dim_offset]) * __half2float(X_inp[cur_row * n_cols + cur_dim_offset]) * cur_recip_avg;
			cur_dim_offset += 32;
			warp_iter++;
		}
	}

	// ensure all warps finish their portion of block
	__syncthreads();

	// now need to do atomic add into the global dW for this section of rows
	for (uint64_t dim = thread_id; dim < n_cols; dim+=blockDim.x){
		atomicAdd(dW + dim, __float2half(weight_derivs[dim]));
	}
}

// Here dX is (N, model_dim) and contains the backprop loss flow that we will update in-place
// This needs to be called after the bwd_weight because the weight we use the updstream dL/dX and this function will
// modify the same pointer...
extern "C" __global__ void rms_norm_bwd_inp_fp16_kernel(float eps, int n_rows, int n_cols, __half * rms_weight, __half * X_inp, float * sq_sums, __half * upstream_dX, __half * dX){
		
	// this gets dynamically allocated the size of model_dim
	extern __shared__ uint8_t sdata[];



	// length should be equal to number of rows
	// load in squared sums and then divide by n_cols and take sqrt
	float * weights_scaled = (float *) sdata;

	// working space when computing weight derivs...
	// the dot products will be updated here and when complete
	// will accumulate in dW

	// length equal to the number of columns
	float * shared_sq_sums = (float *) (weights_scaled + n_cols); 

	int row_base = blockIdx.x;

	if (row_base >= n_rows){
		return;
	}

	int rows_per_block = n_rows / gridDim.x;
	
	int rows_remain = n_rows % gridDim.x;
	int row_offset;
	if (blockIdx.x < rows_remain){
		// this block will need to do an extra row
		rows_per_block += 1;
		// all prior blocks also had an extra row
		row_offset = row_base * rows_per_block;
	}
	else{
		row_offset = row_base * rows_per_block + rows_remain;
	}

	
	int thread_id = threadIdx.x;

	float dim_scale = rsqrt((float) n_cols); 

	for (uint64_t i = thread_id; i < n_cols; i+=blockDim.x){
		weights_scaled[i] = dim_scale * __half2float(rms_weight[i]);
	}

	// retrieve back the recip squared avgs
	for (uint64_t i = thread_id; i < n_rows; i+=blockDim.x){
		shared_sq_sums[i] = sq_sums[i];
	}

	__syncthreads();

	float deriv;
	float cur_sq_sum;
	float cur_sq_sum_rsqrt;

	float inp_val;

	uint64_t row_ind_start;
	for (int row_id = row_offset; row_id < row_offset + rows_per_block; row_id++){
		row_ind_start = (uint64_t) (row_id) * (uint64_t) n_cols;

		cur_sq_sum = shared_sq_sums[row_id];
		cur_sq_sum_rsqrt = rsqrtf(cur_sq_sum);
		
		for (int i = thread_id; i < n_cols; i+=blockDim.x){
			inp_val = __half2float(X_inp[row_ind_start + i]);
			deriv = (weights_scaled[i] * (cur_sq_sum - (inp_val * inp_val)) * cur_sq_sum_rsqrt) / cur_sq_sum;

			// now update dX
			dX[row_id * n_cols + i] = upstream_dX[row_id * n_cols + i] * __float2half(deriv);

		}
	}
}




// THIS COULD REALLY BE PART OF ATTN KERNEL...
extern "C" __global__ void rope_fp16_kernel(int theta, uint64_t N, int model_dim, int head_dim, int num_kv_heads, int * seq_positions, __half * X_q, __half * X_k) {

	// launched with half the number of threads as output positions because each thread updates two spots
	uint64_t i = 2 * (blockIdx.x * blockDim.x + threadIdx.x);

	// N = total_tokens * model_dim
	if (i < N){



		// ASSUMING model_dim > kv_dim

		// If performance is issue with this divides and modulus
		// we could either use bit tricks or hard-code constants...
		int token_row = i / model_dim;
		int cur_pos = seq_positions[token_row];
		int cur_dim = i % head_dim;

		// probably faster (& simpler) to just use arithmetic functions and recompute
		// instead of loading in from global device memory
		float angle = powf(theta, -1 * ((float) cur_dim / (float) head_dim));
		float cos_val = cosf((float) cur_pos * angle);
		float sin_val = sinf((float) cur_pos * angle);

		float x_even, x_odd; 

		// first do X_q
		x_even = __half2float(X_q[i]);
		x_odd = __half2float(X_q[i + 1]);
		X_q[i] = __float2half(cos_val * x_even - sin_val * x_odd);
		X_q[i + 1] = __float2half(cos_val * x_odd + sin_val * x_even);

		// Now reassign this thread to update x_k
		int kv_dim = num_kv_heads * head_dim;
		token_row = i / (kv_dim);
		int total_tokens = N / model_dim;
		__half i_val;
		__half i_next_val;
		if (token_row < total_tokens){
			cur_pos = seq_positions[token_row];
			cur_dim = i % head_dim;

			angle = powf(theta, -1 * ((float) cur_dim / (float) head_dim));
			cos_val = cosf((float) cur_pos * angle);
			sin_val = sinf((float) cur_pos * angle);

			// now do X_k in same manner but obtaining different x vals
			x_even = __half2float(X_k[i]);
			x_odd = __half2float(X_k[i + 1]);
			i_val = __float2half(cos_val * x_even - sin_val * x_odd);
			i_next_val = __float2half(cos_val * x_odd + sin_val * x_even);

			X_k[i] = i_val;
			X_k[i + 1] = i_next_val;

			// Optimization: Could store in kv cache when already in register instead of 
			// reloading again within kv cache kernel.

			// But the cost of these kernels is minimal compared to the matmuls and attention,
			// so not that big a deal and cleaner to seperate.
		}
	}
}

// N = total_tokens * kv_dim
extern "C" __global__ void copy_kv_to_seq_context_fp16_kernel(uint64_t N, int total_tokens, int kv_dim, __half * keys, __half * values, int * seq_positions, uint64_t * seq_context_ptrs, int * seq_context_sizes){

	uint64_t i = (blockIdx.x * blockDim.x + threadIdx.x);

	if (i < N){

		uint64_t token_ind = i / kv_dim;

		__half * seq_context = (__half *) seq_context_ptrs[token_ind];

		uint64_t seq_pos = seq_positions[token_ind];

		uint64_t cur_dim = i % kv_dim;
		
		seq_context[seq_pos * kv_dim + cur_dim] = keys[token_ind * kv_dim + cur_dim];
		seq_context[(seq_context_sizes[token_ind] * kv_dim) + seq_pos * kv_dim + cur_dim] = values[token_ind * kv_dim + cur_dim];
	}
}


// launching number of blocks determined by seq batch packing
// need to ensure shared memory of at least:
//	- (warp reduction buffers): 32 * 4 
//	- (running output): tokens_in_block * (n_heads/n_kv_heads) * head_dim * 4 
//	- (comp seq pos key+value): head_dim * 4
// 	- (running maxs, half and sums, floats): tokens_in_block * (n_heads/n_kv_heads) * 6


// could consider storing temp output only in halfs to reduce memory by half, but gives up precision (especially for large seqs...)

// aggregate total: 128 + 4 * head_dim + 4 * head_dim * tokens_in_block * (n_heads/n_kv_heads) + 6 * tokens_in_block * (n_heads/n_kv_heads)
//				  : 128 + 4 * head_dim + tokens_in_block * (4 * head_dim * (n_heads/n_kv_heads) + 6 * (n_heads/n_kv_heads))

// max_tokens_in_block => floor((SMEM_SIZE - (128 + 4 * head_dim)) / (4 * head_dim * (n_heads/n_kv_heads) + 6 * (n_heads/n_kv_heads)))

// llama3.1 8B: head_dim = 128 and n_heads/n_kv_heads = 4 => total of: 128 + 512 + 2072 * tokens_in_block bytes
// llama3.1 70B: head_dim = 128 and n_heads/n_kv_heads = 8 => total_of : 128 + 1024 + 8240 * tokens_in_block bytes

// 99KB of shared memory should be available meaning 

// can do this at initialization time
// for Compute Capability 8.6 => need to do: cudaFuncSetAttribute(cuFunction attn_fp16, cudaFuncAttributeMaxDynamicSharedMemorySize, 102400);

// on CC 8.6 for

// q_group_dim = n_heads / n_kv_heads


// for convenience just packing this info into a signle uint64_t array per block
#define BLOCK_CONFIG_MASK_TOKEN_START 0xFFFFFFFF00000000
#define BLOCK_CONFIG_MASK_NUM_TOKENS 0x00000000FFFFFFFF


#define SEQ_PHASE_SIZE 32
#define Q_HEAD_PHASE_SIZE 8

extern "C" __global__ void attention_fp16_kernel(int model_dim, int q_group_dim, int kv_dim, int head_dim, uint64_t * block_configs, int * seq_positions, __half * queries, uint64_t * seq_context_ptrs, int * seq_context_sizes, __half * out) {


	int block_id = blockIdx.y;
	// assume we decide these smartly based on shared memory availability / new tokens per sequenence / overall seq len
	// at least 1 block per sequence in batch

	// this is referring to token index within overall batch

	uint64_t block_info = block_configs[block_id];

	int token_ind_start = (block_info & BLOCK_CONFIG_MASK_TOKEN_START) >> 32;
	int num_tokens_in_block = (block_info & BLOCK_CONFIG_MASK_NUM_TOKENS);
	int block_max_seq_pos = seq_positions[token_ind_start] + num_tokens_in_block;

	// each kernel operates only on a specific kv head
	int kv_head = blockIdx.x;
	int num_threads = blockDim.x;
	int num_warps = num_threads / 32;

	int thread_id = threadIdx.x;
	int warp_id = thread_id / 32;
	int lane_id = thread_id % 32;
	


	/*
	if ((kv_head != 0) || (warp_id != 0) || (block_id != 0)){
		return;
	}
	*/
	

	int num_rows_padded = ROUND_UP_TO_MULTIPLE(num_tokens_in_block * q_group_dim, Q_HEAD_PHASE_SIZE);

	// for every warp in the block will store
	// a temporary reduction buffer. 
	// because max 32 warps per thread block
	// and we can do sync_shfl operations per warp
	// then we can make this by default 32 elements
	// where each warp has a slot


	extern __shared__ uint8_t sdata[];
	__half * block_q_tiles = (__half *) (sdata);
	// keep as float for precision until the end

	__half * block_out_tiles = (__half *) (block_q_tiles + (num_rows_padded * head_dim));


	// in reality this only needs to be one array, but keeping both here for convenience / don't consume much memory
	__half * seq_phase_k_tiles = (__half *) (block_out_tiles + (num_rows_padded * head_dim));
	__half * seq_phase_v_tiles = (__half *) (seq_phase_k_tiles + (head_dim * SEQ_PHASE_SIZE));

	// these will store the current maxes for each row as we progress through
	// sequence iteratively. will be of size q_group_dim
	__half * block_maxs = (__half *) (seq_phase_v_tiles + (head_dim * SEQ_PHASE_SIZE));
	// these will store the current sum for online softmax for each row as we progress
	// through sequence iteratively
	// will keep this as floating point for precision purposes


	// need_num_tokens_in_block * q_group_dim to be a multiple of Q_HEAD_PHASE_SIZE
	float * block_sums = (float *) (block_maxs + num_rows_padded);

	// guaranteed that each thread block will be within one sequence
	__half * seq_keys = (__half *) seq_context_ptrs[token_ind_start];
	// keys and values are stored back to back (but there may be empty
	// space before realloc. we need to now the current size (which is really half of entire cache size
	// in order to offset into values))
	__half * seq_values = seq_keys + kv_dim * seq_context_sizes[token_ind_start];




	const int4 zero_int4 = {0, 0, 0, 0};

	// number of tiles in a grid "row" (which is actually for 8 rows)
	int n_head_tiles = (head_dim >> 3);


	// 1.) init block maxes and block sums

	// num_tokens_in_block * q_group_dim
	for (int cur_ind = thread_id; cur_ind < num_rows_padded; cur_ind+=num_threads){
		// minimum value for fp16
		if (cur_ind < num_tokens_in_block * q_group_dim){
			block_maxs[cur_ind] = NEG_INF_DEV_FP16;
			block_sums[cur_ind] = 0;
		}
		else if (cur_ind < num_rows_padded){
			block_maxs[cur_ind] = CONST_ONE_DEV_FP16;
			block_sums[cur_ind] = 1;
		}
	}

	// 2.) Load q and init output. Likely be more token out spaces then threads.	
	int smem_ind;
	int global_ind;

	int cur_row;
	int tile_base_ind;

	int q_group_dim_bits = __ffs(q_group_dim) - 1;

	// each warp loads tiles for 8 rows
	for (int q_base_row = warp_id * Q_HEAD_PHASE_SIZE; q_base_row < num_rows_padded; q_base_row+=(num_warps * Q_HEAD_PHASE_SIZE)){

		// there are head_dim / 8 tiles per "col"
		// there are 8 rows per "row"
		tile_base_ind = (q_base_row >> 3) * (head_dim >> 3);

		// this is row within the implicit (num tokens in block * q_group_dim, head_dim) matrix
		cur_row = q_base_row + (lane_id & 0x7);

		//token_id = cur_row / (q_group_dim);
		//cur_q_group = cur_row % q_group_dim;
		
		if (cur_row < (num_tokens_in_block * q_group_dim)){

			// loading 8x8 tiles (up to 4 per warp) into unique smem banks
			// each thread loads a row

			// TODO: handle non divisble by 8 edge cases...
			for (int tile_offset = (lane_id >> 3); tile_offset < n_head_tiles; tile_offset+=4){

				
				// x 64 because each tile is 8x8
				// tile_offset increases by 4 each iteration because 4 warps 
				// lane_id >> 2 is row within this tile
				smem_ind = (tile_base_ind + tile_offset) * 64 + (lane_id & 0x7) * 8;

				*((int4 *) &(block_out_tiles[smem_ind])) = zero_int4;

				global_ind = (token_ind_start + (cur_row >> q_group_dim_bits)) * model_dim + kv_head * q_group_dim * head_dim + (cur_row & (q_group_dim - 1)) * head_dim + tile_offset * 8;

				*((int4 *) &(block_q_tiles[smem_ind])) = *((int4 *) &(queries[global_ind]));
			}
		}
		else{

			// loading 8x8 tiles (up to 4 per warp) into unique smem banks
			// each thread loads a row
			for (int tile_offset = (lane_id >> 3); tile_offset < n_head_tiles; tile_offset+=4){
				
				// x 64 because each tile is 8x8
				smem_ind = (tile_base_ind + tile_offset) * 64 + (lane_id & 0x7) * 8;
				
				*((int4 *) &(block_out_tiles[smem_ind])) = zero_int4;
				*((int4 *) &(block_q_tiles[smem_ind])) = zero_int4;
			}

		}
	}

	// __half cur_val;
	__half prev_max;
	__half new_max;
	
	float prev_sum;
	float new_sum;

	__half2 half2_vec;
	const __half head_dim_scale_factor = __float2half(1.0 / sqrtf(head_dim));

	int start_seq_pos = seq_positions[token_ind_start];


	// REGISTERS HOLDING ADDRESS TO LOAD IN MATRIX
	uint32_t RA[4];
	uint32_t RB[4];
	uint32_t RC[2];
	// used to get the output of transpose before writing to smem
	uint32_t RTemp[2];

	uint32_t A_lane_addr;
	uint32_t B_lane_addr;
	uint32_t C_lane_addr;

	int cur_base_q_row;
	int cur_base_token_pos;

	int max_seq_offset;

	//unsigned warp_mask;


	for (int cur_base_seq_phase = 0; cur_base_seq_phase < block_max_seq_pos; cur_base_seq_phase+=SEQ_PHASE_SIZE) {

		// ensure to sync before loading new seq phase for kvs...
		__syncthreads();

		for (int seq_base_row = warp_id; seq_base_row < SEQ_PHASE_SIZE; seq_base_row+=num_warps) {
			
			tile_base_ind = (seq_base_row >> 3) * (head_dim >> 3);

			if ((cur_base_seq_phase + seq_base_row) < block_max_seq_pos){
				for (int tile_offset = lane_id; tile_offset < n_head_tiles; tile_offset+=32){
					
					smem_ind = (tile_base_ind + tile_offset) * 64 + ((seq_base_row + tile_offset) & 0x7) * 8;

					global_ind = (cur_base_seq_phase + seq_base_row) * kv_dim + kv_head * head_dim + tile_offset * 8;

					*((int4 *) &(seq_phase_k_tiles[smem_ind])) = *((int4 *) &(seq_keys[global_ind]));
					*((int4 *) &(seq_phase_v_tiles[smem_ind])) = *((int4 *) &(seq_values[global_ind]));;

				}
			}
			else{
				for (int tile_offset = lane_id; tile_offset < n_head_tiles; tile_offset+=32){
					smem_ind = (tile_base_ind + tile_offset) * 64 + ((seq_base_row + tile_offset) & 0x7) * 8;
					*((int4 *) &(seq_phase_k_tiles[smem_ind])) = zero_int4;
					*((int4 *) &(seq_phase_v_tiles[smem_ind])) = zero_int4;
				}

			}
		}

		__syncthreads();

		// Seq phases are 32 x head dim matrices 


		// remember this is for this specific kv head, which is head_dim entries per position



		// TL;DR Each warp takes down 8 rows of "Q", total number of rows is #(tokens in block * q_group_dim
		// The output goes in a temporary buffer that gets updated every phase 
		// which is then finally put into output array after all the sequence phases. Tokens in block is assigned
		// during seq batch finalization and is paired with the launch configuration. Every block must have
		// tokens only in the same sequence and they must be ordered in monitonically increasing sequence positions.


		// Each warp within an iteration (responsible for a specific output chunk) will do a series of <head_dim / k> matrix multiples each of (8, 16) x (16, 32)
		// in order to obtain the correct dot product for 8 "rows" of Q. 
		// Note: Every token will produce q_group_dim rows in this generalized Q matrix correspondign to this kv head.
		// but tokens can be aggrated if they are part of the same sequence, so a given warp iteration during this phase 
		// might update the a chunk of parital outputs of 2 tokens if  q_group_dim = 4 (llama 8B) or 1 token if (q_group_dim = 8). 
		// Here we are taking advantage of the fact that different query heads utilize 
		// the same keys/values so we are grouping them in matmul form.


		// The output of the dot-product phase yields a 8x32 matrix where the columns are dot product of the given query associated with current
		// "row of Q" with the 32 sequences during this phase. (In reality we write the result in column major format for good
		// reading of the tile during the matmul's with values). We then do online softmax across each of these rows (continuation
		// of previous seq phases). 

		// After softmax is completed we and properly update the output by doing 2 sets (because 32 / 16 = 2) 
		// of <head_dim / 32> matmuls of size (8, 16) x (16, 32) where the former matrix is 16 sequences out of 32 across each of the 8
		// rows this warp is working with. The latter matrix is a porition of the values assoicated with these 16 sequence poisitions and 
		// here we are grabbing 32 entries out of the head dim. This processess is repeated if each warp needs to process more 
		// than 8 rows of Q in order to satisify processed all the tokens that have been assigned to this threadblock. 


		


		// The number of iterations each warp will work is dependent on how many total "rows" of Q are in this matrix
		// (corresponding to this threadblock) and how many total warps there are. The mapping between real token rows
		// and how many total tokens each threadblock should work on is configurated during runtime because
		// (hardware constrains, dimension of model, sequence lengths in batch) all play a role. Note that every threadblock only works
		// on tokens that all are part of the same sequence and there will be as many threadblocks assigned to sequence as is necessary
		// to process all of the new tokens part of the sequence.


		// BIG TODO: make a queue so warps can be more balanced instead of waiting at the loading barrier...


		cur_base_q_row = warp_id * Q_HEAD_PHASE_SIZE;
		cur_base_token_pos = start_seq_pos + (cur_base_q_row >> q_group_dim_bits);

		// advance past tokens that have already completed (i.e. they don't need to compare against this seq phase because of masking)
		while (cur_base_token_pos < cur_base_seq_phase){
			cur_base_q_row += num_warps * Q_HEAD_PHASE_SIZE;
			cur_base_token_pos += num_warps * (Q_HEAD_PHASE_SIZE >> q_group_dim_bits);
		}

		while (cur_base_q_row < (num_tokens_in_block * q_group_dim)) {


			// 1.) get QK^T

			// We have that Q is (num_tokens_in_block * head_dim) x head_dim matrix and that K is a 32xhead_dim matrix

			// This warp is currently operating on a base row of Q that should be (8 x head_dim)
			// We will do successive matmuls to then get an (8 x 32) intermeidate matrix that we can do online softmax on

			// However we want to use m16, k16, n8 MMA instruction so in reality, we will be doing KQ^T == QK^T
			// meaning our output will be transposed into (32xhead_dim), which is what we want for softmax phase

			// Because seq phase is 32, we will do 2 rounds


			// The A matrix comes from seq_phase_k and takes in 4 registers corresponding to the 4 different 8x8 sub-matrices that make up 16x16 A matrix
			// Each group of 4 threads loads 16 bytes == 8 elements == 1 row

			// The rows of the first matrix are specified by the addresses provided by threads 0-7
			// The rows of the second matrix are specified by the addresses provided by threads 8-15
			// The rows of the third matrix are specified by the addresses provided by threads 16-23
			// The rows of the fourth matrix are specified by the addresses provided by threads 16-23


			tile_base_ind = (cur_base_q_row >> 3) * (head_dim >> 3);

			half2_vec = __half2half2(head_dim_scale_factor);

			#pragma unroll
			for (int s = 0; s < SEQ_PHASE_SIZE / 16; s++){

				
				// REF: https://docs.nvidia.com/cuda/pdf/ptx_isa_8.5.pdf (pages 398 & page 364)

				// Initialize the output to zero
				RC[0] = 0;
				RC[1] = 0;

				
				// doing 2 head tiles per iteration because (16, 16) x (16, 8) matmul
				for (int k = 0; k < n_head_tiles; k+=2){




					// doing (16, 16) x (16, 8) matmul where A is keys and B is queries



					// s * 16 indicates if we are in the top half of the 32xhead dim or bottom half

					// lane_id & 0x8 indicates if threads are in groups 8-15 or 24-31 (in which case needs to load in grid offset)
					// lane_id & 0x7 indicates the row within 8x8 tile

					// lane_id >> 4 indicates if threads are in groups 16-23 or 24-31 in which case they need to offset by 8 elements in row = 1 tile
				

					// we permuted the rows in each tile to allow for conflict free so using ((lane_id + k + (lane_id >> 4)) & 0x7) to obtain true row
					smem_ind = ((s * (SEQ_PHASE_SIZE / 16) * n_head_tiles + ((lane_id & 0x8) >> 3) * n_head_tiles + k + (lane_id >> 4)) * 64) + ((lane_id + k + (lane_id >> 4)) & 0x7) * 8;
					A_lane_addr = __cvta_generic_to_shared(&(seq_phase_k_tiles[smem_ind]));
					
					// Spliting the 16x16 matrix into 4 submatices

					// RA[0] is top left, RA[1] is bottom left, RA[2] is bottom left, and RA[3] is bottom right
					// (this is based off the thread addressing scheme)

					LDMATRIX_X4(RA[0], RA[1], RA[2], RA[3], A_lane_addr);


					// block_q is (num_padded_rows x head_dim) matrix, but we only want to load in the 8 rows following cur_base_q_row

					// upper 16 lanes don't matter

					// using tile_base_ind derived from cur_base_q row

					// (lane_id & 0x8) >> 3 indicates if this thread should load from a tile offset
					smem_ind = (tile_base_ind + k + ((lane_id & 0x8) >> 3)) * 64 + (lane_id & 0x7) * 8;
					B_lane_addr = __cvta_generic_to_shared(&(block_q_tiles[smem_ind]));

					// RB[0] will then contain an 8x8 matrix where each row has 8 dims and each column corresponds to row in q matrix
					// RB[1] contains the second string of 8 dims and each column corerspondds to row in q matrix

					// i.e. each address should be referring to a sequence of 8 elements that represent half of a column
					LDMATRIX_X2(RB[0], RB[1], B_lane_addr);

					

					// B is expected to be in column-major format as (16x8)
					HMMA16816(RC[0], RC[1], RA[0], RA[1], RA[2], RA[3], RB[0], RB[1], RC[0], RC[1]);
				}

				

				
				// Now we have the full dot product for these 16 sequences and we can save the results to seq phase out
				// in order to do online softmax

				// Save this section of 16 sequenc

				// each thread holds 2 f16x2 outputs (which are packed into u32 register) of the 16x8 result, offset by 8 rows in the 16x8 output


				// storing in tranposed order so we have a 8 x 16 matrix represetnting QK^T as we should...

				// MATMOV ptx instruction
				MAT_TRANS(RTemp[0], RC[0]);
				MAT_TRANS(RTemp[1], RC[1]);



				// get the correct row, but then cast to uint32_t and offset by lane_id % 4 as uint32_t's because each thread holds 2 fp16 items

				// scale by head dim

				if (s == 1){
					*((__half2 *)(&RA[2 * s])) = __hmul2(*(__half2 *) (&RTemp[0]), half2_vec);
    				*((__half2 *)(&RA[2 * s + 1])) = __hmul2(*(__half2 *) (&RTemp[1]), half2_vec);
    			}
    			else{
    				*((__half2 *)(&RB[2])) = __hmul2(*(__half2 *) (&RTemp[0]), half2_vec);
    				*((__half2 *)(&RB[3])) = __hmul2(*(__half2 *) (&RTemp[1]), half2_vec);
    			}
			}

			// Now copy RB[2] and RB[3] to RA
			// (RA[2] and RA[3] were updated in the last iteration so they are already correct)
			RA[0] = RB[2];
			RA[1] = RB[3];

			// We will need to save the previous max and sum when modifying the outputs
			// Each lane has a designated row of outputs to update and they will update 4 elements
			// each within the 8x16 matrix (2 from first 8x8 and 2 from second 8x8)
			prev_max = block_maxs[cur_base_q_row + (lane_id >> 2)];
			prev_sum = block_sums[cur_base_q_row + (lane_id >> 2)];
			
			// Apply causal mask
			max_seq_offset = min(cur_base_token_pos + ((lane_id >> 2) >> q_group_dim_bits) - cur_base_seq_phase, SEQ_PHASE_SIZE);
			
			// CAUSAL MASK
			new_max = prev_max;

			for (int seq_section = 0; seq_section < 4; seq_section++){

				if (((seq_section << 3) + 2 * (lane_id & 0x3) + 1) <= max_seq_offset){
					new_max = __hmax(new_max, __hmax(__low2half(*((__half2 *) &RA[seq_section])),  __high2half(*((__half2 *) &RA[seq_section]))));
				}
				else if (((seq_section << 3) + 2 * (lane_id & 0x3)) == max_seq_offset){
					new_max = __hmax(new_max, __low2half(*((__half2 *) &RA[seq_section])));
					*((__half2 *) (&RA[seq_section])) = __halves2half2(__low2half(*((__half2 *) &RA[seq_section])), NEG_INF_DEV_FP16);
				}
				else{
					*((__half2 *) (&RA[seq_section]))  = __halves2half2(NEG_INF_DEV_FP16, NEG_INF_DEV_FP16);
				}
			}

			new_max = __hmax(new_max, __shfl_down_sync(0xFFFFFFFF, new_max, 2, 4));
			new_max = __hmax(new_max, __shfl_down_sync(0xFFFFFFFF, new_max, 1, 4));
			
			new_max = __shfl_sync(0xFFFFFFFF, new_max, 0, 4);

			new_sum = 0;
			
			float v1;
			float v2;

			// already set maked value to neg inf, so no need to check again
			#pragma unroll
			for (int seq_section = 0; seq_section < 4; seq_section++){
				v1 = expf(__half2float(__low2half(*((__half2 *) &RA[seq_section])) - new_max));
				v2 = expf(__half2float(__high2half(*((__half2 *) &RA[seq_section])) - new_max));
				new_sum += v1 + v2;
				*((__half2 *) (&RA[seq_section])) = __halves2half2(__float2half(v1), __float2half(v2));
			}

			new_sum += __shfl_down_sync(0xFFFFFFFF, new_sum, 2, 4);
			new_sum += __shfl_down_sync(0xFFFFFFFF, new_sum, 1, 4);

			// now the results are lanes where lane_id & 0x3 == 0
			if ((lane_id & 0x3) == 0){
				new_sum += prev_sum * expf(__half2float(prev_max - new_max));
				block_maxs[cur_base_q_row + (lane_id >> 2)] = new_max;
				// update block sums/maxs
				block_sums[cur_base_q_row + (lane_id >> 2)] = new_sum;
			}

			// loading value from the row leader...
			__syncwarp();

			new_sum = block_sums[cur_base_q_row + (lane_id >> 2)];
			new_max = block_maxs[cur_base_q_row + (lane_id >> 2)];

			// now update B matrix using A matrix modified (masked out/adjusted attention scores)
			#pragma unroll
			for (int seq_section = 0; seq_section < 4; seq_section++){
				*((__half2 *) (&RB[seq_section])) = __h2div(*((__half2 *) &RA[seq_section]), __half2half2(__float2half(new_sum)));
			}



			// Now seq_phase_out contains an 8x32 matrix of attention scores for this set of 32 sequences
			// across the 8 rows of q

			// We need to do matmul with values corresponding to these sequences and update the temporary output
			// (held in block_out)

			// Similarly to our KQ^T computation we can do V^TS^T where V is (head_dim, 32) and S^T is (32, 8)

			// The result is then a (head_dim, 8) portion of outputs for the 8 rows

			// We can do head_dim / 16 outer rounds for portion of head dim, and then 2 inner rounds for each half of the seq phase

			// However we now are not starting from scratch but rather loading the previous output, doing scalar updates per row
			// and the adding the new results

			// the row id to update for both matrices is lane_id >> 2 (= lane_id / 4)
			// scale based on new max and new sum relative to prev sum and prev max
			// this thread already saved the prev max and prev sum before the new ones were calculated during softmax
			half2_vec = __half2half2(__float2half((prev_sum / new_sum) * expf(__half2float(prev_max - new_max))));

			for (int k = 0; k < n_head_tiles; k+=2) {

				// a.) Load prior outputs (8x16) along this head dim
				
				// block out is (num_tokens_in_block * q_group_dim) x head_dim

				// we want to take a slice of [cur_base_q_row: cur_base_q_row + 8, k * 16: k * 16 + 16]
							
				// same indexing scheme as in block_q
				// upper 16 lanes don't matter
				
				// lanes 0-7 are going to load tile # k
				// lanes 8-15 are going to load tile # k + 1

				smem_ind = (tile_base_ind + k + ((lane_id & 0x8) >> 3)) * 64 + (lane_id & 0x7) * 8;
				C_lane_addr = __cvta_generic_to_shared(&(block_out_tiles[smem_ind]));
				LDMATRIX_X2(RTemp[0], RTemp[1], C_lane_addr);

				// RC[0] contains an 8x8 matrix with the where each row correspnds to a row of Q and the columns are the first 8 elements of head dim
				// RC[1] contains 8x8 matrix with the second portion of 8 head dim els
	

				// need to multiply the 4 half elements in RC[0] and RC[1] by this amount
				// probably an instrinstic to do this cleaner...
				*((__half2 *) (&RTemp[0])) = (__hmul2((*((__half2 *) &RTemp[0])), half2_vec));
				*((__half2 *) (&RTemp[1])) = (__hmul2((*((__half2 *) &RTemp[1])), half2_vec));
				

				// TODO: For convience with output storing in row major
				// but really should have block out be column major during computation
				// and then at the final output convert back (to avoid these transposes...)
				// also would need to modify how thread out scale chooses the elements to modify...

				MAT_TRANS(RC[0], RTemp[0]);
				MAT_TRANS(RC[1], RTemp[1]);

				#pragma unroll
				for (int s = 0; s < SEQ_PHASE_SIZE / 16; s++){
					
					// now need to load a 16x16 chunk of values

					// where the values are held within (32, head dim) matrix

					// we want to get 4 8x8 matrices (16x16) where the rows are unique head dim and columns are unique seq inds
					// thus we want to transpose

					// the top left matrix when we do matmul (RA[0]) should be the (first 8 head_dim x first 8 sequences). The bottom left 
					// matrix when we do matmul (RA[1]) should be (second set of 8 head_dim x first 8 sequences) and the top right
					// (RA[2]) should be (first 8 head_dim x second set of 8 sequences)

					// Addresses from lanes 0-7 correspond to rows of RA[0], from lanes 8-15 correspond to RA[1], etc.


					// same as loading from seq_pahse_k_tiles...
					smem_ind = ((s * (SEQ_PHASE_SIZE / 16) * n_head_tiles + ((lane_id & 0x8) >> 3) * n_head_tiles + k + (lane_id >> 4)) * 64) + ((lane_id + k + (lane_id >> 4)) & 0x7) * 8;
					A_lane_addr = __cvta_generic_to_shared(&(seq_phase_v_tiles[smem_ind]));

					// RA[0] is top left, RA[1] is bottom left, RA[2] is bottom left, and RA[3] is bottom right
					// (this is based off the thread addressing scheme)

					// However because we are transposing each 8x8 matrix we want to switch RA[2] and RA[1]
					// so that RA[1] and RA[2] point to the lower left and upper right of transposed 16x16 matrix
					// respectively...
					// (and will multiply them normal later)
					LDMATRIX_X4_TRANS(RA[0], RA[2], RA[1], RA[3], A_lane_addr);


					// // and load in the seq phase out portion
					// // only lower 16 threads pass in addresses here

					// // we want the first 8 threads to load in the first 8 sequences
					// // and second to load the next 8 sequences

					// // the (lane_id + (2 * s + ((lane_id & 0x8) >> 3))) & 0x7) is because within seq phase out we are stroing permuted rows to avoid bank conflicts during softmax
					// B_lane_addr =  __cvta_generic_to_shared(&(seq_phase_out_tiles[warp_id * (SEQ_PHASE_SIZE * Q_HEAD_PHASE_SIZE) + (2 * s + ((lane_id & 0x8) >> 3)) * 64 + ((lane_id + (2 * s + ((lane_id & 0x8) >> 3))) & 0x7) * 8]));
					// LDMATRIX_X2(RB[0], RB[1], B_lane_addr);

					// We have already loaded B into 4 registers, and we will choose the correct 2 based
					// on seq phase

					HMMA16816(RC[0], RC[1], RA[0], RA[1], RA[2], RA[3], RB[2 * s], RB[2 * s + 1], RC[0], RC[1]);

				}

			
				MAT_TRANS(RTemp[0], RC[0]);
				MAT_TRANS(RTemp[1], RC[1]);


				// get the correct row, but then cast to uint32_t and offset by lane_id % 4 as uint32_t's because each thread holds 2 fp16 items
				*((uint32_t *)(&block_out_tiles[(tile_base_ind + k) * 64 + (lane_id >> 2) * 8]) + (lane_id & 0x3)) = RTemp[0];
				// the next set of 8 columns
    			*((uint32_t *)(&block_out_tiles[(tile_base_ind + k + 1) * 64 + (lane_id >> 2) * 8]) + (lane_id & 0x3)) = RTemp[1];

			}

			// repeat for other tokens
			cur_base_q_row += num_warps * Q_HEAD_PHASE_SIZE;
			cur_base_token_pos += num_warps * (Q_HEAD_PHASE_SIZE >> q_group_dim_bits);
		}
	}


	// Wrapped up and time to do output...
	__syncthreads();

	// each warp loads tiles for 8 rows
	for (int q_base_row = warp_id * Q_HEAD_PHASE_SIZE; q_base_row < num_rows_padded; q_base_row+=(num_warps * Q_HEAD_PHASE_SIZE)){

		// there are head_dim / 8 tiles per "col"
		// there are 8 rows per "row"
		tile_base_ind = (q_base_row >> 3) * (head_dim >> 3);

		// this is row within the implicit (num tokens in block * q_group_dim, head_dim) matrix
		cur_row = q_base_row + (lane_id & 0x7);

		//token_id = cur_row / (q_group_dim);
		//cur_q_group = cur_row % q_group_dim;
		
		if (cur_row < (num_tokens_in_block * q_group_dim)){

			// loading 8x8 tiles (up to 4 per warp) into unique smem banks
			// each thread loads a row

			// TODO: handle non divisble by 8 edge cases...
			for (int tile_offset = (lane_id >> 3); tile_offset < n_head_tiles; tile_offset+=4){

				global_ind = (token_ind_start + (cur_row >> (q_group_dim_bits))) * model_dim + kv_head * q_group_dim * head_dim + (cur_row & (q_group_dim - 1)) * head_dim + tile_offset * 8;

				// x 64 because each tile is 8x8
				// tile_offset increases by 4 each iteration because 4 warps 
				// lane_id >> 2 is row within this tile
				smem_ind = (tile_base_ind + tile_offset) * 64 + (lane_id & 0x7) * 8;

				*((int4 *) &(out[global_ind])) = *((int4 *) &(block_out_tiles[smem_ind]));
			}
		}
	}
}



extern "C" __global__ void silu_hadamard_fp16_kernel(uint64_t N, __half * x_w1, __half * x_w3, __half * out){

	uint64_t i = (blockIdx.x * blockDim.x + threadIdx.x);
	// here N is total_tokens * ffn_dim 
	if (i < N){

		float x_w1_val = __half2float(x_w1[i]);
		float x_w3_val = __half2float(x_w3[i]);

		// overwrite contents in x_w1
		float silu_x_w1 = x_w1_val / (1 + expf(-1 * x_w1_val));
		
		// normally would set out to be x_w1...
		out[i] = __float2half(silu_x_w1 * x_w3_val);
	}
}


extern "C" __global__ void condense_rows_fp16_kernel(uint64_t N, int n_rows, int n_cols, __half * X_in, __half * X_out, int * row_remapping) {

	uint64_t i = (blockIdx.x * blockDim.x + threadIdx.x);
	if (i < N){
		int prev_row = i / n_cols;

		int new_row = row_remapping[prev_row];
		if (new_row == -1){
			return;
		}

		int cur_col = i % n_cols;
		
		__half cur_val = X_in[i];

		X_out[new_row * n_cols + cur_col] = cur_val;
	}
}




// Assumes N = # columns
// And block_idx is the row

// very naive implementation for now....
extern "C" __global__ void softmax_fp16_to_float_kernel(int n_cols, __half * X_in, float * out, uint32_t * arg_maxs) {

	uint64_t row_ind = blockIdx.x;

	uint64_t row_offset = row_ind * ((uint64_t) n_cols);
	__half * row_start = X_in + row_offset;

	int thread_id = threadIdx.x;

	__shared__ __half warp_maxs[32];
	__shared__ __half warp_sums[32];
	__shared__ __half global_max[1];
	__shared__ __half global_sum[1];

	int warp_id = thread_id / 32;
	int lane_id = thread_id % 32;
	int num_warps = blockDim.x / 32;

	

	if (warp_id == 0){
		warp_maxs[lane_id] = NEG_INF_DEV_FP16;
		warp_sums[lane_id] = 0;
	}

	__syncthreads();

	__half other_val;

	__half new_max = NEG_INF_DEV_FP16;


	unsigned warp_mask = 0xFFFFFFFFU;

	int cur_ind = thread_id;

	// Assuming N is a multiple of 32 for simplicity...
	while (cur_ind < n_cols){

		new_max = __hmax(new_max, row_start[cur_ind]);

		#pragma unroll
		for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
			other_val = __shfl_down_sync(warp_mask, new_max, warp_offset);
			new_max = __hmax(new_max, other_val);
		}

		cur_ind += num_warps * 32;
	}

	if (lane_id == 0){
		warp_maxs[warp_id] = new_max;
	}

	__syncthreads();


	if (warp_id == 0){

		new_max = warp_maxs[lane_id];

		#pragma unroll
		for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
			other_val = __shfl_down_sync(warp_mask, new_max, warp_offset);
			new_max = __hmax(new_max, other_val);
		}

		if (lane_id == 0){
			global_max[0] = new_max;
		}
	}

	__syncthreads();


	// now do sums

	cur_ind = thread_id;

	__half overall_max = global_max[0];

	float total_sum = 0;
	float new_sum;
	__half cur_val;
	while (cur_ind < n_cols){

		cur_val = row_start[cur_ind];
		if (arg_maxs && (cur_val == overall_max)){
			arg_maxs[row_ind] = (uint32_t) cur_ind;
		}

		new_sum = expf(__half2float(cur_val - overall_max));

		#pragma unroll
		for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
			new_sum += __shfl_down_sync(warp_mask, new_sum, warp_offset);
		}

		if (lane_id == 0){
			total_sum += new_sum;
		}

		cur_ind += num_warps * 32;
	}

	if (lane_id == 0){
		warp_sums[warp_id] = total_sum;
	}

	__syncthreads();

	if (warp_id == 0){

		total_sum = warp_sums[lane_id];

		#pragma unroll
		for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
			total_sum += __shfl_down_sync(warp_mask, total_sum, warp_offset);
		}

		if (lane_id == 0){
			global_sum[0] = total_sum;
		}
	}

	__syncthreads();


	// now do output

	float overall_sum = global_sum[0];

	float * out_start = out + row_offset;

	cur_ind = thread_id;

	while (cur_ind < n_cols){

		out_start[cur_ind] = expf(__half2float(row_start[cur_ind] - overall_max)) / overall_sum;
		cur_ind += num_warps * 32;
	}
}

// TODO: could read in row of data to smem...
extern "C" __global__ void softmax_fp16_kernel(int n_cols, __half * X) {

	uint64_t row_ind = blockIdx.x;

	uint64_t row_offset = row_ind * ((uint64_t) n_cols);
	__half * row_start = X + row_offset;

	int thread_id = threadIdx.x;

	__shared__ __half warp_maxs[32];
	__shared__ __half warp_sums[32];
	__shared__ __half global_max[1];
	__shared__ __half global_sum[1];

	int warp_id = thread_id / 32;
	int lane_id = thread_id % 32;
	int num_warps = blockDim.x / 32;

	

	if (warp_id == 0){
		warp_maxs[lane_id] = NEG_INF_DEV_FP16;
		warp_sums[lane_id] = 0;
	}

	__syncthreads();

	__half other_val;

	__half new_max = NEG_INF_DEV_FP16;


	unsigned warp_mask = 0xFFFFFFFFU;

	int cur_ind = thread_id;

	// Assuming N is a multiple of 32 for simplicity...
	while (cur_ind < n_cols){

		new_max = __hmax(new_max, row_start[cur_ind]);

		#pragma unroll
		for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
			other_val = __shfl_down_sync(warp_mask, new_max, warp_offset);
			new_max = __hmax(new_max, other_val);
		}

		cur_ind += num_warps * 32;
	}

	if (lane_id == 0){
		warp_maxs[warp_id] = new_max;
	}

	__syncthreads();


	if (warp_id == 0){

		new_max = warp_maxs[lane_id];

		#pragma unroll
		for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
			other_val = __shfl_down_sync(warp_mask, new_max, warp_offset);
			new_max = __hmax(new_max, other_val);
		}

		if (lane_id == 0){
			global_max[0] = new_max;
		}
	}

	__syncthreads();


	// now do sums

	cur_ind = thread_id;

	__half overall_max = global_max[0];

	float total_sum = 0;
	float new_sum;
	while (cur_ind < n_cols){

		new_sum = expf(__half2float(row_start[cur_ind] - overall_max));

		#pragma unroll
		for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
			new_sum += __shfl_down_sync(warp_mask, new_sum, warp_offset);
		}

		if (lane_id == 0){
			total_sum += new_sum;
		}

		cur_ind += num_warps * 32;
	}

	if (lane_id == 0){
		warp_sums[warp_id] = total_sum;
	}

	__syncthreads();

	if (warp_id == 0){

		total_sum = warp_sums[lane_id];

		#pragma unroll
		for (int warp_offset = 16; warp_offset > 0; warp_offset >>= 1){
			total_sum += __shfl_down_sync(warp_mask, total_sum, warp_offset);
		}

		if (lane_id == 0){
			global_sum[0] = total_sum;
		}
	}

	__syncthreads();


	// now do output

	float overall_sum = global_sum[0];

	cur_ind = thread_id;

	while (cur_ind < n_cols){

		row_start[cur_ind] = __float2half(expf(__half2float(row_start[cur_ind] - overall_max)) / overall_sum);
		cur_ind += num_warps * 32;
	}
}

// subtracts 1 from correct values

// launched with number of rows (total tokens to predict)
extern "C" __global__ void cross_entropy_loss_fp16_kernel(int n_rows, int n_cols, __half * pred_logits, uint32_t * labels){

	uint64_t i = (blockIdx.x * blockDim.x + threadIdx.x);

	int row_ind;
	uint32_t correct_ind;
	if (i < n_rows){
		row_ind = i / n_cols;
		correct_ind = labels[row_ind];
		pred_logits[row_ind * n_cols + correct_ind] -= CONST_ONE_DEV_FP16;
	}

}
