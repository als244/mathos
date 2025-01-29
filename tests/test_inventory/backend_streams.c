#include "backend_streams.h"

int init_stream_group(Memory * memory, Stream_Group * stream_group_ref, char * stream_name, int dev_id, int prio,  uint64_t stream_workspace_bytes){

	CUresult result;
	const char * err;

	strcpy(stream_group_ref -> stream_name, stream_name);

	stream_group_ref -> dev_id = dev_id;
	stream_group_ref -> prio = prio;
	stream_group_ref -> stream_workspace_bytes = stream_workspace_bytes;

	stream_group_ref -> compute_stream_ref = malloc(sizeof(CUstream));

	char cur_stream_name[PATH_MAX];

	result = cuStreamCreateWithPriority((CUstream *) (stream_group_ref -> compute_stream_ref), CU_STREAM_NON_BLOCKING, prio);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to create compute stream for dev %d, stream: %s. Err: %s\n", dev_id, stream_name, err);
		return -1;
	}

	sprintf(cur_stream_name, "%s: Compute", stream_name);
	profile_name_stream((CUstream *) (stream_group_ref -> compute_stream_ref), cur_stream_name);



	stream_group_ref -> inbound_stream_ref = malloc(sizeof(CUstream));

	result = cuStreamCreateWithPriority((CUstream *) (stream_group_ref -> inbound_stream_ref), CU_STREAM_NON_BLOCKING, prio);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to create inbound stream for dev %d, stream: %s. Err: %s\n", dev_id, stream_name, err);
		return -1;
	}

	sprintf(cur_stream_name, "%s: Inbound", stream_name);
	profile_name_stream((CUstream *) (stream_group_ref -> inbound_stream_ref), cur_stream_name);


	stream_group_ref -> outbound_stream_ref = malloc(sizeof(CUstream));

	result = cuStreamCreateWithPriority((CUstream *) (stream_group_ref -> outbound_stream_ref), CU_STREAM_NON_BLOCKING, prio);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to create inbound stream for dev %d, stream: %s. Err: %s\n", dev_id, stream_name, err);
		return -1;
	}

	sprintf(cur_stream_name, "%s: Outbound", stream_name);
	profile_name_stream((CUstream *) (stream_group_ref -> outbound_stream_ref), cur_stream_name);


	stream_group_ref -> compute_event_ref = malloc(sizeof(CUevent));


	result = cuEventCreate((CUevent *) (stream_group_ref -> compute_event_ref), CU_EVENT_DISABLE_TIMING);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to compute event for dev %d, stream: %s, Err: %s\n", dev_id, stream_name, err);
		return -1;
	}


	stream_group_ref -> inbound_event_ref = malloc(sizeof(CUevent));

	result = cuEventCreate((CUevent *) (stream_group_ref -> inbound_event_ref), CU_EVENT_DISABLE_TIMING);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to inbound event for dev %d, stream: %s, Err: %s\n", dev_id, stream_name, err);
		return -1;
	}

	stream_group_ref -> outbound_event_ref = malloc(sizeof(CUevent));

	result = cuEventCreate((CUevent *) (stream_group_ref -> outbound_event_ref), CU_EVENT_DISABLE_TIMING);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to outbound event for dev %d, stream: %s, Err: %s\n", dev_id, stream_name, err);
		return -1;
	}

	Mem_Ref * cur_workspace_ref = &(stream_group_ref -> workspace_mem_ref);
	memset(cur_workspace_ref, 0, sizeof(Mem_Ref));

	Mem_Reservation * cur_res;
	void * workspace_ptr;

	if (stream_workspace_bytes > 0){
		cur_res = &(cur_workspace_ref -> reservation);
		cur_res -> pool_id = dev_id;
		cur_res -> size_bytes = stream_workspace_bytes;

		workspace_ptr = reserve_memory(memory, cur_res, NULL);
		if (!workspace_ptr){
			fprintf(stderr, "Error: failure to reserve workspace bytes memory on pool %d for stream %s, for %lu bytes\n", dev_id, stream_name, stream_workspace_bytes);
			return -1;
		}

		cur_workspace_ref -> size = stream_workspace_bytes;
		cur_workspace_ref -> data = workspace_ptr;
	}
	else{
		cur_workspace_ref -> size = 0;
		cur_workspace_ref -> data = NULL;
	}

	return 0;
}


