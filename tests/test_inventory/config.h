#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"
#include "messages.h"

// FAST TREE CONFIGURATION


// Defining the parameters of the asscoicated Fast_Tables

// THESE PARAMETERS ARE VERY IMPORTANT WITH RELATION TO MEMORY USAGE
// vs. COLLISION/ALLOCATION OVERHEADS!!!

// If uniform key distribution in range [0, 2^64), then we expect each
// tree (within a given level) to contain (n / (2^(64 - tree_bits)) elements. 
// There are (2^(64 - tree_bits) == 1 << (64 - tree_bits) unique trees (meaning
// unique bases) at every level. Each insertation can append a unique element
// at every level, so without any prior we would assume that every get's added.
// The maximum number of elements that can be inserted in the tree is n = 2^64, which
// implies an expected number of elements per tree to be 2^tree_bits => every tree is full 


// NOTE: There is a special casethe special case of 0 elements vs. 1 element. 
// that this probability of (n / (2^(64 - tree_bits)) means that most trees will have 0 elements, 
// so we do not intializate tables until
// that tree was actually enountered. We set the minimum size of the tree
// to be 1.

// However, if we have knowledge about the density distribution across
// the [0, 2^64] universe, we could size our tables and set load/shrink 
// factors appropriately to better trade off memory and lookup times.

// Note: that the hash function within each table in the tree is the simple modulus
// of the table size. If we assume uniform distribution of keys across the key-space
// at each level (32, 16, 8, 8) then this is the best we can do and no need
// to be fancy. It takes care of linearly-clustered regions by default, unless there are unique 
// patterns that exist between levels

// When the last element in the table is removed, the higher level
// function will call free upon the initalized memory containing
// the 1 element item. A different approach which might be better
// in terms of "locality" would be to wait to free the table if there
// are 0 items because it is likely this table might be touched again in the near future.
// However, the computations enabled by this tree are already fast, so will
// air towards the side of conserving memory usage within less table space
// at the epensive of potentially more collisions (setting min size to 1) 
// and reallocation (free immediately)

// Can also dynamically change the load/shrink factor within a specific tree if 
// it is particularly hot within that level. (e.g. if we insert all 2^32 elements
// in the range [0, 2^32) then root.child[0]'s table will be compleletly full
// (that is it will have all 2^16 elements, and we could opitimize memory/compute
// tradeoff (collisions/resizing parameters) if we had known this key distribution as a prior, 
// then we wouldn't have to pay the cost of ceil(2^16 / LOG_alpha(min_size)) (where the log base alpha = 1 / load_factor).
// We either could have started with a higher initial minimum size, or have set the load factor to be lower.
// If we incorporate removals and the impact of the shrink factor (which is a tradeoff of reduced memory
// for price of more collisions/additional allocation), then the analysis of overhead costs because more complex
// the interleaved ordering of inserts/deletes matters (worst-case is when there are repeated inserts follow by 
// repeated deletes, i.e. the table grows from min to max, shrinks from max to min, grows from min to max, etc.).


// A lower load factor more aggressively grows the table
// A higher shrink factor more aggressively shrinks the table 

// LOAD FACTOR:
// if ((new insertion) && ((size < max_size) && (cnt > size * load_factor)):
//	- new_size = min(size * (1 / load_factor), max_size)

// Examples:

// If load factor = 0.5:
//	- Doubles when new insert triggered more than half-full
// If laod factor = 0.75:
//	- Increases by factor of 4/3 when 3/4 full.
// If load factor = 0.25:
//	- Quadruples when more than 1/4 full.


// SHRINK FACTOR:
// if ((new removal) && ((size > min_size) & (cnt < size * (1 - load_factor)):
//	- new size = max(size * (1 - shrink_factor), min_size)

// Examples:

// If shrink factor = 0.5:
//	- Halves when new removal when new removal triggered more than half-empty
// If shrink factor = 0.75:
//	- Decreased by factor of 4 (quartered) when 1/4 empty
// If load factor = 0.25:
//	- Decreases by factor of 4/3 when 3/4 empty


// There is interplay between the load/shrink factors and boundary sizes of the table, so
// they must all be set in tandem to make sense and work well (to prevent repeatedly bouncing
// between growing/shrinking to max/min respectively). The purpose of seperating load from 
// shrink is to enable flexibility for uses that have known patterns of interleaved
// inserts and deletes. 

