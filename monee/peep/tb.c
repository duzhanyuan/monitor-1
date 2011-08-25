#include "peep/tb.h"
#include <debug.h>
#include <types.h>
#include <random.h>
#include <string.h>
#include <stdio.h>
#include "mem/malloc.h"
#include "mem/malloc_cb.h"
#include "mem/palloc.h"
#include "mem/pt_mode.h"
#include "mem/mtrace.h"
#include "mem/vaddr.h"
#include "peep/jumptable1.h"
#include "peep/jumptable2.h"
#include "peep/tb_exit_callbacks.h"
#include "sys/vcpu.h"

#define TB 2

//tb_t *debug_tb;
//void *debug_esp;

/* Statistics. */
static long long tb_num_replacements = 0;
static long long tb_translation_cache_size_sum = 0;
static unsigned tb_translation_cache_size_min = UINT32_MAX;
static unsigned tb_translation_cache_size_max = 0;

static struct hash pc_table;
static struct rbtree tc_tree;
static unsigned nb_tbs;

static bool pc_equal(struct hash_elem const *a, struct hash_elem const *b,
    void *aux);
static bool tc_less(struct rbtree_elem const *a, struct rbtree_elem const *b,
    void *aux);
static unsigned pc_hash(struct hash_elem const *_a, void *aux);
static void tb_reset_jump(tb_t *tb, unsigned n);
static void tb_unchain(tb_t *tb);
static void tb_mtrace(target_phys_addr_t start, size_t len, void *opaque);
static void tb_pool_lock(void *opaque);
static void tb_pool_unlock(void *opaque);
static tb_t * tb_find_replacement(void);
static void tb_free(void *opaque);
static void tb_jmp_remove(tb_t *tb, unsigned n);

static struct list clock_list;
static struct list_elem *clock_hand;

struct malloc_cb tb_malloc_cb = {
	&tb_pool_malloc,
	&tb_pool_lock,
	&tb_pool_unlock
};

void
tb_init(void)
{
  hash_init(&pc_table, &pc_hash, &pc_equal, NULL);
  rbtree_init(&tc_tree, &tc_less, NULL);
  nb_tbs = 0;

  list_init(&clock_list);
  /* Initialize clock hand to the tail of the list.
   * The tail is always an invalid TB.
   */
  clock_hand = list_end(&clock_list);
	tb_exit_callbacks_init();
}


static void
tb_jmp_remove(tb_t *tb, unsigned int n)
{
  tb_t *tb1, **ptb;
  unsigned int n1;

  ptb = &tb->jmp_next[n];
  tb1 = *ptb;
  if (tb1) {
    /* find tb(n) in circular list */
    for (;;) {
      tb1 = *ptb;
      n1 = (long)tb1 & 3;
      tb1 = (tb_t *)((long)tb1 & ~3);
      if (n1 == n && tb1 == tb)
        break;
      if (n1 == 2) {
        ptb = &tb1->jmp_first;
      } else {
        ptb = &tb1->jmp_next[n1];
      }
    }
    /* now we can suppress tb(n) from the list */
    *ptb = tb->jmp_next[n];
    tb->jmp_next[n] = NULL;
    tb_reset_jump(tb, n);
  }
}

static void
tb_tc_print(struct rbtree_elem const *elem, void *aux)
{
  tb_t *tb = rbtree_entry(elem, tb_t, tc_elem);
  printf("%p->%p\n", tb->tc_ptr, tb->tc_ptr + tb->tc_boundaries[tb->num_insns]);
}

