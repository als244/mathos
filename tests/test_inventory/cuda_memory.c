#include "cuda_memory.h"


int cuda_init_driver(unsigned int init_flags){

	CUresult result;
	const char * err;

    result = cuInit(init_flags);

    if (result != CUDA_SUCCESS){
    	cuGetErrorString(result, &err);
    	fprintf(stderr, "Could not init cuda: %s\n", err);
    	return -1;
    }

    return 0;

}

int create_context(int device_id, CUcontext * ret_ctx){

	CUresult result;
	const char * err;

	CUdevice dev;

	result = cuDeviceGet(&dev, device_id);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
    	fprintf(stderr, "Error: Could not get device: %s\n", err);
    	return -1;
	}

	// Set the host thread to spin waiting for completetion from GPU
	unsigned int ctx_flags = CU_CTX_SCHED_SPIN;

	CUcontext ctx;
	result = cuCtxCreate(&ctx, ctx_flags, dev);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
    	fprintf(stderr, "Error: Could not create context: %s\n", err);
    	return -1;
	}

	*ret_ctx = ctx;

	// SUCCESS!
	return 0;
}



int create_context_sm_count(int device_id, unsigned int sm_count, CUcontext * ret_ctx){

	CUresult result;
	const char * err;

	CUdevice dev;

	result = cuDeviceGet(&dev, device_id);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
    	fprintf(stderr, "Error: Could not get device: %s\n", err);
    	return -1;
	}


	// ASSUMES CUDA_MPS_ENABLE_PER_CTX_DEVICE_MULTIPROCESSOR_PARTITIONING=1 always
	// & this process was started with CUDA_MPS_ACTIVE_THREAD_PERCENTAGE=pct_val
	
	// int avail_sm_count;

	// result = cuDeviceGetAttribute(&avail_sm_count, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, dev);
	// if (result != CUDA_SUCCESS){
	// 	cuGetErrorString(result, &err);
    // 	fprintf(stderr, "Error: Could not get available sm count: %s\n", err);
    // 	exit(1);
	// }

	// if (TO_PRINT) {
	// 	printf("Available SM Count: %d\n", avail_sm_count);
	// }

	// CREATING A CONTEXT WITH MINIMIUM SM_COUNT
	// 	- still need to set CUDA_MPS_ENABLE_PER_CTX_DEVICE_MULTIPROCESSOR_PARTITIONING=1 
	// 	- using this context creating with CuexecAffinityParam instead of setting env. CUDA_MPS_ACTIVE_THREAD_PERCENTAGE

	CUexecAffinityParam affinity;
	affinity.type = CU_EXEC_AFFINITY_TYPE_SM_COUNT;
	affinity.param.smCount.val = sm_count;

	// Set the host thread to spin waiting for completetion from GPU
	unsigned int ctx_flags = CU_CTX_SCHED_SPIN;

	CUcontext ctx;
	result = cuCtxCreate_v3(&ctx, &affinity, 1, ctx_flags, dev);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
    	fprintf(stderr, "Error: Could not create context: %s\n", err);
    	return -1;
	}

	*ret_ctx = ctx;

	// SUCCESS!
	return 0;
}

int push_context(CUcontext ctx){

	CUresult result;
	const char * err;

	result = cuCtxPushCurrent(ctx);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
    	fprintf(stderr, "Error: Could not set context: %s\n", err);
    	return -1;
	}

	return 0;
}

int pop_current_context(CUcontext * ret_ctx) {

	CUresult result;
	const char * err;

	CUcontext ctx;
	result = cuCtxPopCurrent(&ctx);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
    	fprintf(stderr, "Error: Could not remove context: %s\n", err);
    	return -1;
	}

	if (ret_ctx != NULL){
		*ret_ctx = ctx;
	}

	return 0;
}

