#include "bdd.h"

static int granularity = 1;

static inline BDD node_lowedge(bddnode_t *node) {
	BDD low = NODE_LOW(node); // obtain low edge
	if (NODE_IS_LOCAL_LOW(node)) low |= bdd_metadata_local; // preserve data locality
	return low;
}

static inline BDD node_highedge(bddnode_t *node) {
	BDD high = NODE_HIGH(node) | (NODE_COMP(node) ? bdd_complement : 0LL); // obtain high edge
	if (NODE_IS_LOCAL_HIGH(node)) high |= bdd_metadata_local; // preserve data locality
	return high;
}

static inline BDD node_low(BDD bdd, bddnode_t *node) {
	return bdd_transfermark(bdd, node_lowedge(node));
}

static inline BDD node_high(BDD bdd, bddnode_t *node) {
	return bdd_transfermark(bdd, node_highedge(node));
}

// calculate the union of 'a' and 'b'
BDD bdd_or(BDD a, BDD b) {
	return bdd_not(CALL_AND(bdd_not(a), bdd_not(b), 0));
}

// calculate the difference of 'a' and 'b': a/b = {x in a | x not in b}
BDD bdd_diff(BDD a, BDD b) {
	return CALL_AND(a, bdd_not(b), 0);
}

BDD bdd_set_add(BDD set, BDDVAR var) {
	return CALL_AND(set, bdd_ithvar(var), 0);
}

static inline upc_handle_t node_query_intern(BDD bdd, bddnode_t *node, bool allowcache) {
	// skip constant BDDs
	if (bdd_isconst(bdd)) {
		return NULL; 
	}
	
	BDD ref = bdd_strip_mark_metadata(bdd);
	
	if (bdd_islocal(bdd)) {
		localstore_retrieve(ref, node);
		return NULL; // receive from localstore
	}
	
	if (allowcache) {
		if (nodecache_get(ref, node)) {
			return NULL; // receive from cache
		}
	}
	
	 // receive from remote 'data' table
	return htable_get_data_async(ref, node);
}

static upc_handle_t node_query(BDD bdd, bddnode_t *node) {
	return node_query_intern(bdd, node, true);
}

static upc_handle_t node_query_nocache(BDD bdd, bddnode_t *node) {
	return node_query_intern(bdd, node, false);
}

static inline void node_sync_intern(upc_handle_t h, BDD bdd, bddnode_t *node, bool allowcache) {
	if (h != NULL) {
		while (!upc_sync_attempt(h)) ws_progress(); // wait for completion
		if (allowcache) nodecache_put(bdd_strip_mark_metadata(bdd), node); // cache received node
	}
}

static void node_sync(upc_handle_t h, BDD bdd, bddnode_t *node) {
	node_sync_intern(h, bdd, node, true);
}

static void node_sync_nocache(upc_handle_t h, BDD bdd, bddnode_t *node) {
	node_sync_intern(h, bdd, node, false);
}

static void node_update(BDD bdd, bddnode_t *node) {
	if (bdd_islocal(bdd)) localstore_set_data(bdd_strip_mark_metadata(bdd), node);
	else htable_set_data(bdd_strip_mark_metadata(bdd), node);
}

// follow the low edge of the given BDD node
BDD bdd_low(BDD bdd) {
	if (bdd_isconst(bdd)) return bdd; // terminal case
	bddnode_t node;
	upc_handle_t h = node_query_nocache(bdd, &node); // query for the node
	node_sync_nocache(h, bdd, &node); // receive node
	return node_low(bdd, &node); // return low edge
}

// follow the high edge of the given BDD node
BDD bdd_high(BDD bdd) {
	if (bdd_isconst(bdd)) return bdd; // terminal case
	bddnode_t node;
	upc_handle_t h = node_query_nocache(bdd, &node); // query for the node
	node_sync_nocache(h, bdd, &node); // receive node
	return node_high(bdd, &node); // return high edge
}

BDDVAR bdd_var(BDD bdd) {
	if (bdd_isconst(bdd)) {
		fprintf(stderr, "ERROR: BDD cannot be constant\n");
		exit(EXIT_FAILURE);
	}
	
	bddnode_t node;
	upc_handle_t h = node_query_nocache(bdd, &node); // query for the node
	node_sync_nocache(h, bdd, &node); // receive node
	return NODE_LEVEL(&node); // return level
}