#define NUM_BIN_CHARS 33
#define NUM_AS_CHARS 30
void
tb_print_in_asm(tb_t const *tb, unsigned size)
{
  char *ptr = (char *)tb->eip_phys;
  target_ulong cur_addr;
  pt_mode_t pt_mode;
  unsigned n;
  printf("IN:\n");
  pt_mode = switch_to_phys();
  for (n = 0; n < tb->num_insns; n++) {
    size_t dlen, num_bin_chars = NUM_BIN_CHARS, num_as_chars = NUM_AS_CHARS;
    static char str[128];
    unsigned i;
    cur_addr = (ptr - (char *)tb->eip_phys) + tb->eip_virt;

    //printf("size=%d\n", size);
    dlen = sprint_insn(ptr, str, sizeof str, size, true);
    printf("%08x:", cur_addr);
    for (i = 0; i < dlen; i++) {
      printf(" %02hhx", ptr[i]);
    }
    for (i = dlen*3; i < num_bin_chars; i++) {
      printf(" ");
    }
    printf(": %s", str);
    ptr += dlen;
    for (i = strlen(str) + 2; i < num_as_chars; i++) {
      printf(" ");
    }
#ifndef NDEBUG
    if (tb->peep_string[n]) {
      printf("%s", tb->peep_string[n]);
    }
#endif
    printf("\n");
  }
  switch_pt(pt_mode);
}

void
print_asm(uint8_t *buf, size_t len, unsigned size)
{
	uint8_t *ptr = buf;
#define NUM_BIN_CHARS 33
#define NUM_AS_CHARS 30
	while (ptr - buf < (int)len) {
		static char str[128];
		size_t dlen, num_bin_chars = NUM_BIN_CHARS, num_as_chars = NUM_AS_CHARS;
		target_ulong cur_addr = (target_ulong)ptr;
		unsigned i;

		dlen = sprint_insn(ptr, str, sizeof str, size, false);
		printf("%08x:", cur_addr);
		for (i = 0; i < dlen; i++) {
			printf(" %02hhx", ptr[i]);
		}
		for (i = dlen*3; i < num_bin_chars; i++) {
			printf(" ");
		}
		printf(": %s", str);
		for (i = strlen(str) + 2; i < num_as_chars; i++) {
			printf(" ");
		}
		printf("\n");
		ptr += dlen;
	}
}

void
tb_print_out_asm(tb_t const *tb)
{
  char *disas_ptr = tb->tc_ptr;
  size_t const  num_bin_chars = NUM_BIN_CHARS;
  size_t tlen = tb->tc_boundaries[tb->num_insns];
  unsigned target_size = 4;     // on target, we always use protected mode.

  printf("OUT:\n");
  while (disas_ptr - (char *)tb->tc_ptr < (int)tlen) {
    static char str[128];
    size_t i, dlen;
    str[0] = '\0';

    dlen = sprint_insn(disas_ptr, str, sizeof str, target_size, false);
    printf("%p:", disas_ptr);
    for (i = 0; i < dlen; i++) {
      printf(" %02hhx", disas_ptr[i]);
    }
    ASSERT(dlen <= num_bin_chars);
    for (i = dlen*3; i < num_bin_chars; i++) {
      printf(" ");
    }
    printf(": %s\n", str);
    disas_ptr += dlen;
  }
  ASSERT(disas_ptr - (char *)tb->tc_ptr == (int)tlen);
}

void
tb_print_rb_asm(tb_t const *tb)
{
  size_t const  num_bin_chars = NUM_BIN_CHARS;
  bool rb_seen = false;
  struct rollbacks_t *rollbacks = tb->rollbacks;
  unsigned target_size = 4;     // on target, we always use protected mode.
  int n_in = tb->num_insns;
  int n;

  for (n = 0; n < n_in; n++) {
    if (rollbacks[n].buf_size == 0) {
      continue;
    }
    char *disas_ptr = rollbacks[n].buf;
    char *disas_end = disas_ptr + rollbacks[n].buf_size;
    int cur_rb = rollbacks[n].nb_rollbacks - 1;
    ASSERT(rollbacks[n].nb_rollbacks);
    if (!rb_seen) {
      printf("RB:\n");
      rb_seen = true;
    }

    while (disas_ptr < disas_end) {
      static char str[128];
      size_t i, dlen;
      str[0] = '\0';

      if (cur_rb >= 0 && disas_ptr - (char *)rollbacks[n].buf
          >= rollbacks[n].rb_offset[cur_rb]) {
        printf("%d: %p:\n", n, (char *)tb->tc_ptr
            + tb->tc_boundaries[n] + rollbacks[n].code_offset[cur_rb]);
        cur_rb--;
      }
      printf("   %p:", disas_ptr);
      dlen = sprint_insn(disas_ptr, str, sizeof str, target_size, false);
      for (i = 0; i < dlen; i++) {
        printf(" %02hhx", disas_ptr[i]);
      }
      ASSERT(dlen <= num_bin_chars);
      for (i = dlen*3; i < num_bin_chars; i++) {
        printf(" ");
      }
      printf(": %s\n", str);
      disas_ptr += dlen;
    }
    ASSERT(disas_ptr == disas_end);
  }
}

