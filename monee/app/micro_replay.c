#include "app/micro_replay.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <types.h>
#include <bitmap.h>
#include <rbtree.h>
#include "mem/malloc.h"
#include "peep/tb.h"
#include "sys/rr_log.h"
#include "sys/vcpu.h"

#define MICRO_REPLAY_FREQUENCY 0x1000000

#define MICRO_REPLAY_LAST_INTR 											0
#define MICRO_REPLAY_LAST_INTR_FROM_FAULTING_THREAD 1

#ifndef MICRO_REPLAY_MODE
#define MICRO_REPLAY_MODE MICRO_REPLAY_LAST_INTR
#endif

#define MREP_CUMULATIVE
#define MICRO_REPLAY_GEOMETRIC

static off_t rollback_offset = 0;
static uint64_t rollback_n_exec = (uint64_t)-1;
static int rollback_mode = 0;

static bool deterministic_error = false;

#ifdef MICRO_REPLAY_N_INTERRUPTS
static size_t micro_replay_n_interrupts = MICRO_REPLAY_N_INTERRUPTS;
#else
static size_t micro_replay_n_interrupts = 32;
#endif
#define MICRO_REPLAY_N_BLACKLISTS 8
#define BLACKLIST_WINDOW 64

struct mrep_interrupt {
	target_ulong eip;
	uint64_t n_exec;
	off_t offset;
};
static struct mrep_interrupt *last_n_interrupts = NULL;
static size_t n = 0;

/* Helper functions. */
static void blacklisted_eips_insert(target_ulong eip);
static void blacklisted_eips_clear(void);
static void blacklisted_eips_print(void);
static void blacklisted_eips_init(void);
static int blacklisted_eips_size(void);

/* Stats. */
static int num_micro_replays = 0;

void
micro_replay_init(void)
{
	blacklisted_eips_init();
}

static void
micro_replay_callback(char const *rr_tag, bool replay, void *opaque)
{
	ASSERT(!opaque);
	if (!strcmp(rr_tag, "MREP")) {
		printf("%s() %d: %llx: %s\n", __func__, __LINE__, vcpu.n_exec, rr_tag);
		micro_replay_switch_mode();
		NOT_REACHED();
	}
	if (rollback_mode != 1) {
		return;
	}
	//printf("%s(): %llx: %s\n", __func__, vcpu.n_exec, rr_tag);
	ASSERT(vcpu.replay_log);
	ASSERT(!vcpu.record_log);
	if (   !strcmp(rr_tag, "IN")  || !strcmp(rr_tag, "INS")
			|| !strcmp(rr_tag, "OUT") || !strcmp(rr_tag, "OUTS")) {
		rollback_offset = ftello(vcpu.replay_log);
		rollback_n_exec = get_n_exec(vcpu.callout_next);
	}
	if (!strcmp(rr_tag, "INTR")) {
		/* Record offending instruction pointers here. */
		ASSERT(last_n_interrupts);
#if MICRO_REPLAY_MODE == MICRO_REPLAY_LAST_INTR
		//printf("%s() %d: %llx: %s\n", __func__, __LINE__, vcpu.n_exec, rr_tag);
		last_n_interrupts[n].eip = (target_ulong)vcpu.eip_executing;
		last_n_interrupts[n].offset = ftello(vcpu.replay_log);
		last_n_interrupts[n].n_exec = get_n_exec(vcpu.callout_next);
		n = (n + 1) % micro_replay_n_interrupts;
#elif MICRO_REPLAY_MODE == MICRO_REPLAY_LAST_INTR_FROM_FAULTING_THREAD
		NOT_IMPLEMENTED();
#endif
	}
}

static void
decide_rollback_point_and_fill_blacklisted_eips(void)
{
	size_t i, rollback_point;

#ifndef MREP_CUMULATIVE
	blacklisted_eips_clear();
#endif
	rollback_point = (n + 1) % micro_replay_n_interrupts;
	if (last_n_interrupts[rollback_point].eip == 0) {
		rollback_point = 1;
	}
	if (rollback_n_exec < last_n_interrupts[rollback_point].n_exec) {
		rollback_n_exec = last_n_interrupts[rollback_point].n_exec;
		rollback_offset = last_n_interrupts[(rollback_point - 1)
			% micro_replay_n_interrupts].offset;
	}
	for (i = rollback_point; i != n; i = (i + 1) % micro_replay_n_interrupts) {
		if (!last_n_interrupts[i].eip) {
			printf("Hitting error deterministically (n_interrupts = 0).\n");
			deterministic_error = true;
			break;
		}
		if (   (i < n && n - i < MICRO_REPLAY_N_BLACKLISTS)
				|| (i > n && n + micro_replay_n_interrupts - i < MICRO_REPLAY_N_BLACKLISTS)) {
			printf("Blacklisting eip %x\n", last_n_interrupts[i].eip);
			blacklisted_eips_insert(last_n_interrupts[i].eip);
		}
	}
	blacklisted_eips_print();
}

