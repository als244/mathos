#ifndef WORK_POOL_H
#define WORK_POOL_H


#include "common.h"
#include "config.h"
#include "fifo.h"

// THIS corresponds to number of ctrl message classes there are that have work that needs to be handled!

typedef struct Work_Bench {
	pthread_mutex_t task_cnt_lock;
	// this task cnt is shared among all worker threads
	uint64_t task_cnt;
	// to shortcut needing to acquiring the cnt lock for no reason
	bool started;
	bool stopped;
	uint64_t task_cnt_start_bench;
	uint64_t task_cnt_stop_bench;
	struct timespec start;
	struct timespec stop;
	// this gets posted to when stop is populated
	// indicates to main thread that it can read results
	sem_t is_bench_ready;
} Work_Bench;


typedef struct worker_thread_data {
	int worker_thread_id;
	Fifo * tasks;
	// optionally have benchmark for recording timestamps upon certain cnts
	Work_Bench * work_bench;
	void * worker_arg;
} Worker_Thread_Data;

typedef struct work_class {
	int num_workers;
	pthread_t * worker_threads;
	Fifo * tasks;
	// Optionally add benchmark
	Work_Bench * work_bench;
	void *(*start_routine)(void *);
	void * worker_arg;
	Worker_Thread_Data * worker_thread_data;
} Work_Class;

typedef struct work_pool {
	int max_work_class_ind;
	Work_Class ** classes;
} Work_Pool;


Work_Pool * init_work_pool(int max_work_class_ind);

int add_work_class(Work_Pool * work_pool, int work_class_index, int num_workers, uint64_t max_tasks, uint64_t task_size, void *(*start_routine)(void *), void * worker_arg);

// Can wait on the work_bench -> is_bench_ready semaphore to know when the start/stop is ready to be read
// NOTE: needs to be called before starting workers

// for convenience, returns the semaphore that should be waited upon to indicate benchmark is finished
sem_t * add_work_class_bench(Work_Pool * work_pool, int work_class_index, uint64_t task_cnt_start_bench, uint64_t task_cnt_stop_bench);

int start_all_workers(Work_Pool * work_pool);


#endif