#ifndef BDD_H
#define BDD_H

#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include "wstealer.h"
#include "cache.h"
#include "bddnode.h"
#include "htable.h"
#include "localstore.h"
#include "varchain.h"
#include "nodecache.h"
#include <upc_nb.h>

typedef struct shared_set {
	BDD bdd;
	BDD variables;
} shared_set_t;

typedef struct set {
	BDD bdd;
	BDD variables; // all variables in the set (used by satcount)
	varchain_t *varchain;
	varchain_t **vararray;
	uint64_t varcount;
} *set_t;

struct bdd_ser {
	BDD bdd;
	uint64_t assigned;
	uint64_t fix;
};

typedef struct relation {
	BDD bdd;
	BDD variables; // all variables in the relation (used by relprod)
	varchain_t *varchain;
	varchain_t **vararray;
	uint64_t varcount;
} *rel_t;

set_t states;
rel_t *next; // each partition of the transition relation
int next_count; // number of partitions of the transition relation

void bdd_init(int cachegran, uint64_t tablesize, uint64_t cachesize, int _qp, int _chunksize);
void bdd_free();

BDDSET bdd_set_fromarray(BDDVAR* arr, size_t length);
BDDSET bdd_set_addall(BDDSET set, BDDSET set_to_add);
BDDVAR bdd_var(BDD bdd);

BDD bdd_makenode(BDDVAR level, BDD low, BDD high);
BDD bdd_makenode_local(BDDVAR level, BDD low, BDD high);
BDD bdd_ithvar(BDDVAR var);
BDD bdd_high(BDD bdd);
BDD bdd_low(BDD bdd);

BDD bdd_or(BDD a, BDD b);
BDD bdd_diff(BDD a, BDD b);
BDD bdd_set_add(BDD set, BDDVAR var);

varchain_t* bdd_to_chain(BDD bdd);
uint64_t bdd_positive_paths(BDD bdd);
	
size_t bdd_nodecount(BDD a);

#endif
