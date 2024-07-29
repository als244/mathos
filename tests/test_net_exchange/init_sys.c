#include "init_sys.h"


System * init_system(char * master_ip_addr, char * self_ip_addr){

	int ret;

	System * system = (System *) malloc(sizeof(System));
	if (system == NULL){
		fprintf(stderr, "Error: malloc failed to allocate system struct container\n");
		return NULL;
	}

	// 1.) Initialize empty exchange

	Exchange * exchange = init_exchange();
	if (exchange == NULL){
		fprintf(stderr, "Error: failed to initialize exchange\n");
		return NULL;
	}

	// 2.) Initialize inventory


	// 3.) Initialize function ingestion stuff


	// 4.) Intialize function request handling



	// 5.) Initialize network and wait for minimum number of nodes to join (set by the master)

	Net_World * net_world = init_net(master_ip_addr, self_ip_addr);
	if (net_world == NULL){
		fprintf(stderr, "Error: failed to initialize network\n");
		return NULL;
	}


	// 6.) create work pool

	// defined in messsages.h => included from config.h
	Work_Pool * work_pool = init_work_pool(MAX_WORK_CLASSES);
	if (work_pool == NULL){
		fprintf(stderr, "Error: failed to intialize work_pool\n");
		return NULL;
	}

	// add our exchange work pool

	Exchange_Worker_Data exchange_worker_data;

	exchange_worker_data.exchange = exchange;
	exchange_worker_data.net_world = net_world;

	// WILL OVERWRITE THE ARGUMENT IN STEP 7 WHEN READY TO RUN!
	//	- each worker will have their own worker_data specified in their worker file
	ret = add_work_class(work_pool, EXCHANGE_CLASS, NUM_EXCHANGE_WORKER_THREADS, EXCHANGE_MAX_TASKS, sizeof(Ctrl_Message), run_exchange_worker, &exchange_worker_data);
	if (ret != 0){
		fprintf(stderr, "Error: unable to add worker class\n");
		return NULL;
	}

	// 7.) Spawn all worker threads

	// For each work class populate their worker argument

	ret = start_all_workers(work_pool);
	if (ret != 0){
		fprintf(stderr, "Error: starting all workers failed\n");
	}

	
	// 8.) Start all the completition queue handler threads for the network
	ret = activate_cq_threads(net_world, work_pool);
	if (ret != 0){
		fprintf(stderr, "Error: failure activing cq threads\n");
		return NULL;
	}

	// 9.) wait until min_init_nodes (besides master) have been added to the net_world -> nodes table
	sem_wait(&(net_world -> is_init_ready));


	// 10.) Update system values

	system -> work_pool = work_pool;
	system -> exchange = exchange;
	system -> net_world = net_world;

	return system;
}