BDD bdd_ithvar(BDDVAR lvl) {
	return bdd_makenode(lvl, bdd_false, bdd_true);
}

uint64_t bdd_nodecount_do_1(BDD bdd) {
	// terminal case
	if (bdd_isconst(bdd)) return 0; 

	// retrieve node
	bddnode_t node;
	upc_handle_t h = node_query_nocache(bdd, &node); 
	node_sync_nocache(h, bdd, &node); 

	// skip node if already marked
	if (node.low_data_comp & MASK_DATA_MARK) return 0; 

	// apply mark + update node
	node.low_data_comp |= MASK_DATA_MARK;
	node_update(bdd, &node); 

	// recursively count descendants
	uint64_t result = 1;
	result += bdd_nodecount_do_1(node_lowedge(&node));
	result += bdd_nodecount_do_1(node_highedge(&node));
	return result;
}

void bdd_nodecount_do_2(BDD bdd) {
	// terminal case
	if (bdd_isconst(bdd)) return;

	// receive node
	bddnode_t node;
	upc_handle_t h = node_query_nocache(bdd, &node);
	node_sync_nocache(h, bdd, &node); 

	// skip if not marked..
	if ((node.low_data_comp & MASK_DATA_MARK) == 0) return;

	// remove mark + update node
	node.low_data_comp &= ~MASK_DATA_MARK;
	node_update(bdd, &node); 

	// recursively unmark descendants
	bdd_nodecount_do_2(node_lowedge(&node));
	bdd_nodecount_do_2(node_highedge(&node));
}

size_t bdd_nodecount(BDD a) {
	uint32_t result = bdd_nodecount_do_1(a);
	bdd_nodecount_do_2(a); // undo all marks
	return result;
}

inline int bdd_newnode(BDDVAR lvl, BDD low, BDD high, bddnode_t *node) {	
	// fill in node information
	node->high_lvl_used = NODE_SET_HIGH(high) | NODE_SET_LVL(lvl);
	node->low_data_comp = NODE_SET_LOW(low);

	// preserve data-locality
	if (bdd_islocal(high)) node->high_lvl_used |= MASK_LOCAL_HIGH;
	if (bdd_islocal(low)) node->low_data_comp |= MASK_LOCAL_LOW;
	
	// update the complement bit
	int mark;
	if (bdd_hasmark(low)) {
		mark = 1;
		node->low_data_comp |= (bdd_hasmark(high) ? 0 : MASK_COMP);
	} else {
		mark = 0;
		node->low_data_comp |= (bdd_hasmark(high) ? MASK_COMP : 0);
	}
		
	return mark;
}

BDD bdd_makenode(BDDVAR lvl, BDD low, BDD high) {
	// keep the BDD reduced
	if (bdd_strip_metadata(low) == bdd_strip_metadata(high)) return low;
	
	// construct a fresh node
	bddnode_t node;
	int mark = bdd_newnode(lvl, low, high, &node);

	// store and return node
	BDD result = htable_find_or_put(&node);
	return mark ? (result | bdd_complement) : result;
}

BDD bdd_makenode_local(BDDVAR lvl, BDD low, BDD high) {
	// keep the BDD reduced
	if (bdd_strip_metadata(low) == bdd_strip_metadata(high)) return low;
	
	// construct a fresh node
	bddnode_t node;
	int mark = bdd_newnode(lvl, low, high, &node);

	// store and return node
	BDD result = localstore_find_or_put(&node) | bdd_metadata_local;
	return mark ? (result | bdd_complement) : result;
}

static inline double bdd_set_count(varchain_t *current) {
	double result = 0;

	while (current != NULL) {
		current = current->next;
		result++;
	}

	return result;
}

varchain_t* bdd_to_chain(BDD bdd) {
	// terminal case
	if (bdd_isconst(bdd)) return NULL;
	
	// retrieve the node
	bddnode_t node;
	upc_handle_t h = node_query(bdd, &node);
	node_sync(h, bdd, &node);
	
	// allocate a new node in the chain
	varchain_t *cur = (varchain_t*)malloc(sizeof(varchain_t));
	
	// recursive computation
	cur->level = NODE_LEVEL(&node);
	cur->next = bdd_to_chain(node_high(bdd, &node));
	return cur;
}