static void
tb_mtrace(target_phys_addr_t start, size_t len, void *opaque)
{
  tb_t *tb = (tb_t *)opaque;
  if (start + len > tb->eip_phys && tb->eip_phys + tb->tb_len > start) {
		//truly over-written
	} else {
		//false sharing
	}
	//in both cases invalidate tb to avoid future trace-faults.
	LOG(MTRACE, "%s(): %x %x. freeing tb [%x-%x] \n", __func__, start, start+len,
			tb->eip_phys, tb->eip_phys + tb->tb_len);
	if (vcpu.callout_next && tb_find(vcpu.callout_next) == tb) {
		tb_unchain(tb);
		jumptable2_remove(tb);
		jumptable1_remove(tb->eip);
		register_tb_exit_callback(tb_free, tb, &tb_malloc_cb);
	} else {
		tb_free(tb);
	}
}

static void
tb_mtrace_add_remove(tb_t *tb, mtrace_add_remove_fn *fn)
{
	return;			//XXX : adding mtrace on every tb really slows things down.
	if (tb->eip_phys_end_page == (tb->eip_phys & ~PGMASK)) {
		fn(tb->eip_phys, tb->tb_len, tb_mtrace, tb, &tb_malloc_cb);
	} else {
		target_phys_addr_t eip_phys_page_end, eip_phys_page_len;
		eip_phys_page_end = (tb->eip_phys & ~PGMASK) + PGSIZE;
		eip_phys_page_len = eip_phys_page_end - tb->eip_phys;
		ASSERT(((tb->eip_phys + tb->tb_len) & ~PGMASK) == eip_phys_page_end);
		ASSERT(eip_phys_page_len < tb->tb_len);
		fn(tb->eip_phys, eip_phys_page_len, tb_mtrace, tb,
				&tb_malloc_cb);
		fn(tb->eip_phys_end_page, tb->tb_len - eip_phys_page_len,
				tb_mtrace, tb, &tb_malloc_cb);
	}
}

static void
tb_mtrace_add(tb_t *tb)
{
	tb_mtrace_add_remove(tb, mtrace_add);
}

static void
tb_mtrace_remove(tb_t *tb)
{
	tb_mtrace_add_remove(tb, mtrace_remove);
}

static void
tb_free(void *opaque)
{
	tb_t *tb;
  void *retp;
  bool retb;
  unsigned i;

	tb = (tb_t *)opaque;
	ASSERT(tb == tb_find(tb->tc_ptr));
	LOG(TB, "%s(): freeing %p: 0x%x-0x%x: %p->%p.\n", __func__, tb, tb->eip_phys,
			tb->eip_phys + tb->tb_len, tb->tc_ptr,
			tb->tc_ptr + tb->tc_boundaries[tb->num_insns]);
  tb_unchain(tb);
  jumptable2_remove(tb);
  jumptable1_remove(tb->eip);
  tb_trace_freed(tb);
  //callout_patches_tb_free(tb);
  retp = hash_delete(&pc_table, &tb->pc_elem);
  ASSERT(retp);
  rbtree_delete(&tc_tree, &tb->tc_elem);
  list_remove(&tb->clock_elem);	//could be double-called but that's
																					//no problem.
  //rbtree_inorder(&tc_tree, tb_tc_print, NULL);
  nb_tbs--;
	tb_mtrace_remove(tb);
  free(tb->tc_ptr);
  free(tb->tc_boundaries);
  free(tb->eip_boundaries);
  for (i = 0; i < tb->num_insns; i++) {
    if (tb->rollbacks[i].buf) {
      free(tb->rollbacks[i].buf);
    }
    if (tb->rollbacks[i].code_offset) {
      free(tb->rollbacks[i].code_offset);
    }
    if (tb->rollbacks[i].rb_offset) {
      free(tb->rollbacks[i].rb_offset);
    }
  }
  free(tb->rollbacks);
#ifndef NDEBUG
  for (i = 0; i < tb->num_insns; i++) {
    if (tb->peep_string[i]) {
      free(tb->peep_string[i]);
    }
  }
  free(tb->peep_string);
#endif
  free((char *)tb - tb->alignment);

	/* Update stats. */
	tb_translation_cache_size_min = min(tb_translation_cache_size_min, nb_tbs+1);
	tb_translation_cache_size_max = max(tb_translation_cache_size_max, nb_tbs+1);
	tb_translation_cache_size_sum += nb_tbs;
	tb_num_replacements++;
}

