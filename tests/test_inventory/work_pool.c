#include "work_pool.h"


Work_Pool * init_work_pool(int max_work_class_ind) {

	Work_Pool * work_pool = (Work_Pool *) malloc(sizeof(Work_Pool));
	if (work_pool == NULL){
		fprintf(stderr, "Error: malloc failed to allocate work_pool container\n");
		return NULL;
	}

	work_pool -> max_work_class_ind = max_work_class_ind;

	work_pool -> classes = (Work_Class **) malloc((max_work_class_ind + 1) * sizeof(Work_Class *));
	if (work_pool -> classes == NULL){
		fprintf(stderr, "Error: malloc failed to allocate work_pool classes array\n");
		return NULL;
	}

	for (int i = 0; i < max_work_class_ind + 1; i++){
		(work_pool -> classes)[i] = NULL;
	}

	return work_pool;
}


int add_work_class(Work_Pool * work_pool, int work_class_index, int num_workers, uint64_t worker_max_tasks, uint64_t task_size, void *(*start_routine)(void *), void * worker_arg) {

	if (work_class_index > work_pool -> max_work_class_ind){
		fprintf(stderr, "Error: failed to add work class index: %d. The work pool was set to have a maximum class index of %d\n", work_class_index, work_pool -> max_work_class_ind);
		return -1;
	}

	// 1.) build the work class

	Work_Class ** classes = work_pool -> classes;

	Work_Class * work_class = (Work_Class *) malloc(sizeof(Work_Class));
	if (work_class == NULL){
		fprintf(stderr, "Error: malloc failed to allocate work class\n");
		return -1;
	}

	work_class -> num_workers = num_workers;
	work_class -> worker_threads = (pthread_t *) malloc(num_workers * sizeof(pthread_t));
	if (work_class -> worker_threads == NULL){
		fprintf(stderr, "Error: malloc failed to allocate worker threads container\n");
		return -1;
	}
	work_class -> worker_tasks = (Fifo **) malloc(num_workers * sizeof(Fifo *));
	if (work_class -> worker_tasks == NULL){
		fprintf(stderr, "Error: failed to intialize fifo worker tasks container\n");
		return -1;
	}

	for (int i = 0; i < num_workers; i++){
		(work_class -> worker_tasks)[i] = init_fifo(worker_max_tasks, task_size);
		if ((work_class -> worker_tasks)[i] == NULL){
			fprintf(stderr, "Error: unable to intialize worker task fifo for worker num %d\n", i);
			return -1;
		}
	}

	
	work_class -> work_bench = NULL;


	work_class -> start_routine = start_routine;
	work_class -> worker_arg = worker_arg;


	classes[work_class_index] = work_class;


	// 2.) Create thread data for each worker in this class
	work_class -> worker_thread_data = (Worker_Thread_Data *) malloc(num_workers * sizeof(Worker_Thread_Data));
	if (work_class -> worker_thread_data == NULL){
		fprintf(stderr, "Error: malloc failed to allocate array for worker thread data on class %d for %d workers\n", work_class_index, num_workers);
		return -1;
	}

	for (int i = 0; i < num_workers; i++){
		(work_class -> worker_thread_data)[i].worker_thread_id = i;
		(work_class -> worker_thread_data)[i].tasks = (work_class -> worker_tasks)[i];
		(work_class -> worker_thread_data)[i].work_bench = &(work_class -> work_bench);
		(work_class -> worker_thread_data)[i].worker_arg = worker_arg;
	}

	return 0;
}


sem_t * add_work_class_bench(Work_Pool * work_pool, int work_class_index, uint64_t task_cnt_start_bench, uint64_t task_cnt_stop_bench){

	int ret;

	if (work_class_index > work_pool -> max_work_class_ind){
		fprintf(stderr, "Error: failed to add benchamrk because work class index: %d does not exist. The work pool was set to have a maximum of %d classes\n", 
						work_class_index, work_pool -> max_work_class_ind);
		return NULL;
	}

	Work_Bench * work_bench = (Work_Bench *) malloc(sizeof(Work_Bench));
	if (work_bench == NULL){
		fprintf(stderr, "Error: malloc failed to allocate work_bench\n");
		return NULL;
	}

	ret = pthread_mutex_init(&(work_bench -> task_cnt_lock), NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not init work_class task_cnt_lock lock\n");
		return NULL;
	}

	work_bench -> task_cnt = 0;
	work_bench -> started = false;
	work_bench -> stopped = false;

	work_bench -> task_cnt_start_bench = task_cnt_start_bench;
	work_bench -> task_cnt_stop_bench = task_cnt_stop_bench;

	ret = sem_init(&(work_bench -> is_bench_ready), 0, 0);
	if (ret != 0){
		fprintf(stderr, "Error: could not initialize is_bench_ready sem\n");
		return NULL;
	}

	// timestamps get recorded wihtin the worker threads based on task count!


	// set work_bench within work_class
	Work_Class ** classes = work_pool -> classes;
	Work_Class * work_class = classes[work_class_index];
	work_class -> work_bench = work_bench;
	
	return &(work_bench -> is_bench_ready);
}

int start_all_workers(Work_Pool * work_pool) {

	int ret;

	int max_work_class_ind = work_pool -> max_work_class_ind;

	Work_Class ** classes = work_pool -> classes;

	int num_workers;
	Work_Class * cur_class;

	Worker_Thread_Data * cur_worker_thread_data;

	for (int i = 0; i < max_work_class_ind + 1; i++){
		cur_class = classes[i];
		if (cur_class == NULL){
			continue;
		}
		num_workers = cur_class -> num_workers;
		cur_worker_thread_data = cur_class -> worker_thread_data;
		for (int thread_id = 0; thread_id < num_workers; thread_id++){
			ret = pthread_create(&(cur_class -> worker_threads[thread_id]), NULL, cur_class -> start_routine, &(cur_worker_thread_data[thread_id]));
			if (ret != 0){
				fprintf(stderr, "Error: start_all_workers failed on class index: %d, and thread id: %d", i, thread_id);
				return -1;
			}
		} 
	}
	return 0;
}