// calculates the number of paths that lead to 'true'
uint64_t bdd_positive_paths(BDD bdd) {
	// terminal cases
	if (bdd_isfalse(bdd)) return 0;
	if (bdd_istrue(bdd)) return 1;
	
	// retrieve the node
	bddnode_t node;
	upc_handle_t h = node_query(bdd, &node);
	node_sync(h, bdd, &node);
	
	// recursive computation
	uint64_t high = bdd_positive_paths(node_highedge(&node));
	uint64_t low = bdd_positive_paths(node_lowedge(&node));
	
	// yield result
	return high + low;
}

uint64_t bdd_support(BDD bdd) {
	// terminal case
	if (bdd_isconst(bdd)) return bdd_true;
	
	// consult the cache
	uint64_t result;
	if (cache_get(bdd_set_data(bdd, CACHE_SUPPORT), 0, 0, &result)) {
		return result;
	}
	
	// retrieve the node
	bddnode_t node;
	upc_handle_t h = node_query(bdd, &node);
	node_sync(h, bdd, &node);
	BDD high, low, set;
	
	// recursive computation 
	SPAWN_SUPPORT(node_lowedge(&node));
	high = CALL_SUPPORT(node_highedge(&node));
	low = SYNC();
	
	// take intersection of support of low and support of high
	set = CALL_AND(low, high, 0);
	
	// add current level to set
	result = bdd_makenode(NODE_LEVEL(&node), bdd_false, set);
	
	// update the cache and return
	cache_put(bdd_set_data(bdd, CACHE_SUPPORT), 0, 0, result);
	return result;
}

uint64_t bdd_satcount(BDD bdd, uint64_t vars, BDDVAR prev_lvl) {
	if (bdd_isfalse(bdd)) return 0; // trivial case

	varchain_t *variables = NULL;
	if (vars < states->varcount) {
		variables = states->vararray[vars];
	}

	if (bdd_istrue(bdd)) return pow(2, bdd_set_count(variables)); // trivial case

	// count variables
	double skipped = 0;
	BDDVAR var = bdd_var(bdd);
	while (var != variables->level) {
		skipped++;
		variables = variables->next;
		assert(variables != NULL);
		vars++;
	}

	// consult the cache
	uint64_t result;
	int cachenow = granularity < 2 || prev_lvl == 0 ? 1 : prev_lvl / granularity != var / granularity;
	if (cachenow) {
		if (cache_get(bdd_set_data(bdd, CACHE_SATCOUNT), vars, bdd_false, &result)) {
			return result * pow(2, skipped);
		}
	}

	// recursive computation 
	SPAWN_SATCOUNT(bdd_high(bdd), vars+1, var);
	uint64_t low = CALL_SATCOUNT(bdd_low(bdd), vars+1, var);
	uint64_t high = SYNC();
	result = low + high;
	
	// cache and return result
	if (cachenow) cache_put(bdd_set_data(bdd, CACHE_SATCOUNT), vars, bdd_false, result);
	return result * pow(2, skipped);
}