static bool
free_a_tb(void)
{
  tb_t *replacement;

  /* Cache full. Need replacement. */
  if (replacement = tb_find_replacement()) {
		DBGn(TB, "replacing %p: %#x: %p->%p.\n", replacement,
				replacement->eip_phys, replacement->tc_ptr,
				replacement->tc_ptr+replacement->tc_boundaries[replacement->num_insns]);
		tb_free(replacement);
		return true;
	}
	return false;
}

void *
tb_pool_malloc(size_t size)
{
  void *ret;
  while (!(ret = malloc_from_pool(POOL_TC, size))) {
    if (!free_a_tb()) {
			PANIC("out of tc-memory.");
		}
  }
  ASSERT(ret);
  return ret;
}

tb_t *
tb_malloc(target_ulong eip, target_ulong eip_virt, target_phys_addr_t eip_phys,
		target_phys_addr_t eip_phys_end_page, size_t num_insns, size_t size,
		rollbacks_t const *rollbacks)
{
  int alignment;
  void *alloc;
  tb_t *tb;
  unsigned i;

  /* Uncomment to test cache replacement.
  if (nb_tbs) {
    free_a_tb();
  }
  */
	//printf("%s(): %x %x %x\n", __func__, eip, eip_virt, eip_phys);
  alloc = tb_pool_malloc(sizeof *tb + 3);
  tb = (void *)(((unsigned long)alloc + 3) & ~3);
  ASSERT(((unsigned long)tb & 3) == 0);
  alignment = (unsigned long)tb - (unsigned long)alloc;
  ASSERT(alignment >= 0 && alignment < 4);
  tb->alignment = alignment;
  tb->eip = eip;
  tb->eip_virt = eip_virt;
  tb->eip_phys = eip_phys;
  tb->eip_phys_end_page = eip_phys_end_page;
  tb->jmp_first = (void *)((long)tb | 2);
  tb->jmp_next[0] = tb->jmp_next[1] = NULL;
  tb->jmp_offset[0] = tb->jmp_offset[1] = 0xffff;
  tb->edge_offset[0] = tb->edge_offset[1] = 0xffff;
	tb_pool_lock(tb);
  tb->rollbacks = tb_pool_malloc(num_insns * sizeof (*tb->rollbacks));
  ASSERT(tb->rollbacks);
  if (!tb->rollbacks) {
    ABORT();
  }

  tb->tc_ptr = tb_pool_malloc(size);
  tb->tc_boundaries = tb_pool_malloc((num_insns + 1) *
      sizeof(*tb->tc_boundaries));
  tb->eip_boundaries = tb_pool_malloc(num_insns * sizeof(*tb->eip_boundaries));
#ifndef NDEBUG
  tb->peep_string = tb_pool_malloc(num_insns * sizeof(char *));
  memset(tb->peep_string, 0x0, num_insns*sizeof(char*));
#endif
  tb->num_insns = num_insns;

  if (!tb->rollbacks) {
    ABORT();
  }

  for (i = 0; i < num_insns; i++) {
    tb->rollbacks[i].buf_size = rollbacks[i].buf_size;
    if (rollbacks[i].buf_size) {
      tb->rollbacks[i].buf = tb_pool_malloc(rollbacks[i].buf_size);
      ASSERT(rollbacks[i].nb_rollbacks);
      tb->rollbacks[i].code_offset = tb_pool_malloc(
          rollbacks[i].nb_rollbacks * sizeof(uint16_t));
      tb->rollbacks[i].rb_offset = tb_pool_malloc(
          rollbacks[i].nb_rollbacks * sizeof(uint16_t));
      tb->rollbacks[i].nb_rollbacks = rollbacks[i].nb_rollbacks;
    } else {
      tb->rollbacks[i].buf = NULL;
      tb->rollbacks[i].code_offset = NULL;
      tb->rollbacks[i].rb_offset = NULL;
      tb->rollbacks[i].nb_rollbacks = 0;
    }
  }
  if (!tb->rollbacks) {
    ABORT();
  }
	tb_pool_unlock(tb);

  /* printf("%s(): returning tb(%p). tb->rollbacks(%p)=%p\n", __func__, tb,
			&tb->rollbacks, tb->rollbacks); */
  return tb;
}


