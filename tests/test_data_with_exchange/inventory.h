#ifndef INVENTORY_H
#define INVENTORY_H

#include "common.h"
#include "table.h"
#include "deque.h"
#include "fingerprint.h"

// BIG TODO: Incorporate memory pools of per-device, shared system ram, SSD, and HDD
// ALSO: Enable object to live at multiple locations

typedef struct obj_location {
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
	void * addr;
	// object might be > max message size
	uint64_t size_bytes;
	uint32_t lkey;
	// needed to acquire before checking if obj is fully available
	pthread_mutex_t inbound_lock;
	bool is_available;
	// needed to acquire to increment when starting outbound transfer and decrement when completed
	pthread_mutex_t outbound_lock;
	uint32_t outbound_inflight_cnt;
} Obj_Location;

// All objects in inventory have been registered with IB-verbs and are ready for sending/receiving
typedef struct inventory {
	uint64_t min_objects;
	uint64_t max_objects;
	// mapping from fingerprint to obj_location
	Table * obj_locations;
	// unused for now
	pthread_mutex_t inventory_lock;
} Inventory;


Inventory * init_inventory(uint64_t min_objects, uint64_t max_objects);

int put_obj_local(Inventory * inventory, uint8_t * fingerprint, void * addr, uint64_t size_bytes, uint32_t lkey);
int reserve_obj_local(Inventory * inventory, uint8_t * fingerprint, void * addr, uint64_t size_bytes, uint32_t lkey);
int lookup_obj_location(Inventory * inventory, uint8_t * fingerprint, Obj_Location ** ret_obj_location);
int copy_obj_local(Inventory * inventory, uint8_t * fingerprint, void ** ret_cloned_obj);






#endif