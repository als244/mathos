#include "cublas_helper.h"

int dtype_to_cuda_dtype(DataType dtype, cudaDataType * ret_dtype){

	switch(dtype){
		case FP16:
			*ret_dtype = CUDA_R_16F;
			break;
		case FP32:
			*ret_dtype = CUDA_R_32F;
			break;
		default:
			printf("Error: unsupported dtype to convert to cuda\n");
			return -1;
	}

	return 0;
}

int create_cublas_matmul_descriptor(cublasLtHandle_t handle, Cublas_Matmul_Desc  * desc, int M, int K, int N, DataType A_dt, DataType B_dt, DataType C_dt,
									bool is_a_row_major, bool is_b_row_major, bool is_c_row_major, bool use_fp32_accum, uint64_t workspace_bytes) {

	int ret;


	// deal with cuBLAS structs
	cublasStatus_t status;

	desc -> M = M;
	desc -> K = K;
	desc -> N = N;

	cudaDataType A_cuda_dt;
	cudaDataType B_cuda_dt;
	cudaDataType C_cuda_dt;

	ret = dtype_to_cuda_dtype(A_dt, &A_cuda_dt);
	if (ret){
		fprintf(stderr, "Error: unsupported dt for cublaslt: %d\n", A_dt);
		return -1;
	}

	ret = dtype_to_cuda_dtype(B_dt, &B_cuda_dt);
	if (ret){
		fprintf(stderr, "Error: unsupported dt for cublaslt: %d\n", B_dt);
		return -1;
	}

	ret = dtype_to_cuda_dtype(C_dt, &C_cuda_dt);
	if (ret){
		fprintf(stderr, "Error: unsupported dt for cublaslt: %d\n", C_dt);
		return -1;
	}

	cudaDataType scale_type; 

	cublasComputeType_t compute_type;
	

	// REF: https://docs.nvidia.com/cuda/cublas/#cublaslt-datatypes-reference
	if ((A_cuda_dt == CUDA_R_16F) && (B_cuda_dt == CUDA_R_16F) && (C_cuda_dt == CUDA_R_16F) && (!use_fp32_accum)) {
		compute_type = CUBLAS_COMPUTE_16F;
		scale_type = CUDA_R_16F;
	}
	else{
		compute_type = CUBLAS_COMPUTE_32F;
		scale_type = CUDA_R_32F;
	}

	desc -> compute_type = compute_type;
	desc -> scale_type = scale_type;

	status = cublasLtMatmulDescCreate(&(desc -> matmul_desc), compute_type, scale_type);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul desc could not be created\n");
		return -1;
	}

	cublasLtPointerMode_t host_pointer_mode = CUBLASLT_POINTER_MODE_HOST;

	// ASSUMING THAT THE SCALARS WILL BE IN HOST MEMORY (normally using 0/1 constants and either half or float)
	status = cublasLtMatmulDescSetAttribute(desc -> matmul_desc, CUBLASLT_MATMUL_DESC_POINTER_MODE, &host_pointer_mode, sizeof(cublasLtPointerMode_t));
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul desc attribute could not be set for pointer mode\n");
		return -1;
	}


	// ASSUMING BOTH A and B are passed in row-major format!!
	// We need to specify thsi
	cublasLtOrder_t row_major = CUBLASLT_ORDER_ROW;
	cublasLtOrder_t col_major = CUBLASLT_ORDER_COL;

	cublasLtOrder_t a_layout;
	cublasLtOrder_t b_layout;
	cublasLtOrder_t c_layout;

	int lda;
	int ldb;
	int ldc;

	if (is_a_row_major){
		a_layout = row_major;
		lda = K;
	}
	else{
		a_layout = col_major;
		lda = M;
	}

	if (is_b_row_major){
		b_layout = row_major;
		ldb = N;
	}
	else{
		b_layout = col_major;
		ldb = K;
	}

	if (is_c_row_major){
		c_layout = row_major;
		ldc = N;
	}
	else{
		c_layout = col_major;
		ldc = M;
	}



	// FOR SIMPLICITY ASSUMING ALL MATRICES ARE STORED
	// IN NORMAL FORMAT

	cublasOperation_t transa = CUBLAS_OP_N;
	cublasOperation_t transb = CUBLAS_OP_N;

	status = cublasLtMatmulDescSetAttribute(desc -> matmul_desc, CUBLASLT_MATMUL_DESC_TRANSA, &transa, sizeof(transa));
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul desc attribute could not be set\n");
		return -1;
	}
	status = cublasLtMatmulDescSetAttribute(desc -> matmul_desc, CUBLASLT_MATMUL_DESC_TRANSB, &transb, sizeof(transb));
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul desc attribute could not be set\n");
		return -1;
	}


	// need to speci

	// A Transposed (from row-major to column-major), not B/D (but still held in col-major format internally)
	// m and k must be multiples of 4, perferablly multiples of 16
	status = cublasLtMatrixLayoutCreate(&(desc -> Adesc), A_cuda_dt, M, K, lda);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul layout could not be created\n");
		return -1;
	}

	
	status = cublasLtMatrixLayoutSetAttribute(desc -> Adesc, CUBLASLT_MATRIX_LAYOUT_ORDER, &a_layout, sizeof(cublasLtOrder_t));
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not set row major order attribute\n");
		return -1;
	}


	status = cublasLtMatrixLayoutCreate(&(desc -> Bdesc), B_cuda_dt, K, N, ldb);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul layout could not be created\n");
		return -1;
	}

	status = cublasLtMatrixLayoutSetAttribute(desc -> Bdesc, CUBLASLT_MATRIX_LAYOUT_ORDER, &b_layout, sizeof(cublasLtOrder_t));
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not set row major order attribute\n");
		return -1;
	}


	status = cublasLtMatrixLayoutCreate(&(desc -> Cdesc), C_cuda_dt, M, N, ldc);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul layout could not be created\n");
		return -1;
	}


	status = cublasLtMatrixLayoutSetAttribute(desc -> Cdesc, CUBLASLT_MATRIX_LAYOUT_ORDER, &c_layout, sizeof(cublasLtOrder_t));
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not set row major order attribute\n");
		return -1;
	}

	status = cublasLtMatrixLayoutCreate(&(desc -> Ddesc), C_cuda_dt, M, N, ldc);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul layout could not be created\n");
		return -1;
	}

	status = cublasLtMatrixLayoutSetAttribute(desc -> Ddesc, CUBLASLT_MATRIX_LAYOUT_ORDER, &c_layout, sizeof(cublasLtOrder_t));
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not set row major order attribute\n");
		return -1;
	}


	cublasLtMatmulPreference_t pref;
	status = cublasLtMatmulPreferenceCreate(&(desc -> pref));
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul pref could not be created\n");
		return -1;
	}
	// Allowing just a small amount of workspace mem (2 MB) makes a big difference
	status = cublasLtMatmulPreferenceSetAttribute(desc -> pref, CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES, &workspace_bytes, sizeof(workspace_bytes));
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: matmul pref attribute could not be set\n");
		return -1;
	}

	cublasLtMatmulAlgo_t algo;
		
	int algoCount = 1;
	int retAlgoCount = 0;

	cublasLtMatmulHeuristicResult_t heuristicResultsArray = {};

	status = cublasLtMatmulAlgoGetHeuristic(handle, desc -> matmul_desc, desc -> Adesc, desc -> Bdesc, desc -> Cdesc, desc -> Ddesc, desc -> pref, algoCount, &heuristicResultsArray, &retAlgoCount);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not get matmul algo heuristic: %s\n", cublasLtGetStatusString(status));
		return -1;
	}

	memcpy(&(desc -> algo), &(heuristicResultsArray.algo), sizeof(cublasLtMatmulAlgo_t));

	return 0;
}


