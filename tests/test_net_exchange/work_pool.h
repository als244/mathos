#ifndef WORK_POOL_H
#define WORK_POOL_H


#include "common.h"
#include "config.h"
#include "fifo.h"

// THIS corresponds to number of ctrl message classes there are that have work that needs to be handled!

typedef struct worker_thread_data {
	int worker_thread_id;
	Fifo * tasks;
	void * worker_arg;
} Worker_Thread_Data;

typedef struct work_class {
	int num_workers;
	pthread_t * worker_threads;
	Fifo * tasks;
	void *(*start_routine)(void *);
	void * worker_arg;
	Worker_Thread_Data * worker_thread_data;
} Work_Class;

typedef struct work_pool {
	int max_work_classes;
	Work_Class ** classes;
} Work_Pool;


Work_Pool * init_work_pool(int max_work_classes);

int add_work_class(Work_Pool * work_pool, int work_class_index, int num_workers, uint64_t max_tasks, uint64_t task_size, void *(*start_routine)(void *), void * worker_arg);

int start_all_workers(Work_Pool * work_pool);


#endif