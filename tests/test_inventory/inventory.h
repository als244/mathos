#ifndef INVENTORY_H
#define INVENTORY_H

#include "common.h"
#include "mempool.h"
#include "fingerprint.h"

typedef struct obj_location {
	// identifies the size of object, and chunk ids
	Mem_Reservation * reservation;
	// needed to acquire before checking if obj is fully available
	pthread_mutex_t inbound_lock;
	bool is_available;
	// needed to acquire to increment when starting outbound transfer and decrement when completed
	pthread_mutex_t outbound_lock;
	uint32_t outbound_inflight_cnt;
} Obj_Locations;

// BY Construction, each pool only contains 1 copy of the object
typedef struct object {
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	uint64_t size_bytes;
	// mapping from pool id => obj_location
	Table * locations;
} Object;

typedef struct inventory {
	int num_compute_pools;
	// index is the device number
	// these pools were intialized with the backend-plugin
	Mempool ** compute_pools;
	// pool associated with system memory
	Mempool * cache_pool;
	// mapping from fingerprint -> obj
	Table * fingerprints;
} Inventory;

// Assume that the compute pools (memory assoicated with a computing device) were intialized prior
// This initialization creates the cache pool and the fingerprint table
Inventory * init_inventory(int num_compute_pools, Mempool ** compute_pools, uint64_t cache_pool_chunk_size, uint64_t num_cache_pool_chunks, uint64_t min_fingerprints, uint64_t max_fingerprints);

// returns 0 upon success, otherwise error

// Responsible for first checking if fingerprint is in inventory -> fingerprints. If not, allocate object and insert
// Allocates an object location and populates it with the mem_reservation returned from reserve_memory
// Inserts the object location into object -> locations
int reserve_object(Inventory * inventory, int pool_id, uint8_t * fingerprint, uint64_t size_bytes);

// Responsbile for checking if fingerprint exists in fingerprint table and exists at that objects's locations(pool_id). Otherwise error
// Once object at location is found, call release_memory upon obj_location -> reservation
// Remove obj_location from object -> locations and free obj_location struct. 
// If object -> locations now is empty, remove object from inventory -> fingerprints and free object struct
int release_object(Inventory * inventory, int pool_id, uint8_t * fingerprint);

// Release object from all pools, and remove from inventory -> fingerprints and free object struct
int destroy_object(Inventory * inventory, uint8_t * fingerprint)

// returns the object within fingerprint table
int lookup_object(Inventory * inventory, uint8_t * fingerprint);

#endif