static tb_t *pool_locked_tb = NULL;
static int num_pool_locked = 0;
static void
tb_pool_lock(void *opaque)
{
	ASSERT(num_pool_locked == 0);
	ASSERT(pool_locked_tb == NULL);
	pool_locked_tb = opaque;
	num_pool_locked = 1;
}

static void
tb_pool_unlock(void *opaque)
{
	ASSERT(num_pool_locked == 1);
	ASSERT(pool_locked_tb == opaque);
	pool_locked_tb = NULL;
	num_pool_locked = 0;
}

static tb_t *
tb_find_replacement(void)
{
//#ifdef TB_REPLACEMENT_RANDOM
#if 1
  struct hash_iterator i;
  struct tb_t *tb = NULL;
  unsigned r;
  ASSERT(nb_tbs > 0);

  if (pool_locked_tb && nb_tbs == 1) {
    return NULL;
  }

  r = random_ulong()%(unsigned)(nb_tbs - (pool_locked_tb?1:0));
	r++;

  hash_first(&i, &pc_table);
  while (r > 0) {
    if (!hash_next(&i)) {
      /* wrap around. */
      hash_first(&i, &pc_table);
      hash_next(&i);
    }
    tb = hash_entry(hash_cur(&i), struct tb_t, pc_elem);
    if (tb != pool_locked_tb) {
      r--;
    }
  }
	ASSERT(tb);
  return tb;
#else
	/* Need to take care of locked tbs here. */
  struct tb_t *tb;
  while (1) {
    if (clock_hand == list_end(&clock_list)) {
      clock_hand = list_begin(&clock_list);
    }
    if (clock_hand == list_end(&clock_list)) {
      return NULL;
    }
    tb = list_entry(clock_hand, struct tb_t, clock_elem);
    if (!tb->accessed_bit && !fcallouts_tb_active(tb)) {
      clock_hand = list_remove(&tb->clock_elem);
      return tb;
    } else {
      tb->accessed_bit = false;
      clock_hand = list_next(clock_hand);
      /* Unchain the tb, so that it's accessed bit can be set the next
       * time it is accessed. */
      tb_unchain(tb);
    }
  }
  NOT_REACHED();
#endif
}


void
tb_add(tb_t *tb)
{
	void *retp;
	LOG(TB, "%s(): adding %p: 0x%x-0x%x: %p->%p.\n", __func__, tb, tb->eip_phys,
			tb->eip_phys + tb->tb_len, tb->tc_ptr,
			tb->tc_ptr + tb->tc_boundaries[tb->num_insns]);
	retp = hash_insert(&pc_table, &tb->pc_elem);
	if (retp) {
		tb_t *tmp;
		tmp = tb_find_pc(tb->eip_phys, tb->eip_phys_end_page, tb->eip_virt,
				(target_ulong)tb->eip);
		printf("eip_phys=%x, eip_phys_end_page=%x, eip_virt=%x, "
				"eip=%x, tmp=%p\n", tb->eip_phys, tb->eip_phys_end_page, tb->eip_virt,
				tb->eip, tmp);
	}
	ASSERT(!retp);
	ASSERT(!rbtree_find(&tc_tree, &tb->tc_elem));
  rbtree_insert(&tc_tree, &tb->tc_elem);
  //rbtree_inorder(&tc_tree, tb_tc_print, NULL);
  tb->accessed_bit = true;
  list_push_back(&clock_list, &tb->clock_elem);
	tb_mtrace_add(tb);
  nb_tbs++;
}

