#ifndef LOCALSTORE_H
#define LOCALSTORE_H

#include "htable.h"

// the number of elements in the 'localstore' table: 2^22
#define LOCALSTORE_SIZE 4194304

// bitmap for the 'localstore' table: 22 bits
#define LOCALSTORE_MASK ((uint64_t)0x00000000003FFFFF)

uint64_t localstore_find_or_put(bddnode_t *key);
void localstore_retrieve(uint64_t index, bddnode_t* dest);
void localstore_set_data(uint64_t index, bddnode_t* dest);

#endif
