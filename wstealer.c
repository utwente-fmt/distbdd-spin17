#include "wstealer.h"

// shared deque and data transfer cells
static shared [WS_DEQUE_SIZE] ws_task_t deque[THREADS * WS_DEQUE_SIZE];
static shared ws_task_t transfer[THREADS];
static shared uint64_t request[THREADS];
static shared uint64_t term[THREADS];

// semaphores for signalling transfer completion
static bupc_sem_t * shared comp[THREADS];
static bupc_sem_t **completion;

// pointers to local regions of the shared data structures
static ws_task_t *deque_p = NULL,* deque_head_p = NULL, *transfer_p = NULL;
static uint64_t *request_p = NULL, *term_p = NULL;

// fields used by work stealing operations
static ws_output_t (*handler)(uint64_t, uint64_t, uint64_t, uint64_t, uint32_t);
static ws_task_t *empty_task;
static uint64_t head, tail;
static size_t victims_n[4];
static size_t *victims[4];

static inline double wctime() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

static inline void init_victim_array() {
	int i; for (i = 0; i < 4; i++) {
		victims[i] = (size_t*)malloc(sizeof(size_t) * THREADS);
		victims_n[i] = 0;
	}

	for (i = 0; i < THREADS; i++) {
		uint32_t distance = bupc_thread_distance(MYTHREAD, i);
		if (distance == BUPC_THREADS_VERYNEAR) victims[0][victims_n[0]++] = i;
		if (distance == BUPC_THREADS_NEAR) victims[1][victims_n[1]++] = i;
		if (distance == BUPC_THREADS_FAR) victims[2][victims_n[2]++] = i;
		if (distance == BUPC_THREADS_VERYFAR) victims[3][victims_n[3]++] = i;
	}
}

static inline void init_comp_sem() {
	comp[MYTHREAD] = bupc_sem_alloc(0);
	completion = malloc(THREADS * sizeof(bupc_sem_t*));
	upc_barrier;

	int i; for (i = 0; i < THREADS; i++) {
		completion[i] = comp[i];
	}
}

void ws_init(void *func) {
	handler = func;

	empty_task = malloc(sizeof(ws_task_t));
	empty_task->metadata = TASK_EMPTY;

	deque_p = upc_cast(&deque[WS_DEQUE_OFFSET]);
	deque_head_p = upc_cast(&deque[WS_DEQUE_OFFSET]);
	transfer_p = upc_cast(&transfer[MYTHREAD]);
	request_p = upc_cast(&request[MYTHREAD]);
	term_p = upc_cast(&term[MYTHREAD]);

	*request_p = WS_REQ_EMPTY;
	head = 0; tail = 0;

	init_victim_array();
	init_comp_sem();
}

void ws_free() {
	bupc_sem_free(comp[MYTHREAD]);
	free(completion);
	free(empty_task);

	int i; for (i = 0; i < 4; i++) {
		free(victims[i]);
	}
}

static inline void ws_refuse() {
	size_t thief = *request_p & WS_REQ_THIEF;
	bupc_memput_signal_async(&transfer[thief], empty_task, 
		sizeof(uint64_t), completion[thief], 1);
}

static inline void ws_block() {
	ws_refuse();
	*request_p = WS_REQ_BLOCK;
}

static inline void ws_communicate() {
	bupc_poll();

	if (tail-head < WS_TRANS_THRESHOLD) {
		if ((*request_p & WS_REQ_BLOCK) == 0) {
			if (*request_p & WS_REQ_OCC) ws_block();
			else if (!LOCAL_CAS(request_p, WS_REQ_EMPTY, WS_REQ_BLOCK)) ws_block();
			//else if (CAS64(&request[MYTHREAD], WS_REQ_EMPTY, WS_REQ_BLOCK) != WS_REQ_EMPTY) ws_block();
		}
	} else if (*request_p & WS_REQ_BLOCK) {
		*request_p = WS_REQ_EMPTY;
	} else if (*request_p & WS_REQ_OCC) {
		size_t thief = *request_p & WS_REQ_THIEF;
		*request_p = WS_REQ_EMPTY; 

		deque_head_p->metadata |= TASK_STOLEN;
		deque_head_p->metadata |= ((uint64_t)thief << 35) & TASK_THREADID;
		deque_head_p->metadata |= (WS_DEQUE_OFFSET + head) & TASK_INDEX;

		bupc_memput_signal_async(&transfer[thief], deque_head_p, 
			sizeof(ws_metadata_t) + sizeof(ws_input_t), completion[thief], 1);

		deque_head_p++;
		head++;
	}
}