BDD bdd_and(BDD a, BDD b, BDDVAR prev_lvl) {
	// terminal cases
	if (bdd_istrue(a)) return b;
	if (bdd_istrue(b)) return a;
	if (bdd_isfalse(a)) return bdd_false;
	if (bdd_isfalse(b)) return bdd_false;
	if (bdd_strip_metadata(a) == bdd_strip_metadata(b)) return a;
	if (bdd_strip_metadata(a) == bdd_not(bdd_strip_metadata(b))) return bdd_false;

	// improve for caching
	if (bdd_strip_mark_metadata(a) > bdd_strip_mark_metadata(b)) {
		BDD t = b; b = a; a = t;
	}
	
	// retrieve nodes
	bddnode_t na, nb;
	upc_handle_t ha = node_query(a, &na);
	upc_handle_t hb = node_query(b, &nb);
	node_sync(ha, a, &na);
	node_sync(hb, b, &nb);

	// find lowest level
	BDDVAR va = NODE_LEVEL(&na);
	BDDVAR vb = NODE_LEVEL(&nb);
	BDDVAR level = va < vb ? va : vb;

	// consult the cache
	BDD result;
	int cachenow = (granularity < 2) || (prev_lvl == 0) ? 1 : (prev_lvl / granularity) != (level / granularity);
	if (cachenow) {
		if (cache_get(bdd_set_data(a, CACHE_AND), b, bdd_false, &result)) {
			return result;
		}
	}

	// get cofactors 
	BDD aLow = a, aHigh = a;
	BDD bLow = b, bHigh = b;

	if (level == va) {
		aLow = node_low(a, &na);
		aHigh = node_high(a, &na);
	}

	if (level == vb) {
		bLow = node_low(b, &nb);
		bHigh = node_high(b, &nb);
	}

	// recursive computation 
	BDD low = bdd_invalid, high = bdd_invalid;
	int n = 0;

	if (bdd_istrue(aHigh)) {
		high = bHigh;
	} else if (bdd_isfalse(aHigh) || bdd_isfalse(bHigh)) {
		high = bdd_false;
	} else if (bdd_istrue(bHigh)) {
		high = aHigh;
	} else {
		SPAWN_AND(aHigh, bHigh, level);
		n = 1;
	}

	if (bdd_istrue(aLow)) {
		low = bLow;
	} else if (bdd_isfalse(aLow) || bdd_isfalse(bLow)) {
		low = bdd_false;
	} else if (bdd_istrue(bLow)) {
		low = aLow;
	} else {
		low = CALL_AND(aLow, bLow, level);
	}

	if (n) high = SYNC();

	// calculate and return results
	result = bdd_makenode(level, low, high);
	if (cachenow) cache_put(bdd_set_data(a, CACHE_AND), b, bdd_false, result);
	return result;
}

BDD bdd_xor(BDD a, BDD b, BDDVAR prev_lvl) {
	// terminal cases
	if (bdd_isfalse(a)) return b;
	if (bdd_isfalse(b)) return a;
	if (bdd_istrue(a)) return bdd_not(b);
	if (bdd_istrue(b)) return bdd_not(a);
	if (bdd_strip_metadata(a) == bdd_strip_metadata(b)) return bdd_false;
	if (bdd_strip_metadata(a) == bdd_not(bdd_strip_metadata(b))) return bdd_true;

	// improve for caching
	if (bdd_strip_mark_metadata(a) > bdd_strip_mark_metadata(b)) {
		BDD t = b; b = a; a = t;
	}

	// improve for caching
	if (bdd_hasmark(a)) {
		a = bdd_strip_mark(a);
		b = bdd_not(b);
	}

	// retrieve nodes
	bddnode_t na, nb;
	upc_handle_t ha = node_query(a, &na);
	upc_handle_t hb = node_query(b, &nb);
	node_sync(ha, a, &na);
	node_sync(hb, b, &nb);

	// find lowest level
	BDDVAR va = NODE_LEVEL(&na);
	BDDVAR vb = NODE_LEVEL(&nb);
	BDDVAR level = va < vb ? va : vb;

	// consult the cache 
	BDD result;
	int cachenow = granularity < 2 || prev_lvl == 0 ? 1 : prev_lvl / granularity != level / granularity;
	if (cachenow) {
		if (cache_get(bdd_set_data(a, CACHE_XOR), b, bdd_false, &result)) {
			return result;
		}
	}

	// get cofactors 
	BDD aLow = a, aHigh = a;
	BDD bLow = b, bHigh = b;

	if (level == va) {
		aLow = node_low(a, &na);
		aHigh = node_high(a, &na);
	}

	if (level == vb) {
		bLow = node_low(b, &nb);
		bHigh = node_high(b, &nb);
	}

	// recursive computation
	SPAWN_XOR(aHigh, bHigh, level);
	BDD low = CALL_XOR(aLow, bLow, level);
	BDD high = SYNC();

	// calculate and return results
	result = bdd_makenode(level, low, high);
	if (cachenow) cache_put(bdd_set_data(a, CACHE_XOR), b, bdd_false, result);
	return result;
}

