#include "sys.h"


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


	// 6.) Update exchange with information from net_world
	ret = update_init_exchange_with_net_info(exchange, net_world -> self_node_id, net_world -> max_nodes);
	if (ret != 0){
		fprintf(stderr, "Error: failed to update exchange with net_info\n");
		return NULL;
	}


	// 6.) create work pool

	// defined in messsages.h => included from config.h
	Work_Pool * work_pool = init_work_pool(MAX_WORK_CLASS_IND);
	if (work_pool == NULL){
		fprintf(stderr, "Error: failed to intialize work_pool\n");
		return NULL;
	}


	// 7.) Add all work classes to pool!


	// a.) Exchange Class

	Exchange_Worker_Data * exchange_worker_data = (Exchange_Worker_Data *) malloc(sizeof(Exchange_Worker_Data));
	if (exchange_worker_data == NULL){
		fprintf(stderr, "Error: malloc failed to allocate exchange_worker_data before adding to work pool\n");
		return NULL;
	}

	exchange_worker_data -> exchange = exchange;
	exchange_worker_data -> net_world = net_world;

	//	- each worker will have their own worker_data specified in their worker file
	ret = add_work_class(work_pool, EXCHANGE_CLASS, NUM_EXCHANGE_WORKER_THREADS, EXCHANGE_WORKER_MAX_TASKS_BACKLOG, sizeof(Ctrl_Message), run_exchange_worker, exchange_worker_data);
	if (ret != 0){
		fprintf(stderr, "Error: unable to add worker class\n");
		return NULL;
	}


	// b.) Inventory Class

	Inventory_Worker_Data * inventory_worker_data = (Inventory_Worker_Data *) malloc(sizeof(Inventory_Worker_Data));
	if (inventory_worker_data == NULL){
		fprintf(stderr, "Error: malloc failed to allocate exchange_worker_data before adding to work pool\n");
		return NULL;
	}

	//inventory_worker_data.inventory = inventory;
	inventory_worker_data -> net_world = net_world;

	//	- each worker will have their own worker_data specified in their worker file
	ret = add_work_class(work_pool, INVENTORY_CLASS, NUM_INVENTORY_WORKER_THREADS, INVENTORY_WORKER_MAX_TASKS_BACKLOG, sizeof(Ctrl_Message), run_inventory_worker, inventory_worker_data);
	if (ret != 0){
		fprintf(stderr, "Error: unable to add worker class\n");
		return NULL;
	}




	// 8.) Add list to hold semaphores that calling thread should wait on before benchmark results are available

	// init list of sempahores that calling thread can wait on to know when benchmarks are ready
	// item_cmp not needed here
	Deque * are_benchmarks_ready = init_deque(NULL);

	// 9.) Update system values

	system -> work_pool = work_pool;
	system -> exchange = exchange;
	system -> net_world = net_world;
	system -> are_benchmarks_ready = are_benchmarks_ready;

	// NOTE: 
	//	- need to run start_system in order to actually bringup all CQ threads and all Worker Threads!!!

	return system;
}


// This should be called after init_system but before start_system
int add_message_class_benchmark(System * system, CtrlMessageClass message_class, uint64_t start_message_cnt, uint64_t end_message_cnt) {


	Deque * are_benchmarks_ready = system -> are_benchmarks_ready;
	Work_Pool * work_pool = system -> work_pool; 

	// exchange throughput benchmark 

	// seeing how long it takes to process 1 million exchange tasks
	sem_t * bench_sem = add_work_class_bench(work_pool, message_class, start_message_cnt, end_message_cnt);
	if (bench_sem == NULL){
		fprintf(stderr, "Error: failed to add benchmark to record throughput for class %d\n", message_class);
		return -1;
	}
	// adding this to benchmarks deque for calling thread to wait on
	int ret = insert_deque(are_benchmarks_ready, BACK_DEQUE, bench_sem);
	if (ret != 0){
		fprintf(stderr, "Error: failed to insert benchmark is ready sem to deque\n");
		return -1;
	}

	return 0;
}



// This returns after min_init_nodes have been added to the net_world table
// After calling this then can start to actually send/recv messages
int start_system(System * system) {

	int ret;


	Net_World * net_world = system -> net_world;
	Work_Pool * work_pool = system -> work_pool;

	// 8.) Spawn all worker threads

	// For each work class populate their worker argument

	ret = start_all_workers(work_pool);
	if (ret != 0){
		fprintf(stderr, "Error: starting all workers failed\n");
		return -1;
	}

	
	// 9.) Start all the completition queue handler threads for the network
	ret = activate_cq_threads(net_world, work_pool);
	if (ret != 0){
		fprintf(stderr, "Error: failure activing cq threads\n");
		return -1;
	}

	// 10.) wait until min_init_nodes (besides master) have been added to the net_world -> nodes table
	sem_wait(&(net_world -> is_init_ready));

	return 0;
}