// For example:

// If we know that we will be inserting a lot and then deleting, we can
// set a lower load factor (to more aggressively grow) and a higher shrink factor (once we start deleting, 
// then shrink quickly)

// If we think that we will be producing at an even rate of consumption we can set the load factors
// to be even 

// If we think that we will producing a lot and not comsuing as much (meainging just steady growth),
// then we can set the load factor to be higher than the shrink factor.


// Be careful about boundary conditions when min table size is small. 
//	- Within Fast_Tree we will have a ton of tables each with small minimum
//		size (most trees will be of cnt = 1 if uniform distribution of keys),
// 		so important to understand the analysis of early growth/shrinking behavior.

// In the exmaple below you will see:
// When cnt is 1, 2, 3, or 4 and removal will cause a shrink.
// So we could get stuck in continous re-sizing loop if insert/delete/insert/delete
// access pattern.

// Thus setting the shrink factor be less aggressive to something
// like .25 makes sense if we assume most tables will be small, 
// but still want to be conservative with memory usage


// Example of Combination:

// If load_factor = 0.5 and shrink factor = 0.5;
// 	- Implies the table will always be half full, if even probably
//		of a given insert and delete. 

 
// The early cases example assuming min size = 1.

// This maintains invariant that current cnt <= current size

//	- a.) Initially cnt = 0, size = 1
//	- b.) If current cnt = 0
//			- First insert grows table to cnt = 1, size = 2
//			- Removal not possible
//	- c.) If current cnt = 1:
//			- An insert causes growth to cnt = 2, size = 4
//			- A removal causes shrink back to b.)
//	- d.) If current cnt = 2:
//			- An insert causes growth to cnt = 3, size = 8
//			- A removal causes shrink back to c.)


//	NOW THINGS CHANGE. 
//	From here there are two paths to get to each subsequent count level.
// 	Could reach these levels depending on the ordering of previous resizes
//	 (meaning if there was either a string of inserts or string of 
//		remvovals leading up to the current point)

// 

//	- e.) If current cnt = 3:
//			- If insert and size = 4 (came from shrinking in f.), then growth, now cnt = 4, size = 8
//			- If insert and size = 8 (came from growth in d, or shrink in f.), then don't resize. now cnt = 4, size = 8
//			- If removal the shrink back to d.)

// Note: Special cases for powers of two under this configuration. The up and down paths
//			lead to only one possible (cnt, size) combination if cnt is a power of two

//	- f.) If current cnt = 4:
//			- An insert insert causes growth to cnt = 5, size = 16
//			- A removal causes shrink to cnt = 3, size = 4


//	- g.) If current cnt 5:
//			- An insert if size = 8 (came from shrinking in h.) causes growth to cnt = 6, size = 16
//			- An insert if size = 16 (came from growth in f) doesn't change size and cnt = 6, size = 16
//			- A removal if size = 8, (came from shrinking in h.) dont resize and now cnt = 4, size = 8
//			- A removal if size = 16, (came from growth in f.) shrink to cnt = 4, size = 8
//	- h.) If current cnt 6:
//			- An insert if size = 8 (came from shrinking in i.) causes growth, cnt = 7, size = 16
//			- An insert if size = 16 (came from g.) dont resize, cnt = 7, size = 16
//			- A removal if size = 8 (came from shrinking in i.), dont resize, cnt = 5, size = 16
//			- A removal if size = 16 (came from g.), causes shrink, cnt = 5, size = 8
//	- i.) If current cnt 7:
//			- An insert if size = 8 (came from shrinking in j.), causes growth, cnt = 8, size = 16
//			- An insert if size = 16 (came from h.), dont resize, cnt = 8, size = 16
//			- A removal if size = 8 (came from shrinking in j.), dont resize cnt = 6, size = 8
//			- A remval if size = 16 (came from h.), causes shrink, cnt = 7, size = 8

//	- j.) If current cnt 8:
//			- An insert causes gwowth to cnt = 9, size = 32
//			- A removal causes shrink to cnt = 7, size = 16

//	- etc.....

// The early cases of load_factor = 0.5 and shrink factor = 0.25 look much cleaner
// in terms of still trying to be conservative with memory, but being less volatile
// in terms of continuously resizing and paying the overhead of doing so.