BDD bdd_ite(BDD a, BDD b, BDD c, BDDVAR prev_lvl) {
	// terminal cases 
	if (bdd_istrue(a)) return b;
	if (bdd_isfalse(a)) return c;
	if (bdd_strip_metadata(a) == bdd_strip_metadata(b)) b = bdd_true;
	if (bdd_strip_metadata(a) == bdd_not(bdd_strip_metadata(b))) b = bdd_false;
	if (bdd_strip_metadata(a) == bdd_strip_metadata(c)) c = bdd_false;
	if (bdd_strip_metadata(a) == bdd_not(bdd_strip_metadata(c))) c = bdd_true;
	if (bdd_strip_metadata(b) == bdd_strip_metadata(c)) return b;
	if (bdd_istrue(b) && bdd_isfalse(c)) return a;
	if (bdd_isfalse(b) && bdd_istrue(c)) return bdd_not(a);

	// cases that reduce to AND and XOR
	if (bdd_isfalse(c)) return CALL_AND(a, b, prev_lvl);
	if (bdd_istrue(b)) return bdd_not(CALL_AND(bdd_not(a), bdd_not(c), prev_lvl));
	if (bdd_isfalse(b)) return CALL_AND(bdd_not(a), c, prev_lvl);
	if (bdd_istrue(c)) return bdd_not(CALL_AND(a, bdd_not(b), prev_lvl));
	if (bdd_strip_metadata(b) == bdd_not(bdd_strip_metadata(c))) return CALL_XOR(a, c, 0);

	// canonical for optimal cache use
	if (bdd_hasmark(a)) {
		a = bdd_strip_mark(a);
		BDD t = c; c = b; b = t;
	}

	int mark = 0;
	if (bdd_hasmark(b)) {
		b = bdd_not(b);
		c = bdd_not(c);
		mark = 1;
	}

	// retrieve nodes
	bddnode_t na, nb, nc;
	upc_handle_t ha = node_query(a, &na);
	upc_handle_t hb = node_query(b, &nb);
	upc_handle_t hc = node_query(c, &nc);
	node_sync(ha, a, &na);
	node_sync(hb, b, &nb);
	node_sync(hc, c, &nc);

	// get lowest level
	BDDVAR va = NODE_LEVEL(&na);
	BDDVAR vb = NODE_LEVEL(&nb);
	BDDVAR vc = NODE_LEVEL(&nc);
	BDDVAR level = vb < vc ? vb : vc;

	// fast case
	if (va < level && bdd_isfalse(node_low(a, &na)) && bdd_istrue(node_high(a, &na))) {
		BDD result = bdd_makenode(va, c, b);
		return mark ? bdd_not(result) : result;
	}

	if (va < level) level = va;

	// consult the cache
	BDD result;
	int cachenow = granularity < 2 || prev_lvl == 0 ? 1 : prev_lvl / granularity != level / granularity;
	if (cachenow) {
		if (cache_get(bdd_set_data(a, CACHE_ITE), b, c, &result)) {
			return mark ? bdd_not(result) : result;
		}
	}

	// get cofactors
	BDD aLow = a, aHigh = a;
	BDD bLow = b, bHigh = b;
	BDD cLow = b, cHigh = b;

	if (level == va) {
		aLow = node_low(a, &na);
		aHigh = node_high(a, &na);
	}

	if (level == vb) {
		bLow = node_low(b, &nb);
		bHigh = node_high(b, &nb);
	}

	if (level == vc) {
		cLow = node_low(c, &nc);
		cHigh = node_high(c, &nc);
	}

	// recursive computation
	BDD low = bdd_invalid, high = bdd_invalid;
	int n = 0;

	if (bdd_istrue(aHigh)) {
		high = bHigh;
	} else if (bdd_isfalse(aHigh)) {
		high = cHigh;
	} else {
		SPAWN_ITE(aHigh, bHigh, cHigh, level);
		n = 1;
	}

	if (bdd_istrue(aLow)) {
		low = bLow;
	} else if (bdd_isfalse(aLow)) {
		low = cLow;
	} else {
		low = CALL_ITE(aLow, bLow, cLow, level);
	}

	if (n) high = SYNC();

	result = bdd_makenode(level, low, high);
	if (cachenow) cache_put(bdd_set_data(a, CACHE_ITE), b, c, result);
	return mark ? bdd_not(result) : result;
}

