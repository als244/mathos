#include "cq_handler.h"

int run_cq_thread(pthread_t * thread, EndpointType endpoint_type, cpu_set_t * affinity, Cq_Thread_Data * cq_thread_data, bool is_recv){

	int ret;

	// should call pthread_setaffinity_np() here

	switch(endpoint_type){
		case CONTROL_ENDPOINT:
			if (is_recv){
				ret = pthread_create(thread, NULL, run_recv_ctrl_handler, (void *) cq_thread_data);
			}
			else{
				ret = pthread_create(thread, NULL, run_send_ctrl_handler, (void *) cq_thread_data);
			}
			break;
		// NEED TO IMPLEMENT BUT FOR NOW DOING NOTHING
		case DATA_ENDPOINT:
			return 0;
		default:
			fprintf(stderr, "Error: unkown endpoint type of %d. Do not have completition queue handler.\n", endpoint_type);
			return -1;
	}

	if (ret != 0){
		fprintf(stderr, "Error: could not create thread\n");
		return -1;
	}

	return 0;
}


int activate_cq_threads(Net_World * net_world, Work_Pool * work_pool){

	int ret;

	Self_Net * self_net = net_world -> self_net;

	int num_ib_devices = self_net -> num_ib_devices;
	int num_endpoint_types = self_net -> num_endpoint_types;
	EndpointType * endpoint_types = self_net -> endpoint_types;


	struct ibv_cq_ex *** cq_recv_collection = self_net -> cq_recv_collection;
	struct ibv_cq_ex *** cq_send_collection = self_net -> cq_send_collection;

	// FOW NOW: not using
	// TODO: SET WITHIN SELF_NET (step 9 in self_net init function)
	
	cpu_set_t ** ib_dev_cpu_affinities = self_net -> ib_dev_cpu_affinities;
	cpu_set_t * cur_ib_dev_cpu_affinity;


	// Allocate thread data for everything
	int num_cq_threads = num_ib_devices * num_endpoint_types;


	Cq_Thread_Data * cq_recv_thread_data = (Cq_Thread_Data *) malloc(num_cq_threads * sizeof(Cq_Thread_Data));
	if (cq_recv_thread_data == NULL){
		fprintf(stderr, "Error: malloc failed to allocate array for cq recv thread data\n");
		return -1;
	}

	for (int i = 0; i < num_ib_devices; i++){
		for (int j = 0; j < num_endpoint_types; j++){
			cq_recv_thread_data[i * num_endpoint_types + j].ib_device_id = i;
			cq_recv_thread_data[i * num_endpoint_types + j].endpoint_type = endpoint_types[j];
			cq_recv_thread_data[i * num_endpoint_types + j].net_world = net_world;
			cq_recv_thread_data[i * num_endpoint_types + j].work_pool = work_pool;
			cq_recv_thread_data[i * num_endpoint_types + j].cq = cq_recv_collection[i][j];
			if (endpoint_types[j] == CONTROL_ENDPOINT){
				cq_recv_thread_data[i * num_endpoint_types + j].recv_dispatcher_fifo = init_fifo(CTRL_RECV_DISPATCHER_BACKLOG_MESSAGES, sizeof(Recv_Ctrl_Message));
				if (cq_recv_thread_data[i * num_endpoint_types + j].recv_dispatcher_fifo == NULL){
					fprintf(stderr, "Error: failed to initialize control receive dispatcher fifo for ib device id %d\n", i);
				}
			}
			else{
				cq_recv_thread_data[i * num_endpoint_types + j].recv_dispatcher_fifo = NULL;
			}
			
		}
	}



	Cq_Thread_Data * cq_send_thread_data = (Cq_Thread_Data *) malloc(num_cq_threads * sizeof(Cq_Thread_Data));
	if (cq_send_thread_data == NULL){
		fprintf(stderr, "Error: malloc failed to allocate array for cq send thread data\n");
		return -1;
	}

	for (int i = 0; i < num_ib_devices; i++){
		for (int j = 0; j < num_endpoint_types; j++){
			cq_send_thread_data[i * num_endpoint_types + j].ib_device_id = i;
			cq_send_thread_data[i * num_endpoint_types + j].endpoint_type = endpoint_types[j];
			cq_send_thread_data[i * num_endpoint_types + j].net_world = net_world;
			cq_send_thread_data[i * num_endpoint_types + j].work_pool = work_pool;
			cq_send_thread_data[i * num_endpoint_types + j].cq = cq_send_collection[i][j];
			cq_send_thread_data[i * num_endpoint_types + j].recv_dispatcher_fifo = NULL;
		}
	}


	pthread_t ** cq_recv_threads = self_net -> cq_recv_threads;
	pthread_t ** cq_send_threads = self_net -> cq_send_threads;


	for (int i = 0; i < num_ib_devices; i++){
		// SHOULD GET cpu_set_t affinity here!
		cur_ib_dev_cpu_affinity = ib_dev_cpu_affinities[i];
		for (int j = 0; j < num_endpoint_types; j++){
			ret = run_cq_thread(&(cq_recv_threads[i][j]), endpoint_types[j], cur_ib_dev_cpu_affinity, &(cq_recv_thread_data[i * num_endpoint_types + j]), true);
			if (ret != 0){
				fprintf(stderr, "Error: could not run cq recv thread from device #%d, and endpoint type ind #%d\n", i, j);
				return -1;
			}
		}
	}


	for (int i = 0; i < num_ib_devices; i++){
		// SHOULD GET cpu_set_t affinity here!
		cur_ib_dev_cpu_affinity = ib_dev_cpu_affinities[i];
		for (int j = 0; j < num_endpoint_types; j++){
			ret = run_cq_thread(&(cq_send_threads[i][j]), endpoint_types[j], cur_ib_dev_cpu_affinity, &(cq_send_thread_data[i * num_endpoint_types + j]), false);
			if (ret != 0){
				fprintf(stderr, "Error: could not run cq send thread from device #%d, and endpoint type ind #%d\n", i, j);
				return -1;
			}
		}
	}

	return 0;

}