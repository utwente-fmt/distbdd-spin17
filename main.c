#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <avl.h>
#include <bdd.h>
#include <localstore.h>
#include <nodecache.h>
#include <varchain.h>
#include <wstealer.h>
#include <sys/time.h>
#include <popt.h>

static size_t ser_done = 0;
static int vector_size; // size of vector
static int statebits, actionbits; // number of bits for state, number of bits for action
static int bits_per_integer; // number of bits per integer in the vector
const char *filename;

AVL(ser_reversed, struct bdd_ser) {
	return left->assigned - right->assigned;
}

static avl_node_t *ser_reversed_set = NULL;

static double wctime() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

BDD serialize_get_reversed(uint64_t value) {
	if (!bdd_isnode(value)) {
		return value;
	}

	struct bdd_ser s, *ss;
	s.assigned = bdd_strip_mark(value);
	ss = ser_reversed_search(ser_reversed_set, &s);
	assert(ss != NULL);
	return bdd_transfermark(value, ss->bdd);
}

static int counter = 0;

void serialize_fromfile(FILE *in) {
	size_t count;
	if (fread(&count, sizeof(size_t), 1, in) != 1) {
		printf("serialize_fromfile: file format error, giving up\n");
	  exit(EXIT_FAILURE);
	}
	
	int i; for (i = 1; i <= count; i++) {
		uint64_t a, b;
		
		// read node from file
		if ((fread(&a, sizeof(uint64_t), 1, in) != 1) ||
				(fread(&b, sizeof(uint64_t), 1, in) != 1)) {
				printf("serialize_fromfile: file format error, giving up\n");
				exit(EXIT_FAILURE);
		}
		
		// extract all components from the node
		BDD node_high = a & 0x800000ffffffffff;
		BDD node_low = b & 0x000000ffffffffff;
		BDDVAR node_level = (BDDVAR)(b >> 40);
		
		// we changed the position of the 'comp' bit, which must be fixed...
		if ((node_high & bdd_metadata_done) != 0) {
			node_high &= ~bdd_metadata_done;
			node_high |= bdd_complement;
		}
		
		BDD low = serialize_get_reversed(node_low);
		BDD high = serialize_get_reversed(node_high);
		
		struct bdd_ser s;
		s.bdd = bdd_makenode_local(node_level, low, high);
		s.assigned = ++ser_done;
		
		ser_reversed_insert(&ser_reversed_set, &s);
	}
}

static rel_t rel_load(FILE* f) {
	serialize_fromfile(f);
	
	// read info from file
  size_t rel_bdd, rel_vars;
	if ((fread(&rel_bdd, sizeof(size_t), 1, f) != 1) ||
			(fread(&rel_vars, sizeof(size_t), 1, f) != 1)) {
			printf("Invalid input file\n");
		  exit(EXIT_FAILURE);
	}
	
	// get states
	rel_t rel = (rel_t)malloc(sizeof(struct relation));
	rel->bdd = serialize_get_reversed(rel_bdd);
	
	// get and process variables
	rel->variables = CALL_SUPPORT(serialize_get_reversed(rel_vars));
	rel->varchain = bdd_to_chain(rel->variables); 
	rel->vararray = chain_to_array(rel->varchain);
	rel->varcount = chain_count(rel->varchain);

	return rel;
}

static void set_load(FILE* f) {
	serialize_fromfile(f); 
	
	// read info from file
	size_t set_bdd, set_vector_size, set_state_vars;
	if ((fread(&set_bdd, sizeof(size_t), 1, f) != 1) ||
			(fread(&set_vector_size, sizeof(size_t), 1, f) != 1) ||
			(fread(&set_state_vars, sizeof(size_t), 1, f) != 1)) {
			fprintf(stderr, "Invalid input file!\n");
			exit(EXIT_FAILURE);
	}
	
	// get states
	states = (set_t)malloc(sizeof(struct set));
	states->bdd = serialize_get_reversed(set_bdd);
	
	// get and process variables
	states->variables = CALL_SUPPORT(serialize_get_reversed(set_state_vars));
	states->varchain = bdd_to_chain(states->variables); 
	states->vararray = chain_to_array(states->varchain);
	states->varcount = chain_count(states->varchain);
}