BDD bdd_relnext(BDD a, BDD b, BDDVAR prev_lvl, uint64_t vars) {
	// terminal cases
	if (bdd_istrue(a) && bdd_istrue(b)) return bdd_true;
	if (bdd_isfalse(a)) return bdd_false;
	if (bdd_isfalse(b)) return bdd_false;

	uint64_t from = vars & 0x00000000ffffffff;
	uint64_t node_i = vars >> 32;

	varchain_t *varchain;
	if (from < next_count) {
		rel_t rel = next[from];
		varchain = rel->vararray[node_i];
	}

	if (varchain == NULL) return a; // terminal case (vars empty)

	// retrieve nodes
	bddnode_t na, nb, nc;
	upc_handle_t ha = node_query(a, &na);
	upc_handle_t hb = node_query(b, &nb);
	node_sync(ha, a, &na);
	node_sync(hb, b, &nb);
	
	// capture constant nodes
	bool ba = !bdd_isconst(a);
	bool bb = !bdd_isconst(b);

	// determine top level
	BDDVAR va = ba ? NODE_LEVEL(&na) : 0xffffffff;
	BDDVAR vb = bb ? NODE_LEVEL(&nb) : 0xffffffff;
	BDDVAR level = va < vb ? va : vb;

	// skip variables
	for (;;) {
		// check if level is s/t 
		if (level == varchain->level || (level^1) == varchain->level) break;
		// check if level < s/t 
		if (level < varchain->level) break;

		varchain = varchain->next;
		node_i++;

		if (varchain == NULL) return a;
	}

	/* Consult the cache */

	int cachenow = granularity < 2 || prev_lvl == 0 ? 1 : prev_lvl / granularity != level / granularity;
	uint64_t cache_var_index = from | (node_i << 32);

	BDD result;

	if (cachenow) {
		if (cache_get(bdd_set_data(a, CACHE_RELNEXT), b, cache_var_index, &result)) {
			return result;
		}
	}

	/* Recursive computation */

	if (level == varchain->level || (level^1) == varchain->level) {

		// Get s and t
		BDDVAR s = level & (~1);
		BDDVAR t = s + 1;
		BDD a0, a1, b0, b1;

		if (ba) {
			if (NODE_LEVEL(&na) == s) {
				a0 = node_low(a, &na);
				a1 = node_high(a, &na);
			} else {
				a0 = a1 = a;
			}
		} else {
			a0 = a1 = a;
		}

		if (bb) {
			if (NODE_LEVEL(&nb) == s) {
				b0 = node_low(b, &nb);
				b1 = node_high(b, &nb);
			} else {
				b0 = b1 = b;
			}
		} else {
			b0 = b1 = b;
		}

		// retrieve nodes 
		BDD b00, b01, b10, b11;
		bddnode_t nb0, nb1;
		ha = node_query(b0, &nb0);
		hb = node_query(b1, &nb1);
		
		if (!bdd_isconst(b0)) {
			node_sync(ha, b0, &nb0);

			if (NODE_LEVEL(&nb0) == t) {
				b00 = node_low(b0, &nb0);
				b01 = node_high(b0, &nb0);
			} else {
				b00 = b01 = b0;
			}
		} else {
			b00 = b01 = b0;
		}

		if (!bdd_isconst(b1)) {
			node_sync(hb, b1, &nb1);

			if (NODE_LEVEL(&nb1) == t) {
				b10 = node_low(b1, &nb1);
				b11 = node_high(b1, &nb1);
			} else {
				b10 = b11 = b1;
			}
		} else {
			b10 = b11 = b1;
		}

		/* Recursive computation */

		uint64_t _vars = from | ((node_i + 1) << 32);

		SPAWN_RELPROD(a0, b00, _vars, level);
		SPAWN_RELPROD(a1, b10, _vars, level);
		SPAWN_RELPROD(a0, b01, _vars, level);
		BDD f = CALL_RELPROD(a1, b11, _vars, level);
		BDD e = SYNC();
		BDD d = SYNC();
		BDD c = SYNC();

		SPAWN_ITE(c, bdd_true, d, 0);
		d = CALL_ITE(e, bdd_true, f, 0);
		c = SYNC();

		result = bdd_makenode(s, c, d);
	}
	else {
		BDD a0, a1, b0, b1;

		if (ba) {
			if (NODE_LEVEL(&na) == level) {
				a0 = node_low(a, &na);
				a1 = node_high(a, &na);
			} else {
				a0 = a1 = a;
			}
		} else {
			a0 = a1 = a;
		}

		if (bb) {
			if (NODE_LEVEL(&nb) == level) {
				b0 = node_low(b, &nb);
				b1 = node_high(b, &nb);
			} else {
				b0 = b1 = b;
			}
		} else {
			b0 = b1 = b;
		}

		/* Recursive computation */

		uint64_t _vars = from | (node_i << 32);

		if (b0 != b1) {
			if (a0 == a1) {
				SPAWN_RELPROD(a0, b0, _vars, level);
				BDD r1 = CALL_RELPROD(a1, b1, _vars, level);
				BDD r0 = SYNC();

				result = bdd_or(r0, r1);
			} 
			else {
				SPAWN_RELPROD(a0, b0, _vars, level);
				SPAWN_RELPROD(a1, b1, _vars, level);
				SPAWN_RELPROD(a0, b0, _vars, level);
				BDD r11 = CALL_RELPROD(a1, b1, _vars, level);
				BDD r10 = SYNC();
				BDD r01 = SYNC();
				BDD r00 = SYNC();

				SPAWN_ITE(r00, bdd_true, r01, 0);
				BDD r1 = CALL_ITE(r10, bdd_true, r11, 0);
				BDD r0 = SYNC();

				result = bdd_makenode(level, r0, r1);
			}
		} 
		else {
			SPAWN_RELPROD(a0, b0, _vars, level);
			BDD r1 = CALL_RELPROD(a1, b1, _vars, level);
			BDD r0 = SYNC();

			result = bdd_makenode(level, r0, r1);
		}
	}

	if (cachenow) {
		cache_put(bdd_set_data(a, CACHE_RELNEXT), b, cache_var_index, result);
	}

	return result;
}

