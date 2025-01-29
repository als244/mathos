#include "backend_funcs.h"

Backend_Funcs * init_backend_funcs(Memory * memory){

	CUresult result;
	const char * err;

	Backend_Funcs * backend_funcs = malloc(sizeof(Backend_Funcs));
	if (!backend_funcs){
		fprintf(stderr, "Error: unable to init backend_functions\n");
		return NULL;
	}

	memset(backend_funcs, 0, sizeof(Backend_Funcs));

	backend_funcs -> memory = memory;


	// 1.) Load in function kernels

	const char * my_module_filename = "cuda_kernels.cubin";

	result = cuModuleLoad(&(backend_funcs -> module), my_module_filename);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not load module from file: %s\n", err);
		return NULL;
	}

	CUmodule my_module = backend_funcs -> module;

	unsigned int func_count;
	result = cuModuleGetFunctionCount(&func_count, my_module);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get get function count: %s\n", err);
		return NULL;
	}

	if (TO_PRINT_FOUND_FUNCTIONS){
		printf("Found %u functions!\n", func_count);
	}

	CUfunction all_functions[func_count];

	result = cuModuleEnumerateFunctions(all_functions, func_count, my_module);
	if (result != CUDA_SUCCESS){
		fprintf(stderr, "Error: unable to enumerate all functions\n");
		return NULL;
	}

	char* funcName;

	if (TO_PRINT_FOUND_FUNCTIONS){
		for (int i = 0; i < func_count; i++){
			funcName = *(char**)((uintptr_t)all_functions[i] + 8);
			printf("Found function: %s\n", funcName);
		}
	}


	// WAS GETTING "named symbol not found errors" after the add function doing it this "correct" way....

	// 1.) Add 
	result = cuModuleGetFunction(&(backend_funcs -> add_fp16), my_module, "add_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get add function from module: %s\n", err);
		return NULL;
	}

	result = cuModuleGetFunction(&(backend_funcs -> transpose_fp16), my_module, "transpose_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get transpose function from module: %s\n", err);
		return NULL;
	}

	
	// 2.) Matmul
	result = cuModuleGetFunction(&(backend_funcs -> matmul_fp16), my_module, "matmul_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get matmul function from module: %s\n", err);
		return NULL;
	}

	result = cuModuleGetFunction(&(backend_funcs -> naive_matmul_fp16), my_module, "naive_matmul_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get matmul function from module: %s\n", err);
		return NULL;
	}


	// 3.) RMS norm
	result = cuModuleGetFunction(&(backend_funcs -> rms_norm_fp16), my_module, "rms_norm_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get rms norm function from module: %s\n", err);
		return NULL;
	}


	// 4.) rope
	result = cuModuleGetFunction(&(backend_funcs -> rope_fp16), my_module, "rope_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get rope function from module: %s\n", err);
		return NULL;
	}

	// 5.) copying keys + values to cache
	result = cuModuleGetFunction(&(backend_funcs -> copy_kv_to_seq_context_fp16), my_module, "copy_kv_to_seq_context_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get copy values to cache function from module: %s\n", err);
		return NULL;
	}


	// 6.) Attention

	// changed this to be seqeuence function
	// was having issues with dynamic paralleism compilaition/linking and not finding symbols...
	result = cuModuleGetFunction(&(backend_funcs -> attention_fp16), my_module, "attention_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get attn function from module: %s\n", err);
		return NULL;
	}


	// 7.) Silu + Hardamard for FFN
	result = cuModuleGetFunction(&(backend_funcs -> silu_hadamard_fp16), my_module, "silu_hadamard_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get ffn silu hadamard function from module: %s\n", err);
		return NULL;
	}

	// 8.) Condense rows (used in output layer to only produce logits for tokens we want to predict
	result = cuModuleGetFunction(&(backend_funcs -> condense_rows_fp16), my_module, "condense_rows_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get condense rows function from module: %s\n", err);
		return NULL;
	}

	result = cuModuleGetFunction(&(backend_funcs -> softmax_fp16), my_module, "softmax_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get softmax function from module: %s\n", err);
		return NULL;
	}

	result = cuModuleGetFunction(&(backend_funcs -> softmax_fp16_to_float), my_module, "softmax_fp16_to_float_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get softmax function from module: %s\n", err);
		return NULL;
	}


	result = cuModuleGetFunction(&(backend_funcs -> cross_entropy_loss_fp16), my_module, "cross_entropy_loss_fp16_kernel");
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get cross entropy loss function from module: %s\n", err);
		return NULL;
	}




	// CHANGING/SAVING FUNCTION ATTRIBUTES!
	//	- helps ensure correct launch configuration for attention kernel


	

	
	// assuming all devices are equal for now...

	result = cuDeviceGetAttribute(&(backend_funcs -> dev_max_smem), CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN, 0);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get max shared memory per block (opt-in): %s\n", err);
		return NULL;
	}

	// Ref: https://github.com/Bruce-Lee-LY/cuda_hgemm/blob/master/src/mma/mma_async_stage4.cu
	backend_funcs -> matmul_fp16_smem = MY_MAX((256 + 128) * 32 * 2 * 4, 256 * 128 * 2);
	result = cuFuncSetAttribute(backend_funcs -> matmul_fp16, CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES, backend_funcs -> matmul_fp16_smem);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not set matmul fp16 kernel's max smem size to %d: %s\n", backend_funcs -> matmul_fp16_smem, err);
		return NULL;
	}

	/*
	result = cuDeviceGetAttribute(&(backend_funcs -> max_smem), CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_MULTIPROCESSOR, 0);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get max shared memory per multiprocessor: %s\n", err);
		return NULL;
	}
	*/


	// RMS norm 
	//	- (doesn't impact launch config which is per-token basis)
	//	- should be easily enough memory unless 405B...
	
	/*
	int rms_static_smem;
	result = cuFuncGetAttribute(&rms_static_smem CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, backend_funcs -> rms_norm_fp16);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: could not get static smem from rms kernel: %s\n", err);
		return NULL;
	}
	*/

	int rms_static_smem;
	result = cuFuncGetAttribute(&rms_static_smem, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, backend_funcs -> rms_norm_fp16);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get static smem as attribute for rms kernel: %s\n", err);
		return NULL;
	}

	// RMS Norm needs 4 * token_dim bytes of smem which should be fine. Going to set to 64k but might use less
	backend_funcs -> rms_norm_smem = (backend_funcs -> dev_max_smem - rms_static_smem) - (1U << 11);
	result = cuFuncSetAttribute(backend_funcs -> rms_norm_fp16, CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES, backend_funcs -> rms_norm_smem);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not set rms norm's kernel's max smem size to %d: %s\n", backend_funcs -> rms_norm_smem, err);
		return NULL;
	}

	result = cuFuncGetAttribute(&(backend_funcs -> rms_norm_max_threads_per_block), CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, backend_funcs -> rms_norm_fp16);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get max threads per block attribute for rms norm kernel: %s\n", err);
		return NULL;
	}
	


	// CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES + CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES <= CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN

	int attn_static_smem;
	result = cuFuncGetAttribute(&attn_static_smem, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, backend_funcs -> attention_fp16);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get static smem as attribute for attention kernel: %s\n", err);
		return NULL;
	}


	// problematic when all smem is used, some should be reserved to attain higher occupancy
	// can look at ncu to see this...
	backend_funcs -> attn_max_smem = (backend_funcs -> dev_max_smem - attn_static_smem) - (1U << 11);
	result = cuFuncSetAttribute(backend_funcs -> attention_fp16, CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES, backend_funcs -> attn_max_smem);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not set attention kernel's max smem size to %d: %s\n", backend_funcs -> attn_max_smem, err);
		return NULL;
	}

	int max_threads;
	result = cuFuncGetAttribute(&(backend_funcs -> attn_max_threads_per_block), CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, backend_funcs -> attention_fp16);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get max threads per block attribute for attention kernel: %s\n", err);
		return NULL;
	}

	// HARDCODING:

	// assert attn_waprs_per_block * 32 < attn_max_threads_per_block
	backend_funcs -> attn_warps_per_block = (backend_funcs -> attn_max_threads_per_block) / 32;


	// This doesn't seem to matter...

	/*
	result = cuFuncSetCacheConfig(backend_funcs -> attention_fp16, CU_FUNC_CACHE_PREFER_SHARED);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not set shared mem carveout to 90 as attribute for attention kernel: %s\n", err);
		return NULL;
	}
	
	// Specify that we prefer shared memory over L1 cache
	int smem_carveout = 80;
	result = cuFuncSetAttribute(backend_funcs -> attention_fp16, CU_FUNC_ATTRIBUTE_PREFERRED_SHARED_MEMORY_CARVEOUT, smem_carveout);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not set shared mem carveout to 90 as attribute for attention kernel: %s\n", err);
		return NULL;
	}
	*/
	


	return backend_funcs;
}

