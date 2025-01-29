#ifndef COMMON_H
#define COMMON_H

// DOING TOP-LEVEL IMPORTS

// OK FOR NOW, BUT UNNECESSARY TO INCLUDE THESE IN ALL FILES....
#define  _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <linux/limits.h>

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
// System V semaphores (can increment/decrement other than one)
#include <sys/sem.h>
// Simpler POSIX semaphores
#include <semaphore.h>
// Setting file permission bits
#include <fcntl.h>
#include <sys/types.h>



// For initial TCP connections
#include <sys/socket.h>
#include <netinet/in.h>

/* To convert ip addr strings to network-byte ordered uint32's*/
#include <arpa/inet.h>

// for libibverbs 
#include <infiniband/verbs.h>


#define FINGERPRINT_NUM_BYTES 32
#define FINGERPRINT_TYPE SHA256_HASH

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)


typedef int (*Item_Cmp)(void * item, void * other_item);
typedef uint64_t (*Hash_Func)(void * item, uint64_t table_size);



/*

// This is used for sys v IPC
// Per man semctl, calling progrma must define this union as follows
// For LINUX!
union semun {
	int val; // Value for SETVAL
	struct semid_ds *buf; // Buffer for IPC_STAT, IPC_SET
	unsigned short *array; // Array for GETALL, SETALL
	struct seminfo *__buf; // Buffer for IPC_INFO
	// (Linux specific)
};

*/

#define MY_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MY_MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define MY_CEIL(a, b) ((a + b - 1) / b)





#endif