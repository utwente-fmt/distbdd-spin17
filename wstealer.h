#ifndef WSTEALER_H
#define WSTEALER_H

#include "atomic.h"
#include "bddnode.h"
#include <stdbool.h>
#include <stdio.h>
#include <sys/time.h>
#include <upc_castable.h>
#include <upc_relaxed.h>

// current number of entries per thread: 2^17
// - each entry is 44 bytes, so the deque is ~5.5MB per thread
// - max number of entries currently supported: 2^35 per thread (~1.4TB per thread)
#define WS_DEQUE_SIZE 131072
#define WS_DEQUE_OFFSET (MYTHREAD * WS_DEQUE_SIZE)
#define WS_TRANS_THRESHOLD 2

// bitmaps for the task metadata field
#define TASK_STOLEN        ((uint64_t)0x8000000000000000) // 1 bit
#define TASK_EMPTY         ((uint64_t)0x4000000000000000) // 1 bit
#define TASK_TYPEFILTER    ((uint64_t)0x3FC0000000000000) // 8 bit for type filtering:
#define TASK_TYPE_ITE      ((uint64_t)0x2000000000000000) // - 1 bit
#define TASK_TYPE_RELPROD  ((uint64_t)0x1000000000000000) // - 1 bit
#define TASK_TYPE_SATCOUNT ((uint64_t)0x0800000000000000) // - 1 bit
#define TASK_TYPE_GOPAR    ((uint64_t)0x0400000000000000) // - 1 bit
#define TASK_TYPE_PAR      ((uint64_t)0x0200000000000000) // - 1 bit
#define TASK_TYPE_AND      ((uint64_t)0x0100000000000000) // - 1 bit
#define TASK_TYPE_XOR      ((uint64_t)0x0080000000000000) // - 1 bit
#define TASK_TYPE_SUPPORT  ((uint64_t)0x0040000000000000) // - 1 bit
#define TASK_THREADID      ((uint64_t)0x005FFFF800000000) // 19 bits
#define TASK_INDEX         ((uint64_t)0x00000007FFFFFFFF) // 35 bits

// bitmaps for the request cell
#define WS_REQ_EMPTY       ((uint64_t)0x0000000000000000)
#define WS_REQ_BLOCK       ((uint64_t)0x8000000000000000) // 1 bit
#define WS_REQ_OCC         ((uint64_t)0x4000000000000000) // 1 bit
#define WS_REQ_THIEF       ((uint64_t)0x3FFFFFFFFFFFFFFF) // 62 bits

// bitmaps for steals (output for ws_steal)
#define WS_STEAL_SUCC      ((uint32_t)0x80000000) // 1 bit
#define WS_STEAL_FAIL      ((uint32_t)0x40000000) // 1 bit

// macro for extracting the owner of a stolen task
#define TASK_GET_THREADID(task) ((task->metadata & TASK_THREADID) >> 35)

// macro's for spawning tasks
#define SPAWN_AND(a, b, lvl) ws_spawn(TASK_TYPE_AND, a, b, 0, lvl)
#define SPAWN_GOPAR(cur, visited, from, len) ws_spawn(TASK_TYPE_GOPAR, cur, visited, from, len)
#define SPAWN_ITE(a, b, c, lvl) ws_spawn(TASK_TYPE_ITE, a, b, c, lvl)
#define SPAWN_RELPROD(a, b, vars, lvl) ws_spawn(TASK_TYPE_RELPROD, a, b, lvl, vars)
#define SPAWN_SATCOUNT(bdd, vars, lvl) ws_spawn(TASK_TYPE_SATCOUNT, bdd, vars, 0, lvl)
#define SPAWN_SUPPORT(a) ws_spawn(TASK_TYPE_SUPPORT, a, 0, 0, 0)
#define SPAWN_XOR(a, b, lvl) ws_spawn(TASK_TYPE_XOR, a, b, 0, lvl)

// macro's for calling tasks
#define CALL_AND(a, b, lvl) ws_call(TASK_TYPE_AND, a, b, 0, lvl)
#define CALL_GOPAR(cur, visited, from, len) ws_call(TASK_TYPE_GOPAR, cur, visited, from, len)
#define CALL_ITE(a, b, c, lvl) ws_call(TASK_TYPE_ITE, a, b, c, lvl)
#define CALL_RELPROD(a, b, vars, lvl) ws_call(TASK_TYPE_RELPROD, a, b, lvl, vars)
#define CALL_SATCOUNT(bdd, vars, lvl) ws_call(TASK_TYPE_SATCOUNT, bdd, vars, 0, lvl)
#define CALL_SUPPORT(a) ws_call(TASK_TYPE_SUPPORT, a, 0, 0, 0)
#define CALL_XOR(a, b, lvl) ws_call(TASK_TYPE_XOR, a, b, 0, lvl)

// macro's for computing tasks (i.e. initiating task trees)
#define COMPUTE_PAR(bdd) ws_compute(TASK_TYPE_PAR, bdd, 0, 0, 0)
#define COMPUTE_SATCOUNT(bdd, vars, lvl) ws_compute(TASK_TYPE_SATCOUNT, bdd, vars, 0, lvl)

// macro for synchronizing on tasks
#define SYNC() ws_sync()

// remove comment to allow statistical data to be gathered
// #define WS_USE_STATS 1

#ifdef WS_USE_STATS
static uint64_t _steals = 0;
static uint64_t _steal_attempts = 0;
static uint64_t _failed_steals = 0;
static uint64_t _empty_steals = 0;
static uint64_t _blocked_steals = 0;
static uint64_t _executed = 0;

#define ADD_TO_STEALS(n) { _steals += n; }
#define ADD_TO_STEAL_ATTEMPTS(n) { _steal_attempts += n; }
#define ADD_TO_FAILED_STEALS(n) { _failed_steals += n; }
#define ADD_TO_EMPTY_STEALS(n) { _empty_steals += n; }
#define ADD_TO_BLOCKED_STEALS(n) { _blocked_steals += n; }
#define ADD_EXECUTED_TASKS(n) { _executed += n; }
#else
#define ADD_TO_STEALS(n)
#define ADD_TO_STEAL_ATTEMPTS(n)
#define ADD_TO_FAILED_STEALS(n)
#define ADD_TO_EMPTY_STEALS(n)
#define ADD_TO_BLOCKED_STEALS(n)
#define ADD_EXECUTED_TASKS(n)
#endif

typedef struct ws_input {
	uint64_t a, b, c;
	uint32_t lvl;
} ws_input_t;

typedef uint64_t ws_output_t;
typedef uint64_t ws_metadata_t;

typedef struct ws_task {
	ws_metadata_t metadata;
	ws_input_t input;
	ws_output_t output;
} ws_task_t;

typedef struct ws_comp_out {
	ws_output_t output;
	double time;
} ws_comp_out_t;

void ws_init(void *func);
void ws_spawn(ws_metadata_t metadata, uint64_t a, uint64_t b, uint64_t c, uint32_t lvl);
ws_output_t ws_call(ws_metadata_t metadata, uint64_t a, uint64_t b, uint64_t c, uint32_t lvl);
ws_output_t ws_sync();
ws_comp_out_t ws_compute(ws_metadata_t metadata, uint64_t a, uint64_t b, uint64_t c, uint32_t lvl);
ws_comp_out_t ws_initiate(ws_metadata_t metadata, uint64_t a, uint64_t b, uint64_t c, uint32_t lvl);
void ws_progress();
void ws_participate();
void ws_free();
void ws_statistics();

#endif
