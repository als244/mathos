#ifndef COMMON_H
#define COMMON_H

// DOING TOP-LEVEL IMPORTS

// OK FOR NOW, BUT UNNECESSARY TO INCLUDE THESE IN ALL FILES....
#define  _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <semaphore.h>
#include <unistd.h>
#include <errno.h>

// For initial TCP connections
#include <sys/socket.h>
#include <netinet/in.h>

/* To convert ip addr strings to network-byte ordered uint32's*/
#include <arpa/inet.h>

// for libibverbs 
#include <infiniband/verbs.h>

// for cpuset macros
#include <sched.h>

#define FINGERPRINT_NUM_BYTES 32
#define FINGERPRINT_TYPE SHA256_HASH

// performance optimizations for branch predictions
//	- mostly using unlikely for error checking
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)


#endif