// supplied prios is optional, but if it is passed in must have length = total_compute_streams
int init_backend_streams(Backend_Funcs * backend_funcs, int num_devices, int * dev_ids, CUcontext * contexts, int total_compute_streams, int * compute_streams_per_device, int * supplied_prios, uint64_t stream_workspace_bytes) {

	int ret;

	CUresult result;
	const char * err;

	Memory * memory = backend_funcs -> memory;

	backend_funcs -> num_devices = num_devices;
	backend_funcs -> contexts = contexts;
	backend_funcs -> total_compute_streams = total_compute_streams;
	backend_funcs -> stream_workspace_bytes = stream_workspace_bytes;
	memcpy(backend_funcs -> dev_ids, dev_ids, num_devices * sizeof(int));
	memcpy(backend_funcs -> compute_streams_per_device, compute_streams_per_device, num_devices * sizeof(int));
	

	int dev_id;
	int dev_num_streams;
	int cur_global_stream = 0;

	// PIPELINE MAX POOLS OUTER INDICES, WITH INNER ARRAYS 
	// CREATED DYNAMICALLY BASED ON NUMBER OF STREAMS PER DEVICE
	Stream_Group * dev_stream_groups;

	int cur_stream_prio;

	Mem_Ref * cur_workspace_ref;
	Mem_Reservation * cur_res;
	void * workspace_ptr;

	cublasStatus_t status;

	char stream_name[256];
	for (int i = 0; i < num_devices; i++){
		dev_id = dev_ids[i];
		dev_num_streams = compute_streams_per_device[i];
		
		dev_stream_groups = malloc(dev_num_streams * sizeof(Stream_Group));

		push_context((backend_funcs -> contexts)[dev_id]);

		// HANDLE DEFAULT STREAM VALUES 


		// make default compute stream
		result = cuStreamCreateWithPriority(&((backend_funcs -> default_compute_streams)[dev_id]), CU_STREAM_NON_BLOCKING, DEFAULT_COMPUTE_STREAM_PRIO);
		if (result != CUDA_SUCCESS){
			cuGetErrorString(result, &err);
			fprintf(stderr, "Error: unable to create default compute stream for dev %d. Err: %s\n", i, err);
			return -1;
		}
		sprintf(stream_name, "Default Compute Stream (Device %d)", i);
		profile_name_stream(&((backend_funcs -> default_compute_streams)[dev_id]), stream_name);


		result = cuStreamCreateWithPriority(&((backend_funcs -> layer_inbound_streams)[dev_id]), CU_STREAM_NON_BLOCKING, DEFAULT_COMPUTE_STREAM_PRIO);
		if (result != CUDA_SUCCESS){
			cuGetErrorString(result, &err);
			fprintf(stderr, "Error: unable to create default compute stream for dev %d. Err: %s\n", i, err);
			return -1;
		}
		sprintf(stream_name, "Layer Inbound (Device %d)", i);
		profile_name_stream(&((backend_funcs -> layer_inbound_streams)[dev_id]), stream_name);

		// assuming cur_global_stream indicates the stage number in pipeline...
		for (int s = 0; s < dev_num_streams; s++){

			// if caller supplied priorities for each stream then we should adhere
			if (supplied_prios){
				cur_stream_prio = supplied_prios[cur_global_stream];
			}
			// otherwise set default
			else{
				// set priority inversely related the stage within the device (future stages have precedency, meaning
				// a lower set priority value to acheive this)

				// all have same prio means issue order will determine order when multiple kernels from seperate streams are ready...
				cur_stream_prio = dev_num_streams - s;
			}
			

			sprintf(stream_name, "Stream %d", s);
			ret = init_stream_group(memory, &dev_stream_groups[s], stream_name, dev_id, cur_stream_prio, stream_workspace_bytes);
			if (ret){
				fprintf(stderr, "Error: could not init stream group #%d on device %d...\n", s, dev_id);
				return -1;
			}

			cur_global_stream++;
		}

		// printf("Creating cublas lt handle...\n\n");

		// init cublas handle on this context (device) that all streams can share
		status  = cublasLtCreate(&(backend_funcs -> cublas_handles[dev_id]));
		if (status != CUBLAS_STATUS_SUCCESS) {
			fprintf(stderr, "Error: could not create cublasLt handle on device %d\n", dev_id);
			return -1;
		}

		(backend_funcs -> stream_groups)[dev_id] = dev_stream_groups;


		pop_current_context(NULL);
	}

	return 0;

}


// For now just assuming cuda streams....

// stream_to_wait_on_ref context needs to be set...?
int backend_add_dependent_stream(void * cur_stream_ref, void * stream_to_wait_on_ref, void * to_wait_on_event_ref) {

	if ((!cur_stream_ref) || (!stream_to_wait_on_ref)){
		fprintf(stderr, "Error: trying to add dependent stream when one stream ref is NULL\n");
		return -1;
	}

	CUresult result;
	const char * err;


	CUstream cur_stream = *((CUstream *) cur_stream_ref);

	CUevent to_wait_on_event = *((CUevent *) to_wait_on_event_ref);
	CUstream stream_to_wait_on = *((CUstream *)  stream_to_wait_on_ref);

	// 1.) record current state of stream to wait on
	result = cuEventRecord(to_wait_on_event, stream_to_wait_on);
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to record event for stream to wait on. Err: %s\n", err);
		return -1;
	}

	// 2.) enforce this depenency on cur stream
	result = cuStreamWaitEvent(cur_stream, to_wait_on_event, CU_EVENT_WAIT_DEFAULT); 
	if (result != CUDA_SUCCESS){
		cuGetErrorString(result, &err);
		fprintf(stderr, "Error: unable to do stream wait event. Err: %s", err);
		return -1;
	}

	return 0;
}