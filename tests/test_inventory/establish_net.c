#include "establish_net.h"

// MIGHT WANT TO CONVERT THREADS TO LIBEVENT FOR BETTER SCALABILITY (if initialization time is a concern)
//	- Ref: https://github.com/jasonish/libevent-examples/blob/master/echo-server/libevent_echosrv1.c

typedef struct connection_thread_data {
	uint32_t thread_ind;
	Net_World * net_world;
	struct sockaddr_in serv_addr;
	// after terminating connection acquire lock and add
	pthread_mutex_t * free_threads_lock;
	Deque * free_threads;
	// WILL BE WRITTEN TO BY MAIN THREAD UPON ACCEPT
	int sockfd;
	struct sockaddr_in client_addr;
} Connection_Thread_Data;

// Called for each thread
void * process_rdma_init_connection(void * _connection_thread_data) {
	
	int ret;

	// net_world was passed when setting this "on_accept" event
	Connection_Thread_Data * connection_thread_data = (Connection_Thread_Data *) _connection_thread_data;

	// HANDLE PROCESSING THE CONNECTION HERE....

	Net_World * net_world = connection_thread_data -> net_world;




	// At the end acquire lock and enqueue this back to free threads
	pthread_mutex_lock(connection_thread_data -> free_threads_lock);

	Deque * free_threads = connection_thread_data -> free_threads;
	ret = enqueue(free_threads, connection_thread_data);
	if (ret != 0){
		fprintf(stderr, "Error: could not enqueue back to free threads deque after processing connection\n");
		pthread_mutex_unlock(connection_thread_data -> free_threads_lock);
		exit(1);
	}

	pthread_mutex_unlock(connection_thread_data -> free_threads_lock);

	exit(0);
}


int start_tcp_server_for_rdma_init(Net_World * net_world, uint32_t num_threads, uint32_t max_accepts){

	int ret;

	// 1.) create server TCP socket
	int serv_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sockfd != 0){
		fprintf(stderr, "Error: could not create server socket\n");
		return -1;
	}

	// 2.) Init server info
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;

	// normally would use inet_aton (or inet_addr) here with a char * ipv4 dots
	// however, we intialized the net_world struct with the converted network-ordered uint32 value already
	uint32_t self_network_ip_addr = net_world -> self_net -> ip_addr;
	serv_addr.sin_addr.s_addr = self_network_ip_addr;
	// Using the same port on all nodes for initial connection establishment to trade rdma info
	// defined within establish_net.h
	serv_addr.sin_port = htons(ESTABLISH_NET_PORT);

	// 3.) Bind server to port
	ret = bind(serv_sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
	if (ret != 0){
		fprintf(stderr, "Error: could not bind server socket to address: %s, port: %u\n", inet_ntoa(self_network_ip_addr), ESTABLISH_NET_PORT);
		return -1;
	}

	// 4.) Create threads that will be used to process each communication intialization
	pthread_t * threads = (pthread_t *) malloc(num_threads * sizeof(pthread_t));
	if (threads == NULL){
		fprintf(stderr, "Error: malloc failed to allocate pthreads array\n");
		return -1;
	}

	// SHOULD REALLY BE A PRODUCER-CONSUMER BUFFER HERE!
	// Create Deque which will contains the free threads
	// Also create lock to ensure deque stability
	pthread_mutex_t free_threads_lock;
	ret = pthread_mutex_init(&free_threads_lock, NULL);
	if (ret != 0){
		fprintf(stderr, "Error: could not create free thread lock\n");
		return -1;
	}

	// will contain connection_thread_data objects that can be dequeued 
	// and passed as thread data to pthread_create for process_accept
	Deque * free_threads = init_deque();
	if (free_threads == NULL){
		fprintf(stderr, "Error: could not initialize free threads deque\n");
		return -1;
	}

	Connection_Thread_Data * connection_thread_data_arr = (Connection_Thread_Data *) malloc(num_threads * sizeof(Connection_Thread_Data));
	if (connection_thread_data_arr == NULL){
		fprintf(stderr, "Error: malloc failed to allocate connection_threads array\n");
		return -1;
	}

	// only thing that differs is the thread ind
	for (uint32_t i = 0; i < num_threads; i++){

		// first initialize all the data
		connection_thread_data_arr[i].thread_ind = i;
		connection_thread_data_arr[i].serv_addr = serv_addr;
		connection_thread_data_arr[i].net_world = net_world;
		connection_thread_data_arr[i].free_thread_lock = &free_threads_lock;
		connection_thread_data_arr[i].free_threads = free_threads;
		// will intialize the sockfd and client addr within accept loop

		// now enqueue to deque
		// can ignore lock at initialization time
		ret = enqueue(free_threads, &(connection_thread_data_arr[i]));
		if (ret != 0){
			fprintf(stderr, "Error: could not enqueue thread #%u to deque\n", i);
			return -1;
		}
	}

	// 5.) Start Listening
	ret = listen(serv_sockfd, max_accepts);
	if (ret != 0){
		fprintf(stderr, "Error: could not start listening on server socket\n");
		return -1;
	}


	// 6.) Process accepts
	uint32_t accept_cnt = 0;

	int accept_sockfd;

	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);

	Connection_Thread_Data * thread_data;
	volatile bool is_empty;

	while(accept_cnt < max_accepts){

		// 1.) accept new connection (blocking)

		accept_sockfd = accept(serv_sockfd, (struct sockaddr *) &client_addr, &client_len);
		if (accept_sockfd < 0){
			fprintf(stderr, "Error: could not process accept within tcp inti server\n");
			return -1;
		}

		// 2.) Once accepted, dequeue a thread to use for processing 

		// NOTE: This is ugly and should be using better sync method (producer/consumer with semaphores)
		
		// (first acquire lock)
		pthread_mutex_lock(&free_threads_lock);
		// wait until there is a free thread
		is_empty = is_deque_empty(free_threads);
		while (is_empty){
			// wait for someone else to enqueue, so we need to release lock
			pthread_mutex_unlock(&free_threads_lock);
			// wait some time for an existing thread to acquire lock and enqueue thread when they are done
			usleep(1000);
			// acquire lock again to check
			pthread_mutex_lock(&free_threads_lock);
			is_empty = is_deque_empty(free_threads);
		}

		// now we know there is an item so we can dequeue and release lock
		ret = dequeue(free_threads, &thread_data);
		if (ret != 0){
			fprintf(stderr, "Error: could not dequeue thread from free threads deque\n");
			return -1;
		}
		pthread_mutex_unlock(&free_threads_lock);


		// 3.) Modify the free thread fields that depend on accpet
		thread_data -> sockfd = accept_sockfd;
		thread_data -> client_addr = client_addr;

		uint32_t thread_ind = thread_data -> thread_ind;
		// 4.) Call function to process connection
		ret = pthread_create(&threads[thread_ind], NULL, process_rdma_init_connection, (void *) thread_data);
		if (ret != 0){
			fprintf(stderr, "Error: pthread_create failed\n");
			return -1;
		}

		// 5.) Update accept_cnt
		accept_cnt++;

	}
}