Cuda_User_Page_Table * cuda_init_user_page_table(int num_devices){

	Cuda_User_Page_Table * cuda_user_page_table = (Cuda_User_Page_Table *) malloc(sizeof(Cuda_User_Page_Table));
	if (!cuda_user_page_table){
		fprintf(stderr, "Error: malloc failed to allocate cuda_user_page_table container\n");
		return NULL;
	}

	cuda_user_page_table -> num_devices = num_devices;

	cuda_user_page_table -> num_chunks = (uint64_t *) calloc(num_devices, sizeof(uint64_t));
	cuda_user_page_table -> chunk_size = (uint64_t *) calloc(num_devices, sizeof(uint64_t));
	cuda_user_page_table -> virt_memories = (void **) calloc(num_devices, sizeof(void *));

	if ((!cuda_user_page_table -> num_chunks) || (!cuda_user_page_table -> chunk_size)
			|| (!cuda_user_page_table -> virt_memories)){
		fprintf(stderr, "Error: malloc failed to allocate containers within cuda_user_page_table\n");
		return NULL;
	}

	return cuda_user_page_table;
}


Cuda_Memory * cuda_init_memory() {

	CUresult result;
	const char * err;

	Cuda_Memory * cuda_memory = (Cuda_Memory *) malloc(sizeof(Cuda_Memory));
	if (!cuda_memory){
		fprintf(stderr, "Error: malloc failed to allocate cuda_memory struct\n");
		return NULL;
	}

	int ret = cuda_init_driver(0);
	if (ret){
		fprintf(stderr, "Error: unable to init cuda driver\n");
		return NULL;
	}

	int dev_count;
	result = cuDeviceGetCount(&dev_count);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get number of devices: %s\n", err);
		return NULL;
	}


	cuda_memory -> num_devices = dev_count;

	CUcontext * contexts = malloc(dev_count * sizeof(CUcontext));
	if (!contexts){
		fprintf(stderr, "Error: malloc failed to allocate container for cucontexts\n");
		return NULL;
	}

	char context_name[256];
	char dev_name[256];
	CUdevice cur_dev;
	for (int i = 0; i < dev_count; i++){
		ret = create_context(i, &(contexts[i]));
		if (ret){
			fprintf(stderr, "Error: unable to create context for device: %d\n", i);
			return NULL;
		}
		sprintf(context_name, "Default Context: Device %d", i);
		profile_name_context(&(contexts[i]), context_name);

		// get device for convenience of naming
		cuDeviceGet(&cur_dev, i);
		sprintf(dev_name, "Device %d", i);
		profile_name_device(&cur_dev, dev_name);

	}

	cuda_memory -> contexts = contexts;

	for (int i = 0; i < dev_count; i++){
		push_context(contexts[i]);
		for (int j = 0; j < dev_count; j++){
			if (i != j){
				result = cuCtxEnablePeerAccess(contexts[j], 0);
			}
		}
		pop_current_context(NULL);
	}

	cuda_memory -> user_page_table = cuda_init_user_page_table(dev_count);
	if (!(cuda_memory -> user_page_table)){
		fprintf(stderr, "Error: unable to initialize user page table\n");
		return NULL;
	}

	cuda_memory -> inbound_streams = malloc(dev_count * sizeof(CUstream));
	if (!(cuda_memory -> inbound_streams)){
		fprintf(stderr, "Error: unable to allocate container for inbound streams\n");
		return NULL;
	}

	cuda_memory -> outbound_streams = malloc(dev_count * sizeof(CUstream));
	if (!(cuda_memory -> outbound_streams)){
		fprintf(stderr, "Error: unable to allocate container for outbound streams\n");
		return NULL;
	}

	char stream_name[256];
	for (int i = 0; i < dev_count; i++){
		push_context(contexts[i]);
		result = cuStreamCreate(&((cuda_memory -> inbound_streams)[i]), CU_STREAM_NON_BLOCKING);
		if (result != CUDA_SUCCESS){
			fprintf(stderr, "Error: unable to create inbound stream for dev %d\n", i);
			return NULL;
		}
		sprintf(stream_name, "Inbound Transfer (Device %d)", i);
		profile_name_stream(&((cuda_memory -> inbound_streams)[i]), stream_name);


		result = cuStreamCreate(&((cuda_memory -> outbound_streams)[i]), CU_STREAM_NON_BLOCKING);
		if (result != CUDA_SUCCESS){
			fprintf(stderr, "Error: unable to create outbound stream for dev %d\n", i);
			return NULL;
		}
		sprintf(stream_name, "Outbound Transfer (Device %d)", i);
		profile_name_stream(&((cuda_memory -> outbound_streams)[i]), stream_name);


		pop_current_context(NULL);
	}



	return cuda_memory;
}