BDD extend_relation(BDD relation, BDDSET variables) {
	// first determine which state BDD variables are in rel
	int *has = (int*)alloca(statebits * sizeof(int));
	int i; for (i = 0; i < statebits; i++) has[i] = 0;
	BDDSET s = variables;
	while (bdd_strip_metadata(s) == bdd_true) {
		BDDVAR v = bdd_var(s);
		if (v/2 >= (unsigned)statebits) break; // action labels
		has[v/2] = 1;
		s = bdd_high(s);
	}
	
	// then determine the extension to the transition relation
	BDD eq = bdd_true;
	for (i = statebits - 1; i >= 0; i--) {
		if (has[i]) continue;
		BDD low = bdd_makenode(2*i+1, eq, bdd_true);
		BDD high = bdd_makenode(2*i+1, bdd_false, eq);
		eq = bdd_makenode(2*i, low, high);
	}
	
	// finally extend the transition relation
	return CALL_AND(relation, eq, 0);
}

BDD big_union(int first, int count) {
	// terminal case
	if (count == 1) return next[first]->bdd;
	
	// recursive computation
	BDD left = big_union(first, count/2);
	BDD right = big_union(first+count/2, count-count/2);
	
	// return the union of left and right
	return bdd_or(left, right);
}

void merge_relations() {
	BDD prime_variables = bdd_true;
	
	int i; for (i = statebits - 1; i >= 0; i--) {
		prime_variables = bdd_set_add(prime_variables, i*2+1);
	}
	
	for (i = 0; i < next_count; i++) {
		next[i]->bdd = extend_relation(next[i]->bdd, next[i]->variables);
		next[i]->variables = prime_variables;
		next[i]->varchain = bdd_to_chain(next[i]->variables); 
		next[i]->vararray = chain_to_array(next[i]->varchain);
		next[i]->varcount = chain_count(next[i]->varchain);
	}
	
	next[0]->bdd = big_union(0, next_count);
	next_count = 1;
}

int read_model() {
	if (filename == NULL) {
		fprintf(stderr, "No model has been given!\n");
		return -1;
	}

	FILE *f = fopen(filename, "r");

	if (f == NULL) {
		fprintf(stderr, "Cannot open file '%s'!\n", filename);
		return -1;
	}

	if (MYTHREAD == 0) {
		printf("Initializing %s!\n", filename);
	}
	
	if ((fread(&vector_size, sizeof(int), 1, f) != 1) ||
			(fread(&statebits, sizeof(int), 1, f) != 1) ||
			(fread(&actionbits, sizeof(int), 1, f) != 1)) {
			fprintf(stderr, "Invalid input file!\n");
			exit(EXIT_FAILURE);
	}
	
	bits_per_integer = statebits;
	statebits *= vector_size;

	if (MYTHREAD == 0) {
		printf("vector size: %d\n", vector_size);
		printf("bits per integer: %d\n", bits_per_integer);
		printf("number of BDD variables: %d\n", vector_size * bits_per_integer);
		printf("Loading initial states: \n");
	}

	set_load(f);

	if (MYTHREAD == 0) {
		printf("Initial states loaded... \n");
	}

	// read transitions
	if (fread(&next_count, sizeof(int), 1, f) != 1) {
		fprintf(stderr, "Invalid input file!\n");
		exit(EXIT_FAILURE);
	}
	
	if (MYTHREAD == 0) {
		printf("Loading transition relations... \n");
	}

	next = (rel_t*)malloc(sizeof(rel_t) * next_count);

	int i; for (i = 0; i < next_count; i++) {
		next[i] = rel_load(f);
	}
	
	// done reading from file..
	fclose(f);
	
	//merge_relations();

	if (MYTHREAD == 0) {
		printf("%d integers per state, %d bits per integer, %d transition groups\n", vector_size, bits_per_integer, next_count);
		printf("BDD nodes:\n");
		printf("Initial states: %llu BDD nodes\n", bdd_nodecount(states->bdd));

		for (i = 0; i < next_count; i++) {
			printf("Transition %d: %llu BDD nodes\n", i, bdd_nodecount(next[i]->bdd));
		}
	}

	return 1;
}