int destroy_cublas_matmul_descriptor(Cublas_Matmul_Desc * desc){

	// deal with cuBLAS structs
	cublasStatus_t status;

	status = cublasLtMatmulPreferenceDestroy(desc -> pref);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not destroy matmul pref\n");
		return -1;
	}
	status = cublasLtMatmulDescDestroy(desc -> matmul_desc);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not destroy matmul desc\n");
		return -1;
	}
	status = cublasLtMatrixLayoutDestroy(desc -> Adesc);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not destroy matmul layout\n");
		return -1;
	}
	status = cublasLtMatrixLayoutDestroy(desc -> Bdesc);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not destroy matmul layout\n");
		return -1;
	}
	status = cublasLtMatrixLayoutDestroy(desc -> Cdesc);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not destroy matmul layout\n");
		return -1;
	}
	status = cublasLtMatrixLayoutDestroy(desc -> Ddesc);
	if (status != CUBLAS_STATUS_SUCCESS) {
		fprintf(stderr, "Error: could not destroy matmul layout\n");
		return -1;
	}

	return 0;



}


int do_cublas_matmul_fp16(CUstream compute_stream, cublasLtHandle_t handle, void * workspace, uint64_t workspace_bytes, int M, int K, int N, 
							void * alpha, void * A, bool is_a_row_major, void * B, bool is_b_row_major, void * beta, void * C, bool is_c_row_major,
							bool use_fp32_accum, Cublas_Matmul_Desc * supplied_desc, Cublas_Matmul_Desc * save_desc)  {


	CUresult result;
	const char * err;

	int ret;

	// deal with cuBLAS structs
	cublasStatus_t status;

	Cublas_Matmul_Desc * desc;

	if (supplied_desc){
		desc = supplied_desc;
	}
	else{
		Cublas_Matmul_Desc new_desc;
		ret = create_cublas_matmul_descriptor(handle, &new_desc, M, K, N, FP16, FP16, FP16, is_a_row_major, is_b_row_major, is_c_row_major, use_fp32_accum, workspace_bytes);
		if (ret){
			fprintf(stderr, "Error: was unable to create matmul descriptor\n");
			return -1;
		}

		desc = &new_desc;

		if (save_desc){
			memcpy(save_desc, desc, sizeof(Cublas_Matmul_Desc));
		}
	}

	status = cublasLtMatmul(handle,
							desc -> matmul_desc,
							alpha,
							A,
							desc -> Adesc,
							B,
							desc -> Bdesc,
							beta,
							C,
							desc -> Cdesc,
							C,
							desc -> Ddesc,
							&(desc -> algo),
							workspace,
							workspace_bytes,
							compute_stream);


	if (status != CUBLAS_STATUS_SUCCESS){
		fprintf(stderr, "Error: cublasLtMatmul failed: %s\n", cublasLtGetStatusString(status));
		return -1;
	}

	if (!supplied_desc && !save_desc){
		ret = destroy_cublas_matmul_descriptor(desc);
		if (ret){
			fprintf(stderr, "Error: could not destory cublas matmul descriptor\n");
			return -1;
		}
	}

	return 0;
}

