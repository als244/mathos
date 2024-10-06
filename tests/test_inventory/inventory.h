#ifndef INVENTORY_H
#define INVENTORY_H

#include "common.h"
#include "table.h"
#include "fifo.h"
#include "deque.h"
#include "fingerprint.h"
#include "inventory_messages.h"
#include "work_pool.h"

#include "memory.h"
#include "memory_client.h"
#include "utils.h"

typedef struct object Object;

typedef struct obj_location {
	// identifies the size of object, and chunk ids
	Mem_Reservation * reservation;
	void * buffer;
	// needed to acquire before checking if obj is fully available
	pthread_mutex_t inbound_lock;
	bool is_available;
	// needed to acquire to increment when starting outbound transfer and decrement when completed
	pthread_mutex_t outbound_lock;
	uint32_t outbound_inflight_cnt;
	// backwards pointer to Obj so we can update number of reserved locations
	// (helpful for debugging)
	int pool_id;
	Object * obj;
} Obj_Location;

// BY Construction, each pool only contains 1 copy of the object
struct object {
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	uint64_t size_bytes;
	int num_reserved_locations;
	// mapping from pool id => obj_location
	// null if doesn't exit in pool
	Obj_Location * locations;
};

typedef struct outstanding_bid {
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	uint64_t content_size;
	int preferred_pool_id;
} Outstanding_Bid;

typedef struct inventory {
	Memory * memory;
	// convenient to just have even though embedded within memory struct
	int num_pools;
	// mapping from fingerprint -> obj
	Table * object_table;
	// TODO: think about "where" outstanding bids fit it? In inventory? In exchange client? In schedule? On its own?
	// FOR NOW...
	// mapping from fingerprint -> outstanding bid
	//	- we will not reserve memory until we have a fingerprint match
	//	- upon figerprint match we will remove the outstanding bid and create
	//		an object with backing memory reservation
	Table * outstanding_bids;
} Inventory;

Inventory * init_inventory(Memory * memory);

int do_inventory_function(Inventory * inventory, int thread_id, Ctrl_Message * ctrl_message, uint32_t * ret_num_ctrl_messages, Ctrl_Message ** ret_ctrl_messages);
void print_inventory_message(uint32_t node_id, WorkerType worker_type, int thread_id, Ctrl_Message * ctrl_message);


// These FUNCTIONS SHOULDN'T BE EXPOSED....

// returns 0 upon success, otherwise error

// Responsible for first checking if fingerprint is in inventory -> fingerprints. If not, allocate object and insert
// Allocates an object location and populates it with the mem_reservation returned from reserve_memory
// Inserts the object location into object -> locations
// Populates ret_obj_location
int reserve_object(Inventory * inventory, uint8_t * fingerprint, int pool_id, uint64_t size_bytes, int num_backup_pools, int * backup_pool_ids, int mem_client_id, Obj_Location ** ret_obj_location);

// Responsbile for checking if fingerprint exists in fingerprint table and exists at that objects's locations(pool_id). Otherwise error
// Once object at location is found, call release_memory upon obj_location -> reservation
// Remove obj_location from object -> locations and free obj_location struct. 
// If object -> locations now is empty, remove object from inventory -> fingerprints and free object struct
int release_object(Inventory * inventory, uint8_t * fingerprint, Obj_Location * obj_location, int mem_client_id);

// Release object from all pools, and remove from inventory -> fingerprints and free object struct
int destroy_object(Inventory * inventory, uint8_t * fingerprint, int mem_client_id);

// returns the object within fingerprint table
int lookup_object(Inventory * inventory, uint8_t * fingerprint, Object ** ret_object);



#endif