static inline uint32_t ws_steal(size_t victim) {
	ws_communicate();

	ADD_TO_STEAL_ATTEMPTS(1);
	uint64_t result = CAS64(&request[victim], WS_REQ_EMPTY, 
		(MYTHREAD & WS_REQ_THIEF) | WS_REQ_OCC); // write steal request

	if (result == WS_REQ_EMPTY) {
		bupc_sem_wait(completion[MYTHREAD]); // wait for the task to arrive
		ws_metadata_t mdata = transfer_p->metadata;

		if (mdata & TASK_EMPTY) {
			ADD_TO_EMPTY_STEALS(1);
			return WS_STEAL_FAIL; // no task received: return a "fail"
		}

		ADD_TO_STEALS(1);
		ADD_EXECUTED_TASKS(1);
		
		ws_input_t *in = &transfer_p->input;
		ws_output_t out = handler(mdata & TASK_TYPEFILTER, 
			in->a, in->b, in->c, in->lvl); // execute received task
			
		ATOMIC_SET(&deque[mdata & TASK_INDEX].output, bdd_set_done(out));
		return WS_STEAL_SUCC; // write back result & return
	} 
	else if (result & WS_REQ_BLOCK) {
		ADD_TO_BLOCKED_STEALS(1);
	} else {
		ADD_TO_FAILED_STEALS(1);
	}

	return WS_STEAL_FAIL;
}

void ws_spawn(ws_metadata_t metadata, uint64_t a, uint64_t b, uint64_t c, uint32_t lvl) {
	deque_p->metadata = metadata;
	deque_p->input.lvl = lvl;
	deque_p->input.a = a;
	deque_p->input.b = b;
	deque_p->input.c = c;
	deque_p->output = 0;

	deque_p++;
	tail++;
	
	if (tail >= WS_DEQUE_SIZE) {
		fprintf(stderr, "ERROR: deque is too small\n");
		exit(EXIT_FAILURE);
	}
}

ws_output_t ws_call(ws_metadata_t metadata, uint64_t a, uint64_t b, uint64_t c, uint32_t lvl) {
	ws_communicate();
	ADD_EXECUTED_TASKS(1);
	return handler(metadata, a, b, c, lvl);
}

ws_output_t ws_sync() {
	ws_communicate();
	ws_task_t *prev_p = deque_p - 1;

	if (prev_p->metadata & TASK_STOLEN) {
		size_t thief = TASK_GET_THREADID(prev_p);

		while (!bdd_isdone(prev_p->output)) {
			if (ws_steal(thief) & WS_STEAL_SUCC) continue; // perform leapfrogging
			if (bdd_isdone(prev_p->output)) break;

			int i; for (i = 0; i < 4; i++) {
				int j; for (j = 0; j < victims_n[i]; j++) {
					size_t victim = victims[i][rand() % victims_n[i]];
					if (ws_steal(victim) & WS_STEAL_SUCC) goto sync_reset; // perform hierarchical work stealing
					if (bdd_isdone(prev_p->output)) goto sync_reset;
				}
			}
		sync_reset:
		}

		ws_output_t output = prev_p->output;
		deque_head_p--;
		deque_p = prev_p;
		head--;
		tail--;

		return output & ~bdd_metadata_done;
	}
	else {
		deque_p = prev_p;
		tail--;

		ADD_EXECUTED_TASKS(1);
		ws_input_t *in = &prev_p->input;
		return handler(prev_p->metadata & TASK_TYPEFILTER,
			in->a, in->b, in->c, in->lvl);
	}
}

ws_comp_out_t ws_initiate(ws_metadata_t metadata, uint64_t a, uint64_t b, uint64_t c, uint32_t lvl) {
	ws_comp_out_t result;
	*term_p = 0; // reset termination flag
	upc_barrier;

	double start = wctime();
	result.output = ws_call(metadata, a, b, c, lvl);
	double stop = wctime();
	result.time = stop - start;

	int i; for (i = 0; i < THREADS; i++) {
		upc_memset_nbi(&term[i], 0xFF, sizeof(uint64_t));
	}

	return result;
}

void ws_participate() {
	*term_p = 0; // reset termination flag
	upc_barrier;

	while (*term_p == 0) {
		int i; for (i = 0; i < 4; i++) {
			int j; for (j = 0; j < victims_n[i]; j++) {
				size_t victim = victims[i][rand() % victims_n[i]];
				if (ws_steal(victim) & WS_STEAL_SUCC) goto test_terminate;
				if (*term_p != 0) return;
			}
		}
	test_terminate:
	}
}

ws_comp_out_t ws_compute(ws_metadata_t metadata, uint64_t a, uint64_t b, uint64_t c, uint32_t lvl) {
	if (MYTHREAD == 0) return ws_initiate(metadata, a, b, c, lvl);
	else ws_participate();
	ws_comp_out_t out;
	return out;
}

void ws_progress() {
	ws_communicate();
}

void ws_statistics() {
	#ifdef WS_USE_STATS
	printf("%i/%i - victim array sizes: (%lu, %lu, %lu, %lu)\n", MYTHREAD, THREADS, victims_n[0], victims_n[1], victims_n[2], victims_n[3]);
	printf("%i/%i - nr. of steal attempts = %llu\n", MYTHREAD, THREADS, _steal_attempts);
	printf("%i/%i - nr. of successful steals = %llu\n", MYTHREAD, THREADS, _steals);
	printf("%i/%i - nr. of failed steal attempts (due to occupation) = %llu\n", MYTHREAD, THREADS, _failed_steals);
	printf("%i/%i - nr. of empty steal attempts = %llu\n", MYTHREAD, THREADS, _empty_steals);
	printf("%i/%i - nr. of blocked steal attempts = %llu\n", MYTHREAD, THREADS, _blocked_steals);
	printf("%i/%i - tasks executed: %llu\n", MYTHREAD, THREADS, _executed);
	#endif
}