// This should only be called on GPUs (i.e. agend_ids > 0)
int cuda_add_device_memory(Cuda_Memory * cuda_memory, int device_id, uint64_t num_chunks, uint64_t chunk_size) {

	CUresult result;
	const char * err;

	int ret;

	

	Cuda_User_Page_Table * cuda_user_page_table = cuda_memory -> user_page_table;

	(cuda_user_page_table -> num_chunks)[device_id] = num_chunks;
	(cuda_user_page_table -> chunk_size)[device_id] = chunk_size;

	// 1.) Allocate device memory

	CUcontext dev_ctx = (cuda_memory -> contexts)[device_id];

	ret = push_context(dev_ctx);
	if (ret){
		fprintf(stderr, "Error: unable to push context for device %d\n", device_id);
		return -1;
	}

	uint64_t total_dev_memory = num_chunks * chunk_size;
	printf("Allocating Device Mempool...\n\tPool ID: %d\n\tNum Chunks %lu\n\tChunk Size: %lu\n\tTotal Dev Memory: %lu\n",
				device_id, num_chunks, chunk_size, total_dev_memory);


	// MUST BE A MULTIPLE OF ALLOC SIZE (either 4KB, 64KB, or 2MB)!!
	uint64_t total_mem_size_bytes = num_chunks * chunk_size;

	void * device_memory;

	result = cuMemAlloc((CUdeviceptr *) &device_memory, total_mem_size_bytes);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not do cudaMemAlloc for device %d with total size %lu: %s\n", device_id, total_mem_size_bytes, err);
		return -1;
	}

	(cuda_user_page_table -> virt_memories)[device_id] = device_memory;

	pop_current_context(NULL);

	return 0;

}

int cuda_copy_to_host_memory(Cuda_Memory * cuda_memory, int src_device_id, void * src_addr, uint64_t length, void * ret_contents) {


	CUresult result;
	const char * err;

	CUcontext dev_ctx = (cuda_memory -> contexts)[src_device_id];

	int ret = push_context(dev_ctx);
	if (ret){
		fprintf(stderr, "Error: unable to push context for device %d\n", src_device_id);
		return -1;
	}

	result = cuMemcpyDtoH(ret_contents, (CUdeviceptr) src_addr, length);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not do cudaMemCpyDtoH from device %d with total size %lu: %s\n", src_device_id, length, err);
		return -1;
	}

	pop_current_context(NULL);

	return 0;
}

int cuda_async_copy_to_host_memory(Cuda_Memory * cuda_memory, int src_device_id, void * src_addr, uint64_t length, void * ret_contents, void * stream_ref) {


	CUresult result;
	const char * err;

	CUcontext dev_ctx = (cuda_memory -> contexts)[src_device_id];

	int ret = push_context(dev_ctx);
	if (ret){
		fprintf(stderr, "Error: unable to push context for device %d\n", src_device_id);
		return -1;
	}

	CUstream outbound_stream;

	if (stream_ref){
		outbound_stream = *((CUstream *) stream_ref);
	}
	// use default outbound stream
	else{
		outbound_stream = (cuda_memory -> outbound_streams)[src_device_id];
	}
	result = cuMemcpyDtoHAsync(ret_contents, (CUdeviceptr) src_addr, length, outbound_stream);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not do cudaMemcpyDtoH from device %d with total size %lu: %s\n", src_device_id, length, err);
		return -1;
	}

	pop_current_context(NULL);

	return 0;
}


int cuda_copy_to_device_memory(Cuda_Memory * cuda_memory, void * contents, int dest_device_id, void * dest_addr, uint64_t length) {


	CUresult result;
	const char * err;

	CUcontext dev_ctx = (cuda_memory -> contexts)[dest_device_id];

	int ret = push_context(dev_ctx);
	if (ret){
		fprintf(stderr, "Error: unable to push context for device %d\n", dest_device_id);
		return -1;
	}

	result = cuMemcpyHtoD((CUdeviceptr) dest_addr, contents, length);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not do cudaMemcpyHtoD to device %d with total size %lu: %s\n", dest_device_id, length, err);
		return -1;
	}

	pop_current_context(NULL);

	return 0;
}

