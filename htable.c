#include "htable.h"

static uint64_t tablesize;
static uint64_t blockmap;
static uint64_t tableportions;
static int quadratic_probing = -1;
static int chunksize = 8;

// determine block sizes and total htable size
#define TOTAL_TABLE_SIZE (tablesize * THREADS)

#ifndef FIXED_BLOCK_SIZE
#define FIXED_BLOCK_SIZE ((uint64_t)8388608)
#endif

// determines the owner and the block of an address
#define STORAGE_BLOCK(addr) ((addr) & (FIXED_BLOCK_SIZE - 1)) 
#define STORAGE_ADDR(addr) ((addr) % TOTAL_TABLE_SIZE)

// shared index and data arrays
static shared [FIXED_BLOCK_SIZE] index_t *indextable;
static shared [FIXED_BLOCK_SIZE] bddnode_t *datatable;

// denotes the number of items inserted in the hash table
static long double inserted;
static uint64_t data_i;

// function called when idle..
static void (*idle_callback)(void);

static inline uint64_t rotl64(uint64_t x, int8_t r) {
	return ((x << r) | (x >> (64 - r)));
}

static uint64_t rehash16_mul(void *key, const uint64_t seed, size_t i) {
	const uint64_t prime = 1099511628211;
	const uint64_t *p = (const uint64_t *)key;
	
	i = (i + 1) * 6659;
	
	uint64_t hash = seed;
	hash = hash ^ (p[0] * i);
	hash = rotl64(hash * i, 47);
	hash = hash * prime;
	hash = (hash * i) ^ p[1];
	hash = rotl64(hash, 31);
	hash = hash * prime;

	return hash ^ (hash >> 32);
}

static uint64_t hash16_mul(void *key, size_t i) {
	return rehash16_mul(key, 14695981039346656037LLU, i);
}

static uint64_t hash(void *key, size_t i) {
	return hash16_mul(key, i);
}

void htable_init(void *idle_callb, uint64_t _tablesize, int _qp, int _chunksize) {
	// capture size of the BDD table
	tablesize = _tablesize;
	tableportions = tablesize / FIXED_BLOCK_SIZE;
	blockmap = tablesize - 1;
	quadratic_probing = _qp;
	chunksize = _chunksize;
	
	if (tablesize < FIXED_BLOCK_SIZE) {
		fprintf(stderr, "BDD table is too small, must at least contain 2^24 entries!\n");
		exit(EXIT_FAILURE);
	}

	// allocate the shared BDD tables
	indextable = (shared [FIXED_BLOCK_SIZE] index_t*)upc_all_alloc(THREADS, tablesize * sizeof(index_t));
	datatable = (shared [FIXED_BLOCK_SIZE] bddnode_t*)upc_all_alloc(THREADS, tablesize * sizeof(bddnode_t));
	
	// get the local pointer to our portion of the data array
	data_i = 1;
	idle_callback = idle_callb;
	inserted = 0;
}

static inline uint64_t calc_chunk_size() {
	// use a static chunk size for quadratic probing..
	if (quadratic_probing) {
		return chunksize;
	}
	
	// estimate (1 - a) with a the load-factor of the hash table
	double alpha = 1 - (inserted / tablesize);

	// estimate (1 - a)^2
	alpha *= alpha;

	// estimate the expected chunk size that should contain an empty bucket
	double size = 3.6 * ((1 + alpha) / (2 * alpha));

	// the chunk size is at least 8 and at most 2048
	size = MIN(2048, size);
	size = MAX(8, size);

	return size;
}

static inline void query_chunk(uint64_t h, uint64_t chunk_size, index_t *dest) {
	// calculate the indices of the begin and end entries in the chunk
	const uint64_t index1 = h;
	const uint64_t index2 = index1 + (chunk_size - 1);

	// determine the second 'block' for the first and last buckets of the chunk
	const uint64_t owner1 = htable_ownerid(STORAGE_ADDR(index1));
	const uint64_t owner2 = htable_ownerid(STORAGE_ADDR(index2));

	if (owner1 != owner2) {
		const uint64_t size1 = FIXED_BLOCK_SIZE - STORAGE_BLOCK(index1); 
		const uint64_t size2 = chunk_size - size1;

		upc_handle_t h1 = upc_memget_nb(dest,
			&indextable[STORAGE_ADDR(index1)], sizeof(index_t) * size1);

		upc_handle_t h2 = upc_memget_nb(&dest[size1],
			&indextable[STORAGE_ADDR(index1 + size1)], sizeof(index_t) * size2);

		while (!upc_sync_attempt(h1)) { idle_callback(); }
		while (!upc_sync_attempt(h2)) { idle_callback(); }
	}
	else {
		upc_handle_t h = upc_memget_nb(dest, &indextable[STORAGE_ADDR(index1)], sizeof(index_t) * chunk_size);

		while (!upc_sync_attempt(h)) { 
			idle_callback(); 
		}
	}
}