void
micro_replay_switch_mode(void)
{
	bool first_replay;
	int seek;

	if (!vcpu.record_log && !vcpu.replay_log) {
		return;
	}
	if (vcpu.record_log && vcpu.replay_log) {
		return;
	}
	if (deterministic_error) {
		return;
	}
	if (vcpu.replay_log && rollback_mode == 2) {
		/* We have rolled back successfully. */
		ASSERT(get_n_exec(vcpu.callout_next) == rollback_n_exec);
		seek = fseeko(vcpu.replay_log, record_log_disk_begin, SEEK_SET);
		ASSERT(seek == 0);
		rr_log_unregister_callback(micro_replay_callback, NULL);
		vcpu.record_log = vcpu.replay_log;
		vcpu.replay_log = NULL;
		vcpu.n_exec = rollback_n_exec;
		vcpu.callout_next = NULL;
		rollback_mode = 0;

		num_micro_replays++;
	} else {
		first_replay = rollback_mode == 1 && vcpu.replay_log;
		if (vcpu.record_log || first_replay) {
			if (first_replay) {
				/* Decide rollback point, fill blacklisted eips, and
				 * start replay (again). */
				decide_rollback_point_and_fill_blacklisted_eips();
				printf("%llx: Fixed rollback_n_exec at %llx, replaying again "
						"at offset %llx.\n", get_n_exec(vcpu.callout_next),
						rollback_n_exec, rollback_offset);
				seek = fseeko(vcpu.replay_log, rollback_offset - RR_LOG_ENTRY_SIZE,
						SEEK_SET);
				ASSERT(seek == 0);
				vcpu.record_log = vcpu.replay_log;
				vcpu.n_exec = rollback_n_exec;
				vcpu.callout_next = NULL;
			}
			record_log_finish("MREP");
			seek = fseeko(vcpu.record_log, record_log_disk_begin, SEEK_SET);
			ASSERT(seek == 0);
			if (first_replay) {
				vcpu.record_log = NULL;
				rollback_mode = 2;
			}
		}

		if (vcpu.record_log) {
			ASSERT(!vcpu.replay_log);
			vcpu.replay_log = vcpu.record_log;
			vcpu.record_log = NULL;
			rr_log_register_callback(micro_replay_callback, NULL);
			rollback_mode = 1;
		}
	}
	rr_log_start();
	if (rollback_mode == 1) {
		rollback_offset = ftello(vcpu.replay_log);
		rollback_n_exec = vcpu.n_exec;
#ifdef MICRO_REPLAY_GEOMETRIC
		//micro_replay_n_interrupts *= 2;
#elif defined(MICRO_REPLAY_LINEAR)
		micro_replay_n_interrupts++;
#endif
		ASSERT(!last_n_interrupts);
		last_n_interrupts =
			tb_pool_malloc(micro_replay_n_interrupts*sizeof(struct mrep_interrupt));
		ASSERT(last_n_interrupts);
		memset(last_n_interrupts, 0,
				micro_replay_n_interrupts * sizeof(struct mrep_interrupt));
		n = 0;
	} else if (rollback_mode == 2) {
		free(last_n_interrupts);
		last_n_interrupts = NULL;
	} else if (rollback_mode == 0) {
		printf("Entering live mode.\n");
	}
	longjmp(vcpu.jmp_env, 3);
}

void
check_micro_replay(void)
{
	static long long last_micro_replay = 0;
	if (!vcpu.record_log) {
		return;
	}
	if (vcpu.n_exec - last_micro_replay > MICRO_REPLAY_FREQUENCY) {
		uint64_t cur_n_exec = get_n_exec(vcpu.callout_next);
		printf("%llx: Micro-replaying.\n", cur_n_exec);
		last_micro_replay = cur_n_exec;
		micro_replay_switch_mode();
	}
}

