#ifndef CACHE_H
#define CACHE_H

#include "atomic.h"
#include "bddnode.h"
#include <stdio.h>
#include <upc_relaxed.h>
#include <upc_castable.h>
#include <upc_nb.h>

// different types of cache operations
#define CACHE_ITE 0
#define CACHE_RELNEXT 1
#define CACHE_AND 2
#define CACHE_XOR 3
#define CACHE_SATCOUNT 4
#define CACHE_SUPPORT 5

#define CACHE_PRIME1 ((uint64_t)1099511628211)
#define CACHE_PRIME2 ((uint64_t)314690833)

// bitmaps for the 'cache_index' table
#define CACHE_INDEX_OCC  ((uint64_t)0x8000000000000000) // 1 bit
#define CACHE_INDEX_HASH ((uint64_t)0x7FFFFC0000000000) // 21 bits
#define CACHE_INDEX_ID   ((uint64_t)0x000003FFFFFFFFFF) // 42 bits

// 8 byte indices to cache entries
typedef uint64_t cache_index_t; 

// 32 byte memoization cache entries
typedef struct cache_entry {
	BDD a, b, c, res;
} cache_data_t; 

void cache_sync();
void cache_init(void *idle_callb, uint64_t _cachesize);
void cache_request(BDD a, BDD b, BDD c);
int cache_check(BDD a, BDD b, BDD c, BDD *res);
int cache_get(BDD a, BDD b, BDD c, BDD *res);
void cache_put(BDD a, BDD b, BDD c, BDD res);

#endif
