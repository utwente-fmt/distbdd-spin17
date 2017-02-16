#include "cache.h"

static uint64_t cachesize;
static uint64_t cacheportions;

#define TOTAL_CACHE_SIZE (cachesize * THREADS)

#ifndef FIXED_BLOCK_SIZE
#define FIXED_BLOCK_SIZE ((uint64_t)8388608)
#endif

// shared index and data tables
static shared [FIXED_BLOCK_SIZE] cache_index_t *cache_index;
static shared [FIXED_BLOCK_SIZE] cache_data_t *cache_data;

// the handle used to synchronize on cache requests
static upc_handle_t request_h;
static cache_index_t request_data;

// function called when idle..
static void (*idle_callback)(void);

// rotating 64-bit FNV-1a hash
static inline uint64_t cache_hash(uint64_t a, uint64_t b, uint64_t c) {
	uint64_t hash = 14695981039346656037LLU;

	hash = (hash ^ (a >> 32));
	hash = (hash ^ a) * CACHE_PRIME1;
	hash = (hash ^ b) * CACHE_PRIME1;
	hash = (hash ^ c) * CACHE_PRIME1;

	return hash;
}

void cache_init(void *idle_callb, uint64_t _cachesize) {
	// capture size of memoization table
	cachesize = _cachesize;
	cacheportions = cachesize / FIXED_BLOCK_SIZE;
	
	// allocate the shared memoization table
	cache_index = (shared [FIXED_BLOCK_SIZE] cache_index_t*)upc_all_alloc(THREADS, cachesize * sizeof(cache_index_t));
	cache_data = (shared [FIXED_BLOCK_SIZE] cache_data_t*)upc_all_alloc(THREADS, cachesize * sizeof(cache_data_t));

	// store callback function pointer
	idle_callback = idle_callb;
}

void cache_request(BDD a, BDD b, BDD c) {
	// remove lock from 'a' (if present)
	a &= ~bdd_metadata_lock;

	// find address of bucket
	const uint64_t h = cache_hash(a, b, c);
	const uint64_t i = h % TOTAL_CACHE_SIZE;
	
	// fetch index entry from table
	request_h = upc_memget_nb(&request_data, &cache_index[i], sizeof(cache_index_t));
}

void cache_sync() {
	while (!upc_sync_attempt(request_h)) { 
		idle_callback(); 
	}
}

int cache_check(BDD a, BDD b, BDD c, BDD *res) {
	cache_sync();

	if (request_data & CACHE_INDEX_OCC) {
		a &= ~bdd_metadata_lock; // remove lock (if present)

		// find hash value of cache entry
		const uint64_t h = cache_hash(a, b, c);

		if ((h & CACHE_INDEX_HASH) == (request_data & CACHE_INDEX_HASH)) {
			// find index of data entry
			const uint64_t i = request_data & CACHE_INDEX_ID;

			// obtain entry from data table
			cache_data_t entry;
			upc_handle_t h = upc_memget_nb(&entry, &cache_data[i], sizeof(cache_data_t));
			while (!upc_sync_attempt(h)) { idle_callback(); }

			// abort if locked
			if (entry.a & bdd_metadata_lock) return 0;

			// abort if key different
			if (entry.a != a || entry.b != b || entry.c != c) return 0;

			*res = entry.res;
			return 1;
		}
	}

	return 0;
}

int cache_get(BDD a, BDD b, BDD c, BDD *res) {
	cache_request(a, b, c);
	return cache_check(a, b, c, res);
}

// given some integer 'v', calculate a corresponding address in 'data' owned by this thread
uint64_t cache_data_addr(uint64_t v) {
	uint64_t a = ((v * CACHE_PRIME2) >> 32) % cacheportions;
	uint64_t b = (v >> 34) % FIXED_BLOCK_SIZE;
	return (a * (uint64_t)THREADS * FIXED_BLOCK_SIZE) + ((uint64_t)MYTHREAD * FIXED_BLOCK_SIZE) + b;
}

void cache_put(BDD a, BDD b, BDD c, BDD res) {
	a &= ~bdd_metadata_lock; // remove lock (if present)

	// find index of bucket (both of index and data table)
	const uint64_t h = cache_hash(a, b, c);
	const uint64_t i = h % TOTAL_CACHE_SIZE;
	const uint64_t data_i = cache_data_addr(h);
	
	if (upc_threadof(&cache_data[data_i]) != MYTHREAD) {
		fprintf(stderr, "Memoization table violation: data-locality is not preserved!\n");
		bupc_exit(EXIT_FAILURE);
	}

	// build an index entry
	cache_index_t entry = CACHE_INDEX_OCC;
	entry |= h & CACHE_INDEX_HASH;
	entry |= data_i & CACHE_INDEX_ID;

	// apply the lock
	cache_data[data_i].a &= bdd_metadata_lock;

	// write entry to the cache (remote)
	ATOMIC_SET(&cache_index[i], entry);

	// write cache entry to data table (local)
	cache_data[data_i].res = res;
	cache_data[data_i].c = c;
	cache_data[data_i].b = b;
	cache_data[data_i].a = a;
}