tb_t *
tb_find_pc(target_ulong eip_phys, target_ulong eip_phys_end_page,
		target_ulong eip_virt, target_ulong eip)
{
  struct tb_t needle;
  struct hash_elem *e;
  struct tb_t *tb;

  needle.eip_phys = eip_phys;
  needle.eip_phys_end_page = eip_phys_end_page;
  needle.eip_virt = eip_virt;
  needle.eip = eip;
  e = hash_find(&pc_table, &needle.pc_elem);
  if (e) {
    tb = hash_entry(e, struct tb_t, pc_elem);
    ASSERT(tb_find(tb->tc_ptr));
    return tb;
  }
	/*
  if (inum) {
    struct hash_iterator i;
    ASSERT(vcpu.replay);
    *inum = 0;
    hash_first(&i, &pc_table);
    while (hash_next(&i)) {
      struct tb_t *tb;
      tb = hash_entry(hash_cur(&i), struct tb_t, pc_elem);
      if (   eip_phys >= tb->eip_phys && eip_phys < tb->eip_phys + tb->tb_len
          && eip_virt >= tb->eip_virt && eip_virt < tb->eip_virt + tb->tb_len) {
        int n;
        for (n = 0; n < tb->num_insns - 1; n++) {
          if (eip_phys == tb->eip_phys + tb->eip_boundaries[n]) {
            ASSERT(eip_virt == tb->eip_virt + tb->eip_boundaries[n]);
            *inum = n+1;
            ASSERT(tb_find(tb->tc_ptr));
            return tb;
          }
        }
      }
    }
  }
	*/
  return NULL;
}

tb_t *
tb_find(const void *tc_ptr)
{
  uint16_t tc_boundaries[1] = {1};
  struct tb_t tb = {0, 0, 0, 0, 0, 0, NULL, NULL, tc_boundaries, };
  struct rbtree_elem *found;
  struct tb_t *ret;

  tb.tc_ptr = (void *)tc_ptr;
  if (!(found = rbtree_find(&tc_tree, &tb.tc_elem))) {
    return NULL;
  }
	/* The entries in tc_tree should always be unique. */
	ASSERT(   rbtree_next(found) == rbtree_end(&tc_tree)
			   || tc_less(found, rbtree_next(found), NULL));
  ret = rbtree_entry(found, struct tb_t, tc_elem);
  return ret;
}

static void
tb_unchain(tb_t *tb)
{ 
  tb_t *tb1, *tb2;

	//printf("%s(): %p: 0x%x\n", __func__, tb, tb->eip_virt);
  /* suppress this TB from the two jump lists */
  tb_jmp_remove(tb, 0);
  tb_jmp_remove(tb, 1);

  /* suppress any remaining jumps to this TB */
  for (tb1 = tb->jmp_first;;tb1 = tb2) {
    unsigned n1;
    n1 = (long)tb1 & 3;
    if (n1 == 2) {
      tb->jmp_first = tb1;
      break;
    }
    tb1 = (void *)((unsigned long)tb1 & ~3);
    //printf("Unlinking jump chain %#x->%#x\n",tb1->eip_phys,tb->eip_phys);
    tb2 = tb1->jmp_next[n1];
		//printf("tb=%p, tb1=%p, n1=%d, tb2=%p\n", tb, tb1, n1, tb2);
    tb_reset_jump(tb1, n1);
    tb1->jmp_next[n1] = NULL;
  }
}

