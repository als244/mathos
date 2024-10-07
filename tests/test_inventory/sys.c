#include "sys.h"

System * init_system(char * master_ip_addr, char * self_ip_addr, Memory * memory){

	int ret;

	System * system = (System *) malloc(sizeof(System));
	if (system == NULL){
		fprintf(stderr, "Error: malloc failed to allocate system struct container\n");
		return NULL;
	}

	// COULD INITIALIZE THIS HERE!!!
	// (i.e. have an init_backend_memory() call here that makes sense)
	// probably a function pointer for each backend...??
	system -> memory = memory;

	// 1.) Initialize empty exchange

	Exchange * exchange = init_exchange();
	if (!exchange){
		fprintf(stderr, "Error: failed to initialize exchange\n");
		return NULL;
	}

	// 2.) Initialize inventory
	Inventory * inventory = init_inventory(memory);
	if (!inventory){
		fprintf(stderr, "Error: failed to initialize inventory\n");
		return NULL;
	}


	// 3.) Initialize function ingestion stuff


	// 4.) Intialize function request handling



	// 5.) Initialize network and wait for minimum number of nodes to join (set by the master)

	Net_World * net_world = init_net(master_ip_addr, self_ip_addr);
	if (!net_world){
		fprintf(stderr, "Error: failed to initialize network\n");
		return NULL;
	}


	// 6.) Update exchange with information from net_world
	ret = update_init_exchange_with_net_info(exchange, net_world -> self_node_id, net_world -> max_nodes);
	if (ret){
		fprintf(stderr, "Error: failed to update exchange with net_info\n");
		return NULL;
	}


	// 6.) create work pool

	// defined in messsages.h => included from config.h
	Work_Pool * work_pool = init_work_pool(MAX_WORK_CLASS_IND);
	if (!work_pool){
		fprintf(stderr, "Error: failed to intialize work_pool\n");
		return NULL;
	}


	// 7.) Add all work classes to pool!


	// a.) Exchange Class

	Exchange_Worker_Data * exchange_worker_data = (Exchange_Worker_Data *) malloc(sizeof(Exchange_Worker_Data));
	if (!exchange_worker_data){
		fprintf(stderr, "Error: malloc failed to allocate exchange_worker_data before adding to work pool\n");
		return NULL;
	}

	exchange_worker_data -> exchange = exchange;
	exchange_worker_data -> net_world = net_world;
	exchange_worker_data -> inventory = inventory;

	//	- each worker will have their own worker_data specified in their worker file
	ret = add_work_class(work_pool, EXCHANGE_CLASS, NUM_EXCHANGE_WORKER_THREADS, EXCHANGE_WORKER_MAX_TASKS_BACKLOG, sizeof(Ctrl_Message), run_exchange_worker, exchange_worker_data);
	if (ret){
		fprintf(stderr, "Error: unable to add worker class\n");
		return NULL;
	}


	// b.) Inventory Class

	Inventory_Worker_Data * inventory_worker_data = (Inventory_Worker_Data *) malloc(sizeof(Inventory_Worker_Data));
	if (!inventory_worker_data){
		fprintf(stderr, "Error: malloc failed to allocate exchange_worker_data before adding to work pool\n");
		return NULL;
	}

	inventory_worker_data -> inventory = inventory;
	inventory_worker_data -> net_world = net_world;

	//	- each worker will have their own worker_data specified in their worker file
	ret = add_work_class(work_pool, INVENTORY_CLASS, NUM_INVENTORY_WORKER_THREADS, INVENTORY_WORKER_MAX_TASKS_BACKLOG, sizeof(Ctrl_Message), run_inventory_worker, inventory_worker_data);
	if (ret){
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
	system -> inventory = inventory;
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


// Register each compute device's memory region for all
// rdma devices on system 
//	(registering same region for different PDs is OK I think...?)
int register_device_memory_with_network(System * system){

	Net_World * net_world = system -> net_world;
	Self_Net * self_net = net_world -> self_net;
	struct ibv_pd ** ib_dev_pds = self_net -> dev_pds; 

	Memory * memory = system -> memory;

	int num_devices = memory -> num_devices;

	int num_ib_devices = self_net -> num_ib_devices;
	int mr_access = IBV_ACCESS_LOCAL_WRITE;


	// SETTING THE num_ib_devices within the memory struct for convenience
	memory -> num_ib_devices = num_ib_devices;


	Mempool * dev_mempool;
	uint64_t dev_capacity_bytes;
	void * dev_va_start_addr;

	struct ibv_pd * pd;
	struct ibv_mr * mr;

	for (int i = 0; i < num_devices; i++){

		dev_mempool = &(memory -> device_mempools[i]);
		dev_capacity_bytes = dev_mempool -> capacity_bytes;
		dev_va_start_addr = (void *) dev_mempool -> va_start_addr;

		// initialize the array to hold the lkeys
		dev_mempool -> ib_dev_mrs = (struct ibv_mr **) malloc(num_ib_devices * sizeof(struct ibv_mr *));
		if (!(dev_mempool -> ib_dev_mrs)){
			fprintf(stderr, "Error: malloc failed to allocate container containing the ib mrs for compute device #%d\n", i);
			return -1;
		}

		for (int ib_device_id = 0; ib_device_id < num_ib_devices; ib_device_id++){

			pd = ib_dev_pds[ib_device_id];

			mr = ibv_reg_mr(pd, dev_va_start_addr, dev_capacity_bytes, mr_access);
			if (mr == NULL){
				fprintf(stderr, "Error: ibv_reg_mr failed for compute device #%d and ib_device #%d\n", i, ib_device_id);
				return -1;
			}

			(dev_mempool -> ib_dev_mrs)[ib_device_id] = mr;

		}
	}

	return 0;
}


int start_memory(System * system){

	// a.) register memory regions
	//		- one region per device (entire device)
	//			- gets registered for all IB devices (i.e. for each ibv_context == protection domain)

	int ret = register_device_memory_with_network(system);

	if (ret){
		fprintf(stderr, "Error: failure to register device memory with verbs...\n");
		return -1;
	}


	// b.) Initilaize the memory operations fifo that
	// 		other threads will use to perform memory ops
	//		(makes life simpler + better to have a single thread
	// 		repsonbile for reservations / releasing => no need for locking/contention
	//		+ caching benefits for the memory metadata structures)

	Memory * memory = system -> memory;

	int num_devices = memory -> num_devices;

	// including the system memory mempool
	int num_mempools = num_devices + 1;

	Fifo ** mem_op_fifos = (Fifo **) malloc(num_mempools * sizeof(Fifo *));
	if (!mem_op_fifos){
		fprintf(stderr, "Error: malloc failed to allocate container for memory ops fifo\n");
		return -1;
	}

	for (int i = 0; i < num_mempools; i++){
		mem_op_fifos[i] = init_fifo(MEMORY_OPS_BUFFER_MAX_REQUESTS_PER_MEMPOOL, sizeof(Mem_Op *));
		if (!mem_op_fifos[i]){
			fprintf(stderr, "Error: failure to initialize memory ops fifo for mempool %d\n", i);
			return -1;
		}
	}

	memory -> mem_op_fifos = mem_op_fifos;


	// c.) Need to spwan memory thread
	//		- for now just having a singluar thread, 
	//			but we could do 1 master thread (to handle conflicts) and then 1 thread per mempool
	pthread_create(&(system -> memory_server), NULL, run_memory_server, (void *) memory);

	return 0;
}


// This returns after min_init_nodes have been added to the net_world table
// After calling this then can start to actually send/recv messages
int start_system(System * system) {

	int ret;

	// 1.) Start Memory Server

	// a.) Register All device memory with Network Card
	// b.) Setup Memory Operation fifo
	// c.) Spawn memory thread


	if (system -> memory){
		ret = start_memory(system);

		if (ret){
			fprintf(stderr, "Error: failure to setup memory...\n");
			return -1;
		}
	}


	Net_World * net_world = system -> net_world;
	Work_Pool * work_pool = system -> work_pool;

	// 2.) Spawn all worker threads

	// For each work class populate their worker argument

	ret = start_all_workers(work_pool);
	if (ret != 0){
		fprintf(stderr, "Error: starting all workers failed\n");
		return -1;
	}

	
	// 3.) Start all the completition queue handler threads for the network
	
	ret = activate_cq_threads(net_world, work_pool);
	if (ret != 0){
		fprintf(stderr, "Error: failure activing cq threads\n");
		return -1;
	}

	// 4.) wait until min_init_nodes (besides master) have been added to the net_world -> nodes table
	sem_wait(&(net_world -> is_init_ready));

	return 0;
}