// Each of these tables are embedded within their parent
// (i.e. a FAST_TREE_32 table is actually within root, a FAST_TREE_16 table is within FAST_TREE_32, etc.)

// Note that the minimum size is set to 2, because all trees are intiailized with at least 1 element,
// so after the first isnertation they would immeidately grow to size 2 no matter what. When the table
// becomes empty it gets freed and removed from the parent table.





// MEMORY CONFIGURATION

// if a request cannot be satisfied on a given pool, then client
// can specify if they want reservation to be retried on other
// pools and can specify up this MEMORY_MAX_BACKUP_POOLS for retrying
#define MEMORY_MAX_BACKUP_POOLS 16

// this is the size of the Fifo where non-memory threads will produce
// on, and that memory thread will consume on

// setting this higher is just an optimization to prevent contention
// for multiple clients submitting to same mempool fifo
#define MEMORY_OPS_BUFFER_MAX_REQUESTS_PER_MEMPOOL 64

// this is to make allocations faster for lists
// that containing starting chunk_id's of the same range size

// each list represent a fixed memory range size, so we actually
// know the maximum capacity, but this could be an undue waste of memory
// setting higher can accelerate list operations because no need for malloc()/frees()
#define MEMORY_RANGE_LIST_DEFAULT_BUFFER_CAPACITY 1


#define MEMORY_RANGE_LISTS_MIN_TABLE_SIZE 256
#define MEMORY_RANGE_LISTS_LOAD_FACTOR 0.5
#define MEMORY_RANGE_LISTS_SHRINK_FACTOR 0.25


// the max table size is the number of chunks
#define MEMORY_ENDPOINT_MIN_TABLE_SIZE 256
#define MEMORY_ENDPOINT_LOAD_FACTOR 0.5
#define MEMORY_ENDPOINT_SHRINK_FACTOR 0.25


#define FAST_TREE_32_MIN_TABLE_SIZE 2
// can contain up to 2^32 entires, when table reaches MAX size the re-hashing will have no collisions 
// because of the modulus hash function across all 2^32 unique keys
#define FAST_TREE_32_MAX_TABLE_SIZE 1UL << 32
#define FAST_TREE_32_LOAD_FACTOR 0.5
#define FAST_TREE_32_SHRINK_FACTOR 0.25

// can contain up to 2^16 entries
#define FAST_TREE_16_MIN_TABLE_SIZE 2
#define FAST_TREE_16_MAX_TABLE_SIZE 1UL << 16
#define FAST_TREE_16_LOAD_FACTOR 0.5
#define FAST_TREE_16_SHRINK_FACTOR 0.25

// can contain up to 2^8 entries
#define FAST_TREE_OUTWARD_LEAF_MIN_TABLE_SIZE 2
#define FAST_TREE_OUTWARD_LEAF_MAX_TABLE_SIZE 1UL << 8
#define FAST_TREE_OUTWARD_LEAF_LOAD_FACTOR 0.5
#define FAST_TREE_OUTWARD_LEAF_SHRINK_FACTOR 0.25


// THIS IS THE TABLE WITHIN THE LEAVES OF THE
// MAIN TREE!
// and contains extra information such as 
// base, another fast table, deque
#define FAST_TREE_MAIN_LEAF_MIN_TABLE_SIZE 2
#define FAST_TREE_MAIN_LEAF_MAX_TABLE_SIZE 1UL << 8
#define FAST_TREE_MAIN_LEAF_LOAD_FACTOR 0.5
#define FAST_TREE_MAIN_LEAF_SHRINK_FACTOR 0.25

// The key is the offset within the leaf.
// (i.e. the lower 8 bits of the uint64_t key)
// The value is the value that was inserted associated
// with corresponding uint64_t key
#define FAST_TREE_VALUE_MIN_TABLE_SIZE 2
#define FAST_TREE_VALUE_MAX_TABLE_SIZE 1UL << 8
#define FAST_TREE_VALUE_LOAD_FACTOR 0.5
#define FAST_TREE_VALUE_SHRINK_FACTOR 0.25



// Not using skiplist anymore...