int cuda_async_copy_to_device_memory(Cuda_Memory * cuda_memory, void * contents, int dest_device_id, void * dest_addr, uint64_t length, void * stream_ref) {


	CUresult result;
	const char * err;

	CUcontext dev_ctx = (cuda_memory -> contexts)[dest_device_id];

	int ret = push_context(dev_ctx);
	if (ret){
		fprintf(stderr, "Error: unable to push context for device %d\n", dest_device_id);
		return -1;
	}

	CUstream inbound_stream;
	if (stream_ref){
		inbound_stream = *((CUstream *) stream_ref);
	}
	// use default outbound stream
	else{
		inbound_stream = (cuda_memory -> inbound_streams)[dest_device_id];
	}
	result = cuMemcpyHtoDAsync((CUdeviceptr) dest_addr, contents, length, inbound_stream);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not do cudaMemcpyHtoD from device %d with total size %lu: %s\n", dest_device_id, length, err);
		return -1;
	}

	pop_current_context(NULL);

	return 0;
}

int cuda_async_dev_copy_to_device_memory(Cuda_Memory * cuda_memory, void * contents, int dest_device_id, void * dest_addr, uint64_t length, void * stream_ref) {


	CUresult result;
	const char * err;

	CUcontext dev_ctx = (cuda_memory -> contexts)[dest_device_id];

	int ret = push_context(dev_ctx);
	if (ret){
		fprintf(stderr, "Error: unable to push context for device %d\n", dest_device_id);
		return -1;
	}

	CUstream inbound_stream;
	if (stream_ref){
		inbound_stream = *((CUstream *) stream_ref);
	}
	// use default outbound stream
	else{
		inbound_stream = (cuda_memory -> inbound_streams)[dest_device_id];
	}
	result = cuMemcpyDtoDAsync((CUdeviceptr) dest_addr, (CUdeviceptr) contents, length, inbound_stream);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not do cudaMemcpyDtoD from device %d with total size %lu: %s\n", dest_device_id, length, err);
		return -1;
	}

	pop_current_context(NULL);

	return 0;
}



void CUDA_CB postSemCallback(CUstream stream, CUresult status, void * data) {

	sem_t * sem_to_post = (sem_t *) data;

	int ret = sem_post(sem_to_post);
	if (ret){
		fprintf(stderr, "Error: failed to post to semaphore\n");
	}
}


int add_callback_to_post_complete(Cuda_Memory * cuda_memory, int device_id, StreamDirection stream_direction, CUstream custom_stream, sem_t * sem_to_post){

	CUresult result;
	const char * err;

	CUstream stream;
	switch (stream_direction){
		case STREAM_INBOUND:
			stream = (cuda_memory -> inbound_streams)[device_id];
			break;
		case STREAM_OUTBOUND:
			stream = (cuda_memory -> outbound_streams)[device_id];
			break;
		case STREAM_CUSTOM:
			stream = custom_stream;
			break;
		default:
			fprintf(stderr, "Error: unkown stream direction: %d\n", stream_direction);
			return -1;
	}
	

	result = cuStreamAddCallback(stream, postSemCallback, (void *) sem_to_post, 0);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to add callback to post to sem. Err: %s\n", err);
		return -1;
	}

	return 0;
}



int cuda_register_sys_mem(Memory * memory){

	CUresult result;
	const char * err;

	Cuda_Memory * cuda_memory = (Cuda_Memory *) memory -> backend_memory;

	CUcontext dev_ctx = (cuda_memory -> contexts)[0];

	int ret = push_context(dev_ctx);
	if (ret){
		fprintf(stderr, "Error: unable to push context for device 0 when registering sys mem\n");
		return -1;
	}

	void * sys_mem_buffer = (void *) ((memory -> system_mempool).va_start_addr);
	uint64_t sys_mem_buffer_size = (memory -> system_mempool).capacity_bytes;

	// setting CU_MEMHOSTREGISTER_PORTABLE makes this memory pinned from the viewpoint of all cuda contexts
	// we already initialized this memory with MAP_POPULATE and called mlock() on it so we know it is truly pinned
	// but the cuda driver also needs to do it's page locking (under the hood it calls MAP_FIXED)
	result = cuMemHostRegister(sys_mem_buffer, sys_mem_buffer_size, CU_MEMHOSTREGISTER_PORTABLE);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to regsiter system memory buffer with cuda. Err: %s\n", err);
		return -1;
	}


	pop_current_context(NULL);

	return 0;
}