void
tb_unchain_all(void)
{
  struct hash_iterator i;
  hash_first(&i, &pc_table);
  while (hash_next(&i)) {
    struct tb_t *tb;
    tb = hash_entry(hash_cur(&i), struct tb_t, pc_elem);
    tb_unchain(tb);
  }
}

static void
tb_set_jmp_target(tb_t *tb, unsigned n, uint8_t *addr)
{
  unsigned long val;
	uint8_t *jmp_addr;
	unsigned i;
  ASSERT(tb->jmp_offset[n] < tb->tc_boundaries[tb->num_insns]);
  jmp_addr = tb->tc_ptr + tb->jmp_offset[n];
	val = addr - (jmp_addr + sizeof(target_ulong));
  *(target_ulong *)jmp_addr = val;
	for (i = 0; i < sizeof(target_ulong); i++) {
		fcallouts_tc_write(jmp_addr+i, (val>>(i*8))&0xff);
	}
	//printf("%s(): Setting %p to 0x%x\n", __func__, (void *)jmp_addr, *(target_ulong *)jmp_addr);
}

void
tb_add_jump(tb_t *tb, unsigned n, tb_t *tb_next)
{
  ASSERT(n == 0 || n == 1);
  ASSERT(tb->jmp_offset[n] != 0xffff);
  LOG(IN_ASM, "Chaining %#x[n=%d,%p]-->%#x[%p]\n", tb->eip_phys, n,
      tb->tc_ptr + tb->jmp_offset[n], tb_next->eip_phys, tb_next->tc_ptr);
#ifndef NO_CHAINING
  tb_set_jmp_target(tb, n, tb_next->tc_ptr);
  tb->jmp_next[n] = tb_next->jmp_first;
  tb_next->jmp_first = (tb_t *)((long)(tb) | (n));
	/*
	printf("%s(): tb=%p[0x%x], tb->jmp_first=%p, tb->jmp_next[%d]=%p\n",
			__func__, tb, tb->eip_virt, tb->jmp_first, n, tb->jmp_next[n]);
			*/
#endif
}

static void
tb_reset_jump(tb_t *tb, unsigned n)
{
  ASSERT(tb->edge_offset[n] != 0xffff);
  tb_set_jmp_target(tb, n, tb->tc_ptr + tb->edge_offset[n]);
}

static bool
pc_equal(struct hash_elem const *_a, struct hash_elem const *_b, void *aux)
{
  struct tb_t *a = hash_entry(_a, struct tb_t, pc_elem);
  struct tb_t *b = hash_entry(_b, struct tb_t, pc_elem);
  if (a->eip_phys == b->eip_phys && a->eip_virt == b->eip_virt
			&& a->eip == b->eip) {
		if (   (a->eip_phys & ~PGMASK) != a->eip_phys_end_page
				&& (b->eip_phys & ~PGMASK) != b->eip_phys_end_page) {
			/* Both tb's span two pages. The second page should be identical. */
			if (a->eip_phys_end_page == b->eip_phys_end_page) {
				return true;
			}
		} else {
			return true;
		}
	}
  return false;
}

static bool
tc_less(struct rbtree_elem const *_a, struct rbtree_elem const *_b, void *aux)
{
  struct tb_t *a = rbtree_entry(_a, struct tb_t, tc_elem);
  struct tb_t *b = rbtree_entry(_b, struct tb_t, tc_elem);
  if ((a->tc_ptr + a->tc_boundaries[a->num_insns]) <= b->tc_ptr) {
    return true;
  }
  return false;
}

static unsigned
pc_hash(struct hash_elem const *_a, void *aux)
{
  struct tb_t *a = hash_entry(_a, struct tb_t, pc_elem);
  return hash_int(a->eip_phys);
}

