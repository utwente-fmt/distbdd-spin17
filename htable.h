#ifndef HTABLE_H
#define HTABLE_H

#include "atomic.h"
#include "bddnode.h"
#include "nodecache.h"
#include <stdio.h>
#include <bupc_extensions.h>
#include <upc_castable.h> 
#include <upc_relaxed.h>
#include <upc_nb.h>

// bitmaps for the 'index' table
#define HT_INDEX_OCC    ((uint64_t)0x8000000000000000) // 1 bit
#define HT_INDEX_HASH   ((uint64_t)0x7FFFFC0000000000) // 21 bits
#define HT_INDEX_ID     ((uint64_t)0x000003FFFFFFFFFF) // 42 bits

// allow statistical data to be gathered
// #define HTABLE_USE_STATS 1

#ifdef HTABLE_USE_STATS
extern uint64_t _rtrips;
extern uint64_t _csize;

#define ADD_TO_RTRIPS(n) { _rtrips += n; }
#define ADD_TO_CSIZE(n) { _csize += n; }
#else
#define ADD_TO_RTRIPS(n)
#define ADD_TO_CSIZE(n)
#endif

typedef uint64_t index_t;

void htable_init(void *idle_callb, uint64_t _tablesize, int _qp, int _chunksize);
uint64_t htable_find_or_put(bddnode_t *key);
size_t htable_owner(bddnode_t *key);
void htable_get_data(uint64_t index, bddnode_t *node);
upc_handle_t htable_get_data_async(uint64_t index, bddnode_t *dest);
void htable_set_data(uint64_t index, bddnode_t *node);
int htable_is_local(uint64_t index);
uint64_t htable_data_index(uint64_t i);

#endif