int main(int argc, char *argv[]) {
	
	int flag_help = -1;
	int flag_qp = -1;
	int cachesize = 23;
	int chunksize = 8;
	int granularity = 8;
	int tablesize = 23;
	
	poptContext con;
	struct poptOption optiontable[] = {
		{ "cachesize", 0, POPT_ARG_INT, &cachesize, 'c', "Sets the size of the memoization table to 1<<N entries. Defaults to 23. Maximum of 32." },
		{ "chunksize", 0, POPT_ARG_INT, &chunksize, 'e', "When quadratic probing is used, the chunk size determines the number of buckets obtained per probe. Maximum of 4096." },
		{ "granularity", 0, POPT_ARG_INT, &granularity, 'g', "Only use the memoization table every 1/N BDD levels. Defaults to 8." },
		{ "help", 0, POPT_ARG_NONE, &flag_help, 'h', "Display available options" },
		{ "quadratic-probing", 0, POPT_ARG_NONE, &flag_qp, 'q', "Use quadratic probing instead of linear probing as collision strategy for the BDD table. This prevents clustering issues, but it hits performance in distributed runs." },
		{ "tablesize", 0, POPT_ARG_INT, &tablesize, 't', "Sets the size of the BDD table to 1<<N nodes. Defaults to 23. Maximum of 32." },
		{NULL, 0, 0, NULL, 0, NULL, NULL}
	};
	
	con = poptGetContext("distdd", argc, (const char **)argv, optiontable, 0);
	poptSetOtherOptionHelp(con, "[OPTIONS..] <model.bdd>");
	
	if (argc < 2) {
		poptPrintUsage(con, stderr, 0);
		exit(1);
	}
	
	char c;  
	while ((c = poptGetNextOpt(con)) > 0) {  
		switch (c) {
			case 'h':
				poptPrintHelp(con, stdout, 0);
				return 0;
		}
	}
	
	// make sure that: 4 <= cachegran
	granularity = MAX(granularity, 4);
	if (MYTHREAD == 0) 
		printf("using a cache granularity of %d\n", granularity);
	
	// make sure that 1 <= chunksize <= 4096
	chunksize = MAX(chunksize, 1);
	chunksize = MIN(chunksize, 4096);
	
	if (flag_qp > 0) {
		if (MYTHREAD == 0)
			printf("using quadratic probing in the BDD table with chunk size %d\n", chunksize);
	}
	else if (MYTHREAD == 0) {
		printf("using linear probing in the BDD table\n");
	}
	
	// make sure that: 23 <= cachesize <= 32
	cachesize = MAX(cachesize, 23);
	cachesize = MIN(cachesize, 32);
	
	uint64_t cacheentries = (((uint64_t)1) << cachesize);
	uint64_t cachemem = (40 * cacheentries) / (1048576);
	if (MYTHREAD == 0) 
		printf("using a memoization table of 2^%d = %llu entries (~%llu MB) per thread\n", cachesize, cacheentries, cachemem);
	
	// make sure that: 23 <= tablesize <= 32
	tablesize = MAX(tablesize, 23);
	tablesize = MIN(tablesize, 32);
	
	uint64_t tableentries = (((uint64_t)1) << tablesize);
	uint64_t tablemem = (24 * tableentries) / (1048576);
	if (MYTHREAD == 0) 
		printf("using a BDD table of 2^%d = %llu entries (~%llu MB) per thread\n", tablesize, tableentries, tablemem);
	
	bdd_init(granularity, tableentries, cacheentries, flag_qp, chunksize);
	
	// read model file from disk
	filename = poptGetArg(con);
	read_model();

	// perform reachability
	ws_comp_out_t parout = COMPUTE_PAR(states->bdd);

	if (MYTHREAD == 0) {
		printf("PAR Time: %f\n", parout.time);
	}

	// show statistics
	nodecache_show_stats();
	ws_statistics();

	// determine the size of the state space
	ws_comp_out_t satout = COMPUTE_SATCOUNT(parout.output, 0, 0);

	if (MYTHREAD == 0) {
		printf("Final states: %llu states\n", satout.output);
		printf("Final states: %llu BDD nodes\n", bdd_nodecount(parout.output));
	}

	return 0;
}
