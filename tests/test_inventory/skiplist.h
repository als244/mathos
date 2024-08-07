#ifndef SKIPLIST_H
#define SKIPLIST_H

#include "common.h"
#include "deque.h"


typedef struct skiplist {
	int max_levels;
} Skiplist;


Skiplist * init_skiplist(int max_levels);


#endif