target_ulong
tb_tc_ptr_to_eip_virt(const void *tcptr)
{
  uint8_t const *tc_ptr;
  size_t i;
  tb_t *tb;

  tc_ptr = tcptr;
  tb = tb_find(tc_ptr);
  ASSERT(tb);
  ASSERT(   tc_ptr >= tb->tc_ptr
         && tc_ptr < tb->tc_ptr + tb->tc_boundaries[tb->num_insns]);

	/*
  if (boundary_only) {
    if (tc_ptr == tb->tc_ptr + tb->tc_boundaries[0]) {
      return tb->eip_virt;
    }
    for (i = 1; i < tb->num_insns; i++) {
      if (tc_ptr == tb->tc_ptr + tb->tc_boundaries[i]) {
        return tb->eip_virt + tb->eip_boundaries[i-1];
      }
    }
    printf("tb->tc_ptr=%#x, tc_ptr=%#x, boundaries=", tb->tc_ptr, tc_ptr);
    for (i = 0; i <= tb->num_insns; i++) {
      printf("%#x,", tb->tc_boundaries[i]);
    }
    printf("\n");
    NOT_REACHED();
  } else
	*/
  if (tc_ptr < tb->tc_ptr + tb->tc_boundaries[0]) {
    return tb->eip_virt;
  }
  for (i = 0; i < tb->num_insns; i++) {
    if (   tc_ptr >= tb->tc_ptr + tb->tc_boundaries[i]
        && tc_ptr <  tb->tc_ptr + tb->tc_boundaries[i + 1]) {
      return tb->eip_virt + ((i == 0)?0:tb->eip_boundaries[i-1]);
    }
  }
  printf("tb->tc_ptr=%p, tc_ptr=%p, boundaries=", tb->tc_ptr, tc_ptr);
  for (i = 0; i <= tb->num_insns; i++) {
    printf("%#x,", tb->tc_boundaries[i]);
  }
  printf("\n");
  NOT_REACHED();
}


uint8_t const *
tb_get_tc_next(tb_t const *tb, uint8_t const *tc_ptr)
{
  unsigned i;
  if (tc_ptr >= tb->tc_ptr && tc_ptr < tb->tc_ptr + tb->tc_boundaries[0]) {
    return tb->tc_ptr + tb->tc_boundaries[0];
  }
  for (i = 0; i < tb->num_insns; i++) {
    if (   tc_ptr >= tb->tc_ptr + tb->tc_boundaries[i]
        && tc_ptr < tb->tc_ptr + tb->tc_boundaries[i + 1]) {
      if (tc_ptr == tb->tc_ptr + tb->tc_boundaries[i]) {
        return tc_ptr;
      }
      return tb->tc_ptr + tb->tc_boundaries[i + 1];
    }
  }
  NOT_REACHED();
}

bool
tb_is_tc_boundary(const void *tcptr)
{
  uint8_t const *tc_ptr;
  size_t i;
  tb_t *tb;

  tc_ptr = tcptr;
  tb = tb_find(tc_ptr);
  ASSERT(tb);
  ASSERT(   tc_ptr >= tb->tc_ptr
         && tc_ptr < tb->tc_ptr + tb->tc_boundaries[tb->num_insns]);

  if (tc_ptr == tb->tc_ptr) {
    return true;
  }
  for (i = 0; i < tb->num_insns; i++) {
    if (tc_ptr == tb->tc_ptr + tb->tc_boundaries[i]) {
      return true;
    }
  }
  return false;
}

void
tb_flush(void)
{
	while (nb_tbs) {
		bool freed;
		freed = free_a_tb();
		ASSERT(freed);
	}
}

void
tb_print_stats(void)
{
	tb_translation_cache_size_min = min(tb_translation_cache_size_min, nb_tbs);
	tb_translation_cache_size_max = max(tb_translation_cache_size_max, nb_tbs);
	tb_translation_cache_size_sum += nb_tbs;
	tb_num_replacements++;

	printf("MON-STATS: %lld translation block replacements.\n",
			tb_num_replacements - 1);
	printf("MON-STATS: Size of translation cache: [%d avg, %d min, %d max] "
			"(%d/%d pages)\n",
			(int)(tb_translation_cache_size_sum/tb_num_replacements),
			tb_translation_cache_size_min, tb_translation_cache_size_max,
			tc_page_count,
			min(free_pages - kernel_page_count - swap_page_count, tc_page_limit));
}
