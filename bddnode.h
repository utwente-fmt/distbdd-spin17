#ifndef BDDNODE_H
#define BDDNODE_H

#include <stdio.h>

typedef uint64_t BDD;
typedef uint64_t BDDSET;
typedef uint64_t BDDMAP;
typedef uint32_t BDDVAR;

typedef struct bddnode {
	uint64_t high_lvl_used;
	uint64_t low_data_comp;
} bddnode_t; 

// bitmaps for 'high_lvl_used' in BDD nodes
#define MASK_HIGH       ((uint64_t)0xFFFFFFFFFFC00000) // 42 bits
#define MASK_LEVEL      ((uint64_t)0x00000000003FFFFC) // 20 bits
#define MASK_LOCAL_HIGH ((uint64_t)0x0000000000000002) // 1 bit

// bitmaps for 'low_data_comp' in BDD nodes
#define MASK_LOW        		 ((uint64_t)0xFFFFFFFFFFC00000) // 42 bits
#define MASK_DATA       		 ((uint64_t)0x00000000003FFFFC) // 20 bits for data
#define MASK_DATA_MARK  		 ((uint64_t)0x0000000000000004) // 1 bits (part of data)
#define MASK_LOCAL_LOW  		 ((uint64_t)0x0000000000000002) // 1 bits
#define MASK_COMP       		 ((uint64_t)0x0000000000000001) // 1 bit

// macro's for accessing fields of BDD nodes
#define NODE_HIGH(n)  ((((n)->high_lvl_used) & MASK_HIGH) >> 22)
#define NODE_LEVEL(n) ((((n)->high_lvl_used) & MASK_LEVEL) >> 2)
#define NODE_LOW(n)   ((((n)->low_data_comp) & MASK_LOW) >> 22)
#define NODE_DATA(n)  ((((n)->low_data_comp) & MASK_DATA) >> 2)
#define NODE_COMP(n)  ((((n)->low_data_comp) & MASK_COMP))

// macro's for checking certain properties of BDD nodes
#define NODE_IS_LOCAL_HIGH(node) ((node)->high_lvl_used & MASK_LOCAL_HIGH)
#define NODE_IS_LOCAL_LOW(node) ((node)->low_data_comp & MASK_LOCAL_LOW)

// macro's for setting specific fields of BDD nodes
#define NODE_SET_HIGH(high) (((high) & (uint64_t)0x000003FFFFFFFFFF) << 22)
#define NODE_SET_LOW(low) (((low) & (uint64_t)0x000003FFFFFFFFFF) << 22)
#define NODE_SET_LVL(lvl) (((uint64_t)((lvl) & 0x000FFFFF)) << 2)

// bitmaps for BDD references with memory layout:
// - 3 bits metadata ("done" flag, "lock" flag, and "local" flag)
// - 1 bit for complement edges
// - 42 bits for storing the index in 'data'
#define bdd_metadata       ((BDD)0xE000000000000000) // 3 bits for metadata:
#define bdd_metadata_done  ((BDD)0x8000000000000000) // - 1 bit, "done" flag
#define bdd_metadata_lock  ((BDD)0x4000000000000000) // - 1 bit, "lock" flag
#define bdd_metadata_local ((BDD)0x2000000000000000) // - 1 bit, "local" flag
#define bdd_complement     ((BDD)0x1000000000000000) // 1 bit for complement edges
#define bdd_false          ((BDD)0x0000000000000000)
#define bdd_true           (bdd_false|bdd_complement)
#define bdd_invalid        ((BDD)0x0FFFFFFFFFFFFFFF) // 60 bits

#define bdd_strip_metadata(s) ((s)&~bdd_metadata)
#define bdd_strip_mark(s) ((s)&~bdd_complement)
#define bdd_strip_mark_metadata(s) bdd_strip_mark(bdd_strip_metadata(s))

#define bdd_istrue(bdd) (bdd_strip_metadata(bdd) == bdd_true)
#define bdd_isfalse(bdd) (bdd_strip_metadata(bdd) == bdd_false)
#define bdd_isconst(bdd) (bdd_isfalse(bdd) || bdd_istrue(bdd))
#define bdd_isnode(bdd) (!bdd_isfalse(bdd) && !bdd_istrue(bdd))
#define bdd_islocal(bdd) ((bdd) & bdd_metadata_local)
#define bdd_isdone(bdd) ((bdd) & bdd_metadata_done)

#define bdd_not(a) (((BDD)a)^bdd_complement)
#define bdd_hasmark(s) (((s)&bdd_complement) ? 1 : 0)

#define bdd_transfermark(from, to) ((to) ^ ((from) & bdd_complement))
#define bdd_set_data(bdd, data) (((bdd) & 0xF00003FFFFFFFFFF) | (((uint64_t)((data) & 0x0000FFFF)) << 42))
#define bdd_set_done(bdd) ((bdd) | bdd_metadata_done)

#endif