uint64_t htable_find_or_put(bddnode_t *key) {
	// approximate the chunk size
	const uint64_t chunk_size = calc_chunk_size();
	const uint64_t attempts = MAX(1, 4096 / chunk_size);
	ADD_TO_CSIZE(chunk_size);

	// calculate base hash
	const uint64_t _h = hash(key, 0);
	uint64_t data_addr = htable_data_index(data_i);

	// copy the given bdd node to the data table
	upc_memput(&datatable[data_addr], key, sizeof(bddnode_t));

	// allocate space on the stack for a chunk
	index_t *chunks = alloca(chunk_size * sizeof(index_t));

	// perform a maximum of 10 chunk selection attempts
	int i; for (i = 0; i < attempts; i++) {
		// calculate starting index of bucket
		uint64_t h = quadratic_probing > 0 ? hash(key, i) : _h + (chunk_size * i);

		// query for the ith chunk
		query_chunk(h, chunk_size, chunks);
		ADD_TO_RTRIPS(1);

		// iterate over the ith chunk
		int j; for (j = 0; j < chunk_size; j++) {

			if (!(chunks[j] & HT_INDEX_OCC)) {
				// find the address of the bucket in the index and data table
				uint64_t index_addr = STORAGE_ADDR(h + j);

				// construct a new bucket..
				uint64_t bucket = HT_INDEX_OCC;
				bucket |= _h & HT_INDEX_HASH;
				bucket |= data_addr & HT_INDEX_ID;

				// try to claim it with CAS
				uint64_t res = CAS64(&indextable[index_addr], chunks[j], bucket);

				if (res == chunks[j]) {
					// insert successful, update local data pointers
					inserted++;
					data_i++;

					// return the address of the new data entry
					return data_addr;
				} 
				else if ((_h & HT_INDEX_HASH) == (res & HT_INDEX_HASH)) {
					// we may have found an existing entry
					uint64_t res_addr = res & HT_INDEX_ID;
					bddnode_t node;

					// find the corresponding node
					if (!nodecache_get(res_addr, &node)) {
						htable_get_data(res_addr, &node);
					}

					// check if the entry has already been added
					if (node.high_lvl_used == key->high_lvl_used) {
						if (node.low_data_comp == key->low_data_comp) {
							return res_addr;
						}
					}
				}
			}
			else if ((_h & HT_INDEX_HASH) == (chunks[j] & HT_INDEX_HASH)) {
				// we may have found an existing entry
				uint64_t res_addr = chunks[j] & HT_INDEX_ID;
				bddnode_t node;

				// find the corresponding node
				if (!nodecache_get(res_addr, &node)) {
					htable_get_data(res_addr, &node);
				}

				// check if the entry has already been added
				if (node.high_lvl_used == key->high_lvl_used) {
					if (node.low_data_comp == key->low_data_comp) {
						return res_addr;
					}
				}
			}
		}
	}

	printf("%i/%i - error: hash table full, load-factor~%f, chunk-size=%llu, attempts=%llu, inserted=%lf\n",
		MYTHREAD, THREADS, inserted / tablesize, chunk_size, attempts, inserted);
	fflush(stdout);

	upc_global_exit(EXIT_FAILURE);
	return 0;
}

int htable_ownerid(uint64_t idx) {
	return upc_threadof(&indextable[idx]);
}

size_t htable_owner(bddnode_t *key) {
	// calculate the hash..
	const uint64_t h = STORAGE_ADDR(hash(key, 1));

	// find the owner..
	return htable_ownerid(h);
}

uint64_t htable_data_index(uint64_t i) {
	uint64_t a = i / FIXED_BLOCK_SIZE;
	uint64_t b = i % FIXED_BLOCK_SIZE;
	
	if (a >= tableportions) {
		fprintf(stderr, "BDD table (all data sections owned by thread %d) is full!\n", MYTHREAD);
		bupc_exit(EXIT_FAILURE);
	}
	
	uint64_t addr = (a * (uint64_t)THREADS * FIXED_BLOCK_SIZE) + ((uint64_t)MYTHREAD * FIXED_BLOCK_SIZE) + b;
	
	// small sanity check
	if (upc_threadof(&datatable[addr]) != MYTHREAD) {
		fprintf(stderr, "BDD table violation: data-locality is not preserved!\n");
		bupc_exit(EXIT_FAILURE);
	}
	
	return addr;
}

void htable_get_data(uint64_t idx, bddnode_t *node) {
	upc_handle_t h = htable_get_data_async(idx, node);
	while (!upc_sync_attempt(h)) { idle_callback(); }
}

upc_handle_t htable_get_data_async(uint64_t idx, bddnode_t *dest) {
	return upc_memget_nb(dest, &datatable[idx], sizeof(bddnode_t));
}

void htable_set_data(uint64_t idx, bddnode_t *node) {
	upc_memput_nbi(&datatable[idx], node, sizeof(bddnode_t));
}

shared void * htable_data_addr(uint64_t idx) {
	return &datatable[idx];
}

int htable_is_local(uint64_t idx) {
	const size_t owner = upc_threadof(&datatable[idx]);
	return bupc_thread_distance(MYTHREAD, owner) < BUPC_THREADS_NEAR;
}
