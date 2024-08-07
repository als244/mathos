#include "rocblas_funcs.h"


#define TO_PRINT 0

uint32_t * create_cu_mask(unsigned int total_cu, unsigned int cu_count, uint32_t * ret_num_entries){

	unsigned int num_entries = MY_CEIL(total_cu, 32);

	uint32_t * cu_mask = (uint32_t *) calloc(num_entries, sizeof(uint32_t));

	// ALL 1's
	uint32_t full_mask = ~(uint32_t)0;

	unsigned int remain_cu_count = cu_count;
	int entry_ind = 0;
	while (remain_cu_count > 0){
		if (remain_cu_count >= 32){
			cu_mask[entry_ind] = full_mask;
			remain_cu_count -= 32;
		}
		else{
			cu_mask[entry_ind] = full_mask >> (32 - remain_cu_count);
			remain_cu_count = 0;
		}

		entry_ind++;
	}

	// set num entries
	*ret_num_entries = num_entries;

	return cu_mask;
}

// 1.) Init Cuda driver
// 2.) Get device handle
// 3.) Create context with specified sm_count
//		- assumes using MPS and that CUDA_MPS_ENABLE_PER_CTX_DEVICE_MULTIPROCESSOR_PARTITIONING=1 is set
// 4.) Set context

int initialize_stream(int device_id, unsigned int cu_count, hipStream_t * ret_stream){

	hipError_t result;
	const char * err;

	unsigned long flags = 0;
	result = hipInit(flags);
	if (result != hipSuccess){
    	err = hipGetErrorString(result);
    	fprintf(stderr, "Could not init hip: %s\n", err);
    	return -1;
    }

    hipDeviceAttribute_t attr_total_cu_count = hipDeviceAttributeMultiprocessorCount; 
    int total_cu_count;
    result = hipDeviceGetAttribute(&total_cu_count, attr_total_cu_count, device_id);
    if (result != hipSuccess){
    	err = hipGetErrorString(result);
    	fprintf(stderr, "Could not get total number of compute units on device: %s\n", err);
    	return -1;
    }

    if (TO_PRINT){
    	printf("Total Compute Unit Count: %d\n", total_cu_count);
    }

	// create stream with correct compute mask
	//	- should be more precice about mask when actually doing in practice
	//		- (i.e. track which compute units are occupied and set mask accorindgly)
	//		- for now just setting the first cu_count units to be 1 bit

    // number of uint32_t entries in cuMask (= ceil(total cu count / 32))
	uint32_t num_mask_entries;
    uint32_t * cu_mask = create_cu_mask(total_cu_count, cu_count, &num_mask_entries);


    result = hipExtStreamCreateWithCUMask(ret_stream, num_mask_entries, cu_mask);
    if (result != hipSuccess){
    	err = hipGetErrorString(result);
    	fprintf(stderr, "Could not get create stream with mask: %s\n", err);
    	return -1;
    }

    // set device id

    result = hipSetDevice(device_id);
    if (result != hipSuccess){
    	err = hipGetErrorString(result);
    	fprintf(stderr, "Could not set to device #%d: %s\n", device_id, err);
    	return -1;
    }

    // set this program to spin waiting until completition

    unsigned dev_flag = hipDeviceScheduleSpin;

    result = hipSetDeviceFlags(dev_flag);
    if (result != hipSuccess){
    	err = hipGetErrorString(result);
    	fprintf(stderr, "Could not set sched spin flag on device: %s\n", err);
    	return -1;
    }

	// SUCCESS!
	return 0;
}

// PASSING IN ROW-MAJOR matrices!
int do_rocblas_matmul(hipStream_t cu_mask_stream, size_t M, size_t K, size_t N, void * d_A, void * d_B, void * d_C, uint64_t * ret_elapsed_ns){

	const char * err;
	hipError_t result;
	rocblas_status status;

    // optionally initialize rocblas to prevent startup costs when gemm is called
    rocblas_initialize();


    // Create rocblas handle!
    rocblas_handle handle;
    status = rocblas_create_handle(&handle);
    if (status != rocblas_status_success){
    	err = rocblas_status_to_string(status);
    	fprintf(stderr, "Error: could not create rocblas handle: %s\n", err);
    	return -1;
    }


    // set stream that was created with compute unit mask in this program's initialize() func.
    status = rocblas_set_stream(handle, cu_mask_stream);
    if (status != rocblas_status_success){
    	err = rocblas_status_to_string(status);
    	fprintf(stderr, "Error: could not set compute unit mask'ed stream in rocblas: %s\n", err);
    	return -1;
    }

    // prepare the matmul

    // Assumes Column Major storing!
    // thus if we feed in row-major, that is equivalent to 
    // to the matrices already being transposed

    // AB = C is equivalent to B^T * A^T = C^T
    // now because rocBlas is doing computations within col-major
    // ordering we can compute C^T and reinterpret it in row-major as just C!
    rocblas_operation trans_a = rocblas_operation_none;
    rocblas_operation trans_b = rocblas_operation_none;

    // Assuming row-major ordering 
    //  - (where leading dimension is number of columns)
    int lda = K;
    int ldb = N;
    int ldc = N;

    float alpha = 1.0, beta = 0.0;

    struct timespec start, stop;
	uint64_t timestamp_start, timestamp_stop, elapsed_ns;


    result = hipStreamSynchronize(cu_mask_stream);
    if (result != hipSuccess){
    	err = hipGetErrorString(result);
    	fprintf(stderr, "Could not sync stream: %s\n", err);
    	return -1;
    }

    clock_gettime(CLOCK_REALTIME, &start);
    

    // ACUTALLY PERFORM MATRIX MULTIPLY!
    
    // here M, N, K are the values after doing op(A)/op(B)

    // we will pass in row-major then do not do any transposes
    // (which rocblas will interpret as transposed) and then 
    // reorder such that A = B^T and B = A^T
    // This also means that M = original N, and N = orignal M

    // But now we are computing B^T * A^T = C^T and interpreting the results in row-major
    // so we need to re-arrange
    status = rocblas_sgemm(handle, trans_b, trans_a, N, M, K, &alpha, d_B, ldb, d_A, lda, &beta, d_C, ldc);
    
    result = hipStreamSynchronize(cu_mask_stream);
  

    clock_gettime(CLOCK_REALTIME, &stop);

    timestamp_start = start.tv_sec * 1e9 + start.tv_nsec;
    timestamp_stop = stop.tv_sec * 1e9 + stop.tv_nsec;

	elapsed_ns = timestamp_stop - timestamp_start;

    if (result != hipSuccess){
        err = hipGetErrorString(result);
        fprintf(stderr, "Could not sync stream: %s\n", err);
        return -1;
    }

	/* RETURN RESULT */
	*ret_elapsed_ns = elapsed_ns;

    if ((status != rocblas_status_success) && (status != rocblas_status_perf_degraded)){
    	err = rocblas_status_to_string(status);
    	fprintf(stderr, "Error: actual rocblas_sgemm() failed: %s\n", err);
    	return -1;
    }


    // destroy rocBlas handle
    status = rocblas_destroy_handle(handle);
    if (status != rocblas_status_success){
    	err = rocblas_status_to_string(status);
    	fprintf(stderr, "Error: could not destroy rocblas handle: %s\n", err);
    	return -1;
    }

    // SUCCESS
    return 0;
}