// Can cause system glitchiness when exiting without unmapping
int unregister_backend_sys_mapping(Memory * memory){

	CUresult result;
	const char * err;

	Cuda_Memory * cuda_memory = (Cuda_Memory *) memory -> backend_memory;

	CUcontext dev_ctx = (cuda_memory -> contexts)[0];

	int ret = push_context(dev_ctx);
	if (ret){
		fprintf(stderr, "Error: unable to push context for device 0 when unregistering sys mem\n");
		return -1;
	}

	void * sys_mem_buffer = (void *) ((memory -> system_mempool).va_start_addr);

	result = cuMemHostUnregister(sys_mem_buffer);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to unregister system memory buffer with cuda. Err: %s\n", err);
		return -1;
	}


	pop_current_context(NULL);

	return 0;

}



int init_backend_memory(Memory * memory, uint64_t dev_num_chunks, uint64_t dev_chunk_size) {

	int ret;

	Cuda_Memory * cuda_memory = cuda_init_memory();
	if (!cuda_memory){
		fprintf(stderr, "Error: failed to init cuda memory\n");
		return -1;
	}

	int dev_cnt = cuda_memory -> num_devices;
	for (int i = 0; i < dev_cnt; i++){
		ret = cuda_add_device_memory(cuda_memory, i, dev_num_chunks, dev_chunk_size);
		if (ret){
			fprintf(stderr, "Error: unable to add device memory (%lu chunks, %lu chunk size) for dev #%d\n", dev_num_chunks, dev_chunk_size, i);
			return -1;
		}
	}

	memory -> backend_memory = cuda_memory;

	Cuda_User_Page_Table * cuda_user_page_table = cuda_memory -> user_page_table;

	int num_devices = cuda_user_page_table -> num_devices;

	memory -> num_devices = num_devices;

	Mempool * mempools = (Mempool *) malloc(num_devices * sizeof(Mempool));
	if (mempools == NULL){
		fprintf(stderr, "Error: malloc faile dto allocate mempools container\n");
		return -1;
	}


	uint64_t num_chunks;
	uint64_t chunk_size;
	void * backing_memory;
	for (int i = 0; i < num_devices; i++){
		num_chunks = (cuda_user_page_table -> num_chunks)[i];
		chunk_size = (cuda_user_page_table -> chunk_size)[i];
		backing_memory = ((cuda_user_page_table -> virt_memories)[i]);

		ret = init_mempool(&(mempools[i]), i, backing_memory, num_chunks, chunk_size);
		if (ret){
			fprintf(stderr, "Error: unable to initialize mempool for device #%d\n", i);
			return -1;
		}
	}


	memory -> device_mempools = mempools;

	// Need to page-lock the system memory from cuda prospective (it already is based on MAP_POPULATE + mlock() from original mmap allocation)

	ret = cuda_register_sys_mem(memory);
	if (ret){
		fprintf(stderr, "Error: was unable to register system memory with cuda\n");
		return -1;
	}

	return 0;
}
















uint64_t get_aligned_size(uint64_t content_size, uint64_t page_size){
	int remainder = content_size % page_size;
	if (remainder == 0)
		return content_size;

	return content_size + page_size - remainder;
}

int create_phys_mem(int device_id, uint64_t content_size, uint64_t page_size, CUmemGenericAllocationHandle * ret_phys_mem_handle){

	CUresult result;
	const char * err;

	CUmemGenericAllocationHandle generic_handle;
	CUmemAllocationProp prop = {};
	prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
	prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
	// set device in which we are allocating physical memory
	prop.location.id = device_id;
	prop.requestedHandleTypes = CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR;
	CUmemAllocationHandleType handle_type = CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR;
	
	// ensure to round up to mulitple of page size
	uint64_t aligned_size = get_aligned_size(content_size, page_size);

	result = cuMemCreate(&generic_handle, aligned_size, &prop, 0);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not get alloc handle: %s\n", err);
		// TODO:
		//	- more cleanup needed here...
		return -1;
	}

	*ret_phys_mem_handle = generic_handle;

	return 0;
}