void
micro_replay_print_stats(void)
{
	printf("MON-STATS: Number of micro replays: %d\n", num_micro_replays);
	printf("MON-STATS: Size of blacklisted memory: %d\n",blacklisted_eips_size());
}

#define MAX_BLACKLISTED_EIPS 1024
struct blacklisted_eip {
	target_ulong begin, end;
	struct rbtree_elem rb_elem;
};

static struct blacklisted_eip blacklisted_eips[MAX_BLACKLISTED_EIPS];
struct bitmap *alloc_bitmap = NULL;
struct rbtree rbtree;

static bool
blacklisted_eip_struct_less(struct rbtree_elem const *a_,
		struct rbtree_elem const *b_, void *aux)
{
	struct blacklisted_eip *a = rbtree_entry(a_, struct blacklisted_eip,
			rb_elem);
	struct blacklisted_eip *b = rbtree_entry(b_, struct blacklisted_eip,
			rb_elem);
	if (a->end < b->begin) {
		return true;
	}
	return false;
}

static void
blacklisted_eips_init(void)
{
	alloc_bitmap = bitmap_create(MAX_BLACKLISTED_EIPS);
	ASSERT(alloc_bitmap);
	blacklisted_eips_clear();
	rbtree_init(&rbtree, blacklisted_eip_struct_less, NULL);
}

static void
blacklisted_eips_clear(void)
{
	ASSERT(alloc_bitmap);
	bitmap_set_all(alloc_bitmap, false);
}

static void
blacklisted_eips_print(void)
{
	struct rbtree_elem *e;

	printf("Blacklisted eips:");
	for (e = rbtree_begin(&rbtree); e != rbtree_end(&rbtree);
			e = rbtree_next(e)) {
		struct blacklisted_eip *b;
		b = rbtree_entry(e, struct blacklisted_eip, rb_elem);
		if (b->begin == b->end) {
			printf(" 0x%x", b->begin);
		} else {
			printf(" 0x%x-%x", b->begin, b->end);
		}
	}
	/*
	size_t i = 0;
	ASSERT(alloc_bitmap);
	printf("Blacklisted eips:");
	while ((i = bitmap_scan(alloc_bitmap, i, 1, true)) != BITMAP_ERROR) {
		if (blacklisted_eips[i].begin == blacklisted_eips[i].end) {
			printf(" 0x%x", blacklisted_eips[i].begin);
		} else {
			printf(" 0x%x-%x", blacklisted_eips[i].begin, blacklisted_eips[i].end);
		}
		i++;
	}
	*/
	printf(".\n");
}

static void
blacklisted_eips_insert(target_ulong eip)
{
	size_t i;
	struct blacklisted_eip needle, *found;
	struct rbtree_elem *e;

	needle.begin = eip - BLACKLIST_WINDOW;
	needle.end = eip + BLACKLIST_WINDOW;
	if (e = rbtree_find(&rbtree, &needle.rb_elem)) {
		found = rbtree_entry(e, struct blacklisted_eip, rb_elem);
		found->begin = min(eip, found->begin);
		found->end = max(eip, found->end);
		return;
	}
	ASSERT(alloc_bitmap);
	i = bitmap_scan_and_flip(alloc_bitmap, 0, 1, false);
	ASSERT(i != BITMAP_ERROR);
	blacklisted_eips[i].begin = eip;
	blacklisted_eips[i].end = eip;
	rbtree_insert(&rbtree, &blacklisted_eips[i].rb_elem);
}

static int
blacklisted_eips_size(void)
{
#define AVG_INSN_SIZE 4
	struct rbtree_elem *e;
	int ret = 0;
	for (e = rbtree_begin(&rbtree); e != rbtree_end(&rbtree);
			e = rbtree_next(e)) {
		struct blacklisted_eip *b;
		b = rbtree_entry(e, struct blacklisted_eip, rb_elem);
		if (b->begin != b->end) {
			ret += b->end - b->begin;
		}
		ret += AVG_INSN_SIZE;
	}
	return ret;
}

bool
interrupts_black_listed_eip(target_ulong eip)
{
	size_t i = 0;
	struct blacklisted_eip needle;
	needle.begin = eip;
	needle.end = eip;

	if (rbtree_find(&rbtree, &needle.rb_elem)) {
		return true;
	}
	return false;
}
