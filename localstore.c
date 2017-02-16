#include "localstore.h"

static bddnode_t localstore[LOCALSTORE_SIZE];

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

uint64_t localstore_find_or_put(bddnode_t *key) {
	uint64_t i; for (i = 0; i < LOCALSTORE_SIZE; i++) {
		// perform quadratic probing
		uint64_t index = hash(key, i) & LOCALSTORE_MASK;

		if (localstore[index].high_lvl_used == 0 && localstore[index].low_data_comp == 0) {
			localstore[index] = *key;
			return index;
		}
		else if (localstore[index].high_lvl_used == key->high_lvl_used) {
			if (localstore[index].low_data_comp == key->low_data_comp) {
				return index;
			}
		}
	}

	printf("localstore is full, allocate more entries!\n");
	exit(1);

	return 0;
}

void localstore_retrieve(uint64_t index, bddnode_t* dest) {
	//assert(index < LOCALSTORE_SIZE);
	*dest = localstore[index];
}

void localstore_set_data(uint64_t index, bddnode_t* dest) {
	//assert(index < LOCALSTORE_SIZE);
	localstore[index] = *dest;
}