BDD bdd_go_par(BDD cur, BDD visited, size_t from, size_t len) {
	if (len == 1) {
		BDD succ = CALL_RELPROD(cur, next[from]->bdd, from, 0);
		return bdd_diff(succ, visited);
	} else {
		SPAWN_GOPAR(cur, visited, from, (len+1)/2);
		BDD right = CALL_GOPAR(cur, visited, from+(len+1)/2, len/2);
		BDD left = SYNC(); 
		return bdd_or(left, right);
	}
}

uint64_t bdd_par(BDD bdd) {
	BDD visited = bdd;
	BDD new = visited;
	size_t lvl = 1;

	do {
		printf("Level %zu... ", lvl++);
		new = CALL_GOPAR(new, visited, 0, next_count);
		visited = bdd_or(visited, new);
		printf("done.\n");
	} while (bdd_strip_metadata(new) != bdd_false);

	return visited;
}

BDD driver(uint64_t mdata, BDD a, BDD b, BDD c, BDDVAR lvl) {
	if (mdata & TASK_TYPE_ITE) return bdd_ite(a, b, c, lvl);
	if (mdata & TASK_TYPE_RELPROD) return bdd_relnext(a, b, c, lvl);
	if (mdata & TASK_TYPE_AND) return bdd_and(a, b, lvl);
	if (mdata & TASK_TYPE_XOR) return bdd_xor(a, b, lvl);
	if (mdata & TASK_TYPE_SATCOUNT) return bdd_satcount(a, b, lvl);
	if (mdata & TASK_TYPE_GOPAR) return bdd_go_par(a, b, c, lvl);
	if (mdata & TASK_TYPE_SUPPORT) return bdd_support(a);
	if (mdata & TASK_TYPE_PAR) return bdd_par(a);

	fprintf(stderr, "ERROR: task not found\n");
	exit(EXIT_FAILURE);
	return 0;
}

void bdd_init(int cachegran, uint64_t tablesize, uint64_t cachesize, int _qp, int _chunksize) {
	granularity = cachegran;
	
	ws_init(&driver);
	htable_init(&ws_progress, tablesize, _qp, _chunksize);
	cache_init(&ws_progress, cachesize);
	nodecache_init();
}