/*
// higher level factor -> more memory usage, 
//	but potentially more items to skip over (but also could lead to more traversing)
#define MEMORY_SKIPLIST_LEVEL_FACTOR 0.5
// consider un-defining this and also having it be a function of num_chunks...?
#define MEMORY_SKIPLIST_MIN_ITEMS_TO_CHECK_REAP 4096
// higher ratio => reaping less, which could benefit in not stopping-the-world as much
// but also lead to slower search times due to traversing zombied items
#define MEMORY_SKIPLIST_MAX_ZOMBIE_RATIO 0.25
// max levels will be set to log (num_chunks)

#define MEMORY_SKIPLIST_ITEM_SLAB_CAPACITY 1U << 16

// there may be race conditions (particularly at system startup time)
// where an excess reservation is taken and another thread tries to reserve
// but the excess as been re-inserted. Having a lock is too degredating for performance
// so retry is better.

// Different option would be to a set a maximum range size, so there is less contention for breaking
// up large ranges at the beginning before system stabilizes
#define MEMORY_RESERVATION_ATTEMPT_CNT 3
*/


// NOTE: THE "MASTER" PROCESS AND ALL WORKERS NEED TO AGREE ON THIS CONFIG!

#define MASTER_NODE_ID 0

// used within:
//	- Master's run_join_net_server()
//	- Worker's join_net -> connect_to_master()
#define JOIN_NET_PORT 9123
// set timeout to be 100 ms for a worker to try joining net again
#define JOIN_NET_TIMEOUT_MICROS 100000

// used within:
//	- init_master starting its rdma_init server
//	- within processing join_net on the worker side to connect to
#define MASTER_RDMA_INIT_PORT 9184

//  used within:
//	- Worker node's creation of RDMA_INIT tcp server within processing join_net
//	- Worker node's connecting to other worker RDMA_INIT server within init_net function
#define WORKER_RDMA_INIT_PORT 9272
// set timeout to be 100 ms for a worker to try to connect to other rdma_init server again
#define RDMA_INIT_TIMEOUT_MICROS 100000


// SELF_NET CONFIGURATION


// BID TODO: fix this configuration of queue sizes to be more flexible!!!

#define SRQ_MAX_WR 1U << 14
#define SRQ_MAX_SGE 2

#define QP_MAX_SEND_WR 1U << 10
#define QP_MAX_SEND_SGE 2

#define QP_MAX_RECV_WR 1U << 14
#define QP_MAX_RECV_SGE 2

#define QP_MAX_INLINE_DATA 0 // SEEMS LIKE 956 is the max for qp creation error...?


// CONTROL 

#define RECV_CTRL_MAX_POLL_ENTRIES 1U << 14
#define SEND_CTRL_MAX_POLL_ENTRIES 1U << 10

#define CTRL_RECV_DISPATCHER_BACKLOG_MESSAGES 1U << 21


// MASTER CLASS CONFIGURATION

#define NUM_MASTER_WORKER_THREADS 1
#define MASTER_WORKER_MAX_TASKS_BACKLOG 4096



// EXCHANGE CLASS CONFIGURATION

#define NUM_EXCHANGE_WORKER_THREADS 1
#define EXCHANGE_WORKER_MAX_TASKS_BACKLOG 1U << 16


// EXCHANGE CONFIGURATION

#define EXCHANGE_MIN_BID_TABLE_ITEMS 1UL << 6
#define EXCHANGE_MAX_BID_TABLE_ITEMS 1UL << 20

#define EXCHANGE_MIN_OFFER_TABLE_ITEMS 1UL << 6
#define EXCHANGE_MAX_OFFER_TABLE_ITEMS 1UL << 30

#define EXCHANGE_MIN_FUTURE_TABLE_ITEMS 1UL << 6
#define EXCHANGE_MAX_FUTURE_TABLE_ITEMS 1UL << 20


// the load factor and shrink factor only matter if min_size != max_size
#define EXCHANGE_TABLES_LOAD_FACTOR 0.5f
#define EXCHANGE_TABLES_SHRINK_FACTOR 0.1f



// INVENTORY CLASS CONFIGURATION

#define NUM_INVENTORY_WORKER_THREADS 1
#define INVENTORY_WORKER_MAX_TASKS_BACKLOG 1U << 16


// INVENTORY CONFIGURATION

#define MAX_FINGERPRINT_MATCH_LOCATIONS 24


#endif


	