int creating_mapping(CUmemGenericAllocationHandle phys_mem_handle, uint64_t content_size, uint64_t page_size, void ** ret_cuda_ptr){

	CUresult result;
	const char * err;

	CUdeviceptr cuda_ptr;

	// ensure to round up to mulitple of page size
	uint64_t aligned_size = get_aligned_size(content_size, page_size);

	// 1.) Reserve virtual address

	// pass in the cuda pointer we are supposed to return
	result = cuMemAddressReserve(&cuda_ptr, aligned_size, 0, 0, 0);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not allocate device range: %s\n", err);
		return -1;
	}

	// 2.) Perform mapping
	result = cuMemMap(cuda_ptr, aligned_size, 0, phys_mem_handle, 0);
	if (result != CUDA_SUCCESS){
			cuGetErrorString(result, &err);
			fprintf(stderr, "Error: Could not do memory mapping: %s\n", err);
			return -1;
	}

	*ret_cuda_ptr = (void *) cuda_ptr;

	return 0;


   
}

int set_mapped_mem_access(void * cuda_ptr, uint64_t content_size, uint64_t page_size, int num_devices, int * device_ids) {

	CUresult result;
	const char * err;

	 // 1.) Set access

	// every entry in accessDescriptors will refer to a device that can access the mapped memory referred to by cuda_ptr
	// need to include the device in which underlying phys memory is allocated as well
	CUmemAccessDesc * accessDescriptors = (CUmemAccessDesc *) malloc(num_devices * sizeof(CUmemAccessDesc));
	for (int i = 0; i < num_devices; i++){
		accessDescriptors[i].location.id = device_ids[i];
		accessDescriptors[i].location.type = CU_MEM_LOCATION_TYPE_DEVICE;
		accessDescriptors[i].flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
	}

	uint64_t aligned_size = get_aligned_size(content_size, page_size);

	result = cuMemSetAccess((CUdeviceptr) cuda_ptr, aligned_size, accessDescriptors, num_devices);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not set access: %s\n", err);
		return -1;
	}

	// driver has ingested this information now, so can release this array
	free(accessDescriptors);

	return 0;
}



void * device_malloc(int phys_mem_device_id, uint64_t content_size, uint64_t page_size, int num_devices_access, int * device_ids_access) {

	int ret;

	// 1.) Create physical memory on specific device

	CUmemGenericAllocationHandle phys_mem_handle;
	// populates phys_mem_handle
	ret = create_phys_mem(phys_mem_device_id, content_size, page_size, &phys_mem_handle);
	if (ret != 0){
		fprintf(stderr, "Error: could not create physical memory on device: %d\n", phys_mem_device_id);
		return NULL;
	}


	// 2.) Create virtual address and perform memory mapping
	void * cuda_ptr;
	ret = creating_mapping(phys_mem_handle, content_size, page_size, &cuda_ptr);
	if (ret != 0){
		fprintf(stderr, "Error: could not create memory mapping\n");
		return NULL;
	}


	// 3.) Enable devices to access this memory
	ret = set_mapped_mem_access(cuda_ptr, content_size, page_size, num_devices_access, device_ids_access);
	if (ret != 0){
		fprintf(stderr, "Error: could not set memory access\n");
		return NULL;
	}

	return cuda_ptr;
}



void * put_host_content_on_device(void * content, int phys_mem_device_id, uint64_t content_size, uint64_t page_size, int num_devices_access, int * device_ids_access){

	CUresult result;
	const char * err;

	// 1.) allocate memory on specific device, create mapping, and set access
	void * cuda_ptr = device_malloc(phys_mem_device_id, content_size, page_size, num_devices_access, device_ids_access);
	if (cuda_ptr == NULL){
		fprintf(stderr, "Error: device_malloc failed\n");
		return NULL;
	}


	// 2.) Perform memcpy
	result = cuMemcpyHtoD((CUdeviceptr) cuda_ptr, content, content_size);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not do mempcy from host to device: %s\n", err);
		return NULL;
	}

	return cuda_ptr;
}

void * get_device_content_on_host(void * cuda_ptr, uint64_t content_size) {

	CUresult result;
	const char * err;

	// 1.) Allocate memory on host 
	// (in reality, should really use memory from a pinned pool for perf.)
	void * content = malloc(content_size);

	// 2.) Perform device to host memcpy
	result = cuMemcpyDtoH(content, (CUdeviceptr) cuda_ptr, content_size);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: Could not do mempcy from device to host: %s\n", err);
		return NULL;
	}

	return content;
}