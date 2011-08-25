#include "mem/swap.h"
#include <debug.h>
#include <list.h>
#include <hash.h>
#include <string.h>
#include <stdio.h>
#include "devices/disk.h"
#include "mem/malloc.h"
#include "mem/malloc_cb.h"
#include "mem/mtrace.h"
#include "mem/pte.h"
#include "mem/palloc.h"
#include "sys/init.h"
#include "sys/vcpu.h"

#define SWAP 2

#define is_swap_pt(paddr) (((paddr) & 0x7) == SWAP_PT_USER 	\
		|| ((paddr) & 0x7) == SWAP_PT_SUPERVISOR)
#define is_swap_pd(paddr) (((paddr) & 0x7) == SWAP_PD_USER 	\
		|| ((paddr) & 0x7) == SWAP_PD_SUPERVISOR)


typedef struct reference_t {
  uint32_t const *pte;
	struct swap_page_t *spage;
  struct list_elem l_elem;
	struct hash_elem rh_elem;
} reference_t;

typedef struct swap_page_t {
  target_phys_addr_t paddr;
  void *page;
  bool on_disk, dirty;
  struct list references;
	long long mtraces_version;

  /* hash element. */
  struct hash_elem h_elem;
} swap_page_t;

static void *swap_pool_malloc(size_t size);
static void swap_pool_lock(void *opaque);
static void swap_pool_unlock(void *opaque);

struct malloc_cb swap_malloc_cb = {
	&swap_pool_malloc,
	&swap_pool_lock,
	&swap_pool_unlock
};

#if 0
typedef struct phys_write_trace_t {
  target_phys_addr_t page;
  /* hash element. */
  struct hash_elem h_elem;
} phys_write_trace_t;
#endif

/* Statistics. */
static long long swap_pt_user_num_replacements = 0;
static long long swap_pt_supervisor_num_replacements = 0;
static long long swap_pd_user_num_replacements = 0;
static long long swap_pd_supervisor_num_replacements = 0;
static long long swap_pg_num_replacements = 0;
static long long swap_pg_cache_size_sum = 0;
static int swap_pg_cache_size_min = INT32_MAX;
static int swap_pg_cache_size_max = 0;
static long long swap_pt_user_cache_size_sum = 0;
static int swap_pt_user_cache_size_min = INT32_MAX;
static int swap_pt_user_cache_size_max = 0;
static long long swap_pt_supervisor_cache_size_sum = 0;
static int swap_pt_supervisor_cache_size_min = INT32_MAX;
static int swap_pt_supervisor_cache_size_max = 0;
static long long swap_pd_user_cache_size_sum = 0;
static int swap_pd_user_cache_size_min = INT32_MAX;
static int swap_pd_user_cache_size_max = 0;
static long long swap_pd_supervisor_cache_size_sum = 0;
static int swap_pd_supervisor_cache_size_min = INT32_MAX;
static int swap_pd_supervisor_cache_size_max = 0;
static ssize_t swap_pt_user_count = 0, swap_pt_supervisor_count = 0;
static ssize_t swap_pd_user_count = 0, swap_pd_supervisor_count = 0;
static ssize_t swap_pg_count = 0;

static struct hash swap_pages;
static struct hash swap_ptes;
static ssize_t num_swap_pages = 0;
static const void *locked_pt;
static struct swap_page_t *locked_pd[2];
static int num_locked_pds = 0;

static struct hash phys_write_traces;

/* Helper functions. */
static void swap_disk_write(struct swap_page_t *spage,target_phys_addr_t paddr);
static void swap_mtrace(target_phys_addr_t start, size_t len, void *opaque);
static void swap_page_remove_in_reference(swap_page_t *spage, uint32_t *pte);
static bool swap_page_is_locked(swap_page_t const *spage);

#define SWAP_ASSERT(...)
//#define SWAP_ASSERT ASSERT

#define count_swap_pages() ({                                                 \
  struct hash_iterator iter;                                                  \
  int n = 0;                                                                  \
  hash_first(&iter, &swap_pages);                                             \
  while (hash_next(&iter)) {                                                  \
    struct swap_page_t *spage;                                                \
    spage = hash_entry(hash_cur(&iter), struct swap_page_t, h_elem);          \
    n++;                                                                      \
  }                                                                           \
  DBGn(SWAP, "%s() %d: sanity_check(): n=%d, num_swap_pages=%d\n",            \
    __func__, __LINE__, n, num_swap_pages);                                   \
  ASSERT(n <= swap_page_limit);                                               \
  n;                                                                          \
})

#define num_locked() (num_locked_pds + 																				\
		(locked_pt && (locked_pd[0] && locked_pt != locked_pd[0]->page)						\
		 && (locked_pd[1] && locked_pt != locked_pd[1]->page)?1:0))

static void *swap_phys_pt = NULL;
static disk_sector_t swap_ofs = 0;

void *
swap_get_phys_pt(void)
{
  swap_phys_pt = palloc_get_page(PAL_ASSERT | PAL_ZERO);
  return swap_phys_pt;
}

/*
static enum swap_pagetype
get_child_ptype(enum swap_pagetype ptype)
{
	switch (ptype) {
		case SWAP_PD_SUPERVISOR: 
			return SWAP_PT_SUPERVISOR;
		case SWAP_PD_USER: 
			return SWAP_PT_USER;
		case SWAP_PT_SUPERVISOR: 
			return SWAP_PAGE;
		case SWAP_PT_USER: 
			return SWAP_PAGE;
		case SWAP_PAGE:
		default:
			NOT_REACHED();
	}
}
*/

static inline bool
swap_page_exists(const void *page)
{
  struct hash_iterator iter;
  //printf("%s(): searching for %p\n", __func__, page);
  //printf("  swap_phys_pt=%p\n", swap_phys_pt);
  if (   page >= (void *)swap_phys_pt
      && (uint8_t *)page < (uint8_t *)swap_phys_pt + PGSIZE) {
    return true;
  }
  hash_first(&iter, &swap_pages);
  while (hash_next(&iter)) {
    struct swap_page_t *spage;
    spage = hash_entry(hash_cur(&iter), struct swap_page_t, h_elem);
    //printf("  %p->%p\n", spage->page, (uint8_t *)spage->page + PGSIZE);
    if (   page >= spage->page
        && (uint8_t *)page < ((uint8_t *)spage->page + PGSIZE)) {
      return true;
    }
  }
  return false;
}

static inline void
invalidate_pte_entry (uint32_t *pte)
{
	if (*pte & PTE_P) {
		pte_remove_mtrace(pte, 0, NULL);
		*pte &= ~PTE_P;
	}
}

static void
swap_page_remove_out_references(swap_page_t const *spage)
{
	bool spage_is_pt, spage_is_pd;
  uint32_t *pt;
  int i;
  ASSERT(spage->page);
  pt = spage->page;

	spage_is_pt = is_swap_pt(spage->paddr);
	spage_is_pd = is_swap_pd(spage->paddr);

  if (spage_is_pt || spage_is_pd) {
		int n = spage_is_pt?1024:(LOADER_MONITOR_VIRT_BASE>>LPGBITS);
    for (i = 0; i < n; i++) {
      if (pt[i] & PTE_P) {
				struct reference_t needle, *found;
				struct hash_elem *e;
				needle.pte = &pt[i];
				e = hash_delete(&swap_ptes, &needle.rh_elem);
				if (e) {
					found = hash_entry(e, struct reference_t, rh_elem);
					ASSERT(found->pte == &pt[i]);
					list_remove(&found->l_elem);
					free(found);
				}
				invalidate_pte_entry(&pt[i]);
      }
    }
  }
}

static unsigned
swap_page_hash(struct hash_elem const *e, void *aux)
{
  struct swap_page_t *spage;
  spage = hash_entry(e, struct swap_page_t, h_elem);
  return (unsigned)spage->paddr;
}

static bool
swap_page_equal(struct hash_elem const *a, struct hash_elem const *b, void *aux)
{
  struct swap_page_t *apage, *bpage;
  apage = hash_entry(a, struct swap_page_t, h_elem);
  bpage = hash_entry(b, struct swap_page_t, h_elem);
  return apage->paddr == bpage->paddr;
}

static unsigned
swap_pte_hash(struct hash_elem const *e, void *aux)
{
  struct reference_t *ref;
  ref = hash_entry(e, struct reference_t, rh_elem);
  return (unsigned)ref->pte;
}

static bool
swap_pte_equal(struct hash_elem const *a, struct hash_elem const *b, void *aux)
{
  struct reference_t *aref, *bref;
  aref = hash_entry(a, struct reference_t, rh_elem);
  bref = hash_entry(b, struct reference_t, rh_elem);
  return aref->pte == bref->pte;
}



#if 0
static unsigned
write_trace_hash(struct hash_elem const *e, void *aux)
{
  struct phys_write_trace_t *wtrace;
  wtrace = hash_entry(e, struct phys_write_trace_t, h_elem);
  return (unsigned)wtrace->page;
}

static bool
write_trace_equal(struct hash_elem const *a, struct hash_elem const *b,
    void *aux)
{
  struct phys_write_trace_t *atrace, *btrace;
  atrace = hash_entry(a, struct phys_write_trace_t, h_elem);
  btrace = hash_entry(b, struct phys_write_trace_t, h_elem);
  return atrace->page == btrace->page;
}
#endif

static void
free_swap_page_in_references(struct swap_page_t *spage)
{
  struct list_elem *e;
  struct list *ls;
  ls = &spage->references;
  for (e = list_begin(ls); e != list_end(ls); ) {
    struct reference_t *ref;
		struct hash_elem *re;
    ref = list_entry(e, struct reference_t, l_elem);
    e = list_next(e);
    if (ref->pte) {
      ASSERT(ref->pte >= (uint32_t const *)LOADER_MONITOR_VIRT_BASE);
      ASSERT(*ref->pte & PTE_P);
      ASSERT((*ref->pte & PTE_ADDR) == vtop_mon(spage->page));
      SWAP_ASSERT(swap_page_exists(ref->pte));
      DBGn(SWAP, "  removing reference at %p\n", ref->pte);
			invalidate_pte_entry ((uint32_t *)ref->pte);
      //*((uint32_t *)ref->pte) &= ~PTE_P;
      if ((*((uint32_t *)ref->pte)) & PTE_D) {
        spage->dirty = true;
      }
    }
		re = hash_delete(&swap_ptes, &ref->rh_elem);
		ASSERT(re);
    free(ref);
  }
  spage->dirty = true;//XXX: remove this line.
  list_init(&spage->references);
}



static bool
free_a_swap_ref(void)
{
  /* To implement this function, we need the ability to lock a reference.
   * Since this is unlikely to happen, we leave it as not-implemented. */
  NOT_IMPLEMENTED();
  return false;
}

static void
update_stats(target_phys_addr_t paddr, bool remove)
{
	if ((paddr & 0x7) == SWAP_PD_SUPERVISOR) {
		if (remove) {
			swap_pd_supervisor_cache_size_min = min(swap_pd_supervisor_count,
					swap_pd_supervisor_cache_size_min);
			swap_pd_supervisor_cache_size_max = max(swap_pd_supervisor_count,
					swap_pd_supervisor_cache_size_max);
			swap_pd_supervisor_cache_size_sum +=    swap_pd_supervisor_count;
			swap_pd_supervisor_num_replacements++;
			swap_pd_supervisor_count--;
		} else {
			swap_pd_supervisor_count++;
		}
	} else if ((paddr & 0x7) == SWAP_PD_USER) {
		if (remove) {
			swap_pd_user_cache_size_min = min(swap_pd_user_count,
					swap_pd_user_cache_size_min);
			swap_pd_user_cache_size_max = max(swap_pd_user_count,
					swap_pd_user_cache_size_max);
			swap_pd_user_cache_size_sum +=    swap_pd_user_count;
			swap_pd_user_num_replacements++;
			swap_pd_user_count--;
		} else {
			swap_pd_user_count++;
		}
	} else if ((paddr & 0x7) == SWAP_PT_SUPERVISOR) {
		if (remove) {
			swap_pt_supervisor_cache_size_min = min(swap_pt_supervisor_count,
					swap_pt_supervisor_cache_size_min);
			swap_pt_supervisor_cache_size_max = max(swap_pt_supervisor_count,
					swap_pt_supervisor_cache_size_max);
			swap_pt_supervisor_cache_size_sum +=    swap_pt_supervisor_count;
			swap_pt_supervisor_num_replacements++;
			swap_pt_supervisor_count--;
		} else {
			swap_pt_supervisor_count++;
		}
	} else if ((paddr & 0x7) == SWAP_PT_USER) {
		if (remove) {
			swap_pt_user_cache_size_min = min(swap_pt_user_count,
					swap_pt_user_cache_size_min);
			swap_pt_user_cache_size_max = max(swap_pt_user_count,
					swap_pt_user_cache_size_max);
			swap_pt_user_cache_size_sum +=    swap_pt_user_count;
			swap_pt_user_num_replacements++;
			swap_pt_user_count--;
		} else {
			swap_pt_user_count++;
		}
	} else {
		ASSERT((paddr & 0x7) == SWAP_PAGE);
		if (remove) {
			swap_pg_cache_size_min = min(swap_pg_count,
					swap_pg_cache_size_min);
			swap_pg_cache_size_max = max(swap_pg_count,
					swap_pg_cache_size_max);
			swap_pg_cache_size_sum +=    swap_pg_count;
			swap_pg_num_replacements++;
			swap_pg_count--;
		} else {
			swap_pg_count++;
		}
	}
}

static void
swap_page_free(struct swap_page_t *spage)
{
  struct hash_elem *e;
	ASSERT(!swap_page_is_locked(spage));
	swap_page_remove_out_references(spage);
	e = hash_delete(&swap_pages, &spage->h_elem);
	ASSERT(e);
	//printf("freeing %p\n", spage->page);
	free_swap_page_in_references(spage);
	if (is_swap_pd(spage->paddr) || is_swap_pt(spage->paddr)) {
		target_phys_addr_t paddr;
		paddr = spage->paddr & ~0x7;
		mtrace_remove(paddr, PGSIZE, swap_mtrace, spage, NULL);
	}
	if (spage->on_disk && spage->dirty) {
		ASSERT((spage->paddr & PGMASK) == 0);
		swap_disk_write(spage, spage->paddr);
	}
	ASSERT(spage->page);
	palloc_free_page(spage->page);

	/* Update stats. */
	update_stats(spage->paddr, true);

	free(spage);
	num_swap_pages--;
	SWAP_ASSERT(count_swap_pages() == num_swap_pages);
}

static bool
free_a_swap_page(void)
{
  struct swap_page_t *replacement = NULL;
  struct hash_iterator iter;
  unsigned r;
  ASSERT(num_swap_pages > num_locked());
  r = random_ulong() % (unsigned)(num_swap_pages - num_locked());
	r++;
  /* Loop around swap_pages and find the r'th candidate page. If
   * r is greater than number of candidate pages, loop around. */
  do {
    hash_first(&iter, &swap_pages);
    while (hash_next(&iter)) {
      struct swap_page_t *spage;
      spage = hash_entry(hash_cur(&iter), struct swap_page_t, h_elem);
      if (!swap_page_is_locked(spage)) {
        r--;
      }
      if (r == 0) {
        replacement = spage;
        break;
      }
    }
		ASSERT(r || replacement);
  } while (!replacement);
  ASSERT(replacement);
	swap_page_free(replacement);
  return true;
}

static bool
free_swap_space(void)
{
  if (num_swap_pages - num_locked() == 0) {
    return free_a_swap_ref();
  } else {
    return free_a_swap_page();
  }
}

void
swap_init(void)
{
  char drive_str[128];
  int pg_no, ret;
  void *zero;

  snprintf(drive_str, sizeof drive_str, "bootdisk:0x0:0x0");
  swap_ofs = (loader_pages + monitor_pages) * 8 + monitor_ofs;
  DBGn (SWAP, "%s(): loader_pages=%d, monitor_pages=%d, monitor_ofs=%d, "
      "overall=%d\n", __func__, loader_pages, monitor_pages, monitor_ofs,
      (loader_pages + monitor_pages) * 8 + monitor_ofs);
  ret = bdrv_open(&swap_bdrv, drive_str, "rw");
  ASSERT(ret >= 0);

  hash_init(&swap_pages, swap_page_hash, swap_page_equal, NULL);
  hash_init(&swap_ptes, swap_pte_hash, swap_pte_equal, NULL);
	locked_pd[0] = locked_pd[1] = NULL;
	locked_pt = NULL;

	/*
  // Zero all on-disk swap pages.
  zero = palloc_get_page(PAL_ASSERT | PAL_ZERO | PAL_SWAP);
  for (pg_no = 0; pg_no < swap_disk_pages; pg_no++) {
    printf("%s() %d:\n", __func__, __LINE__);
    ret = bdrv_write(&swap_bdrv, pg_no*(PGSIZE/DISK_SECTOR_SIZE), zero,
        PGSIZE/DISK_SECTOR_SIZE);
    printf("%s() %d:\n", __func__, __LINE__);
    ASSERT(ret >= 0);
  }
  palloc_free_page(zero);
	*/
}

void
swap_unlock_pds(void)
{
	int i;
  ASSERT(locked_pt == NULL);
	for (i = 0; i < num_locked_pds; i++) {
		locked_pd[i] = NULL;
	}
	num_locked_pds = 0;
}

static void
swap_lock_pt(const void *vaddr)
{
  struct swap_page_t needle, *found;
  struct hash_elem *e;

	ASSERT(locked_pt == NULL);
  DBGn(SWAP, "%s(%p) called.\n", __func__, vaddr);
  if (vaddr == NULL) {
    return;
  }
  locked_pt = vaddr;
}

static void
swap_unlock_pt(const void *vaddr)
{
  DBGn(SWAP, "%s(%p) called.\n", __func__, vaddr);
  if (vaddr == NULL) {
    return;
  }
  ASSERT(locked_pt == vaddr);
  locked_pt = NULL;
}

static void *
swap_pool_malloc(size_t size)
{
  void *ret;
  while (!(ret = malloc_from_pool(POOL_SWAP, size))) {
    free_swap_space();
  }
  ASSERT(ret);
  return ret;
}

static struct swap_page_t *pool_locked_spage = NULL;
static int num_pool_locked = 0;
static void
swap_pool_lock(void *opaque)
{
	ASSERT(num_pool_locked == 0);
	ASSERT(pool_locked_spage == NULL);
	pool_locked_spage = opaque;
	num_pool_locked = 1;
}

static void
swap_pool_unlock(void *opaque)
{
	ASSERT(num_pool_locked == 1);
	ASSERT(pool_locked_spage == opaque);
	pool_locked_spage = NULL;
	num_pool_locked = 0;
}

static bool
swap_page_is_locked(swap_page_t const *spage)
{
  if (   locked_pd[0] == spage || locked_pd[1] == spage
			|| locked_pt == spage->page) {
    return true;
  }
	if (spage == pool_locked_spage) {
		return true;
	}
  return false;
}

static void
swap_pd_init(uint32_t *pd)
{
  size_t pd_num;
  for (pd_num = 0; pd_num < (1 << 10); pd_num++) {
    void *vaddr = (void *)(pd_num * LPGSIZE);

    if (vaddr >= (void *)LOADER_MONITOR_VIRT_BASE) {
      uintptr_t paddr = vtop_mon(vaddr);
      pd[pd_num] = pde_create_large_mon(paddr, true);
    } else {
      pd[pd_num] = 0;
    }
  }
}

static void
swap_page_add_in_reference(swap_page_t *spage, uint32_t *pte,
		uint32_t pte_flags)
{
  struct reference_t *ref;
  struct list_elem *e;
	struct hash_elem *rh;
	uint32_t mtrace_mask;

	ASSERT(pte_flags & PTE_P);
	ASSERT(pte_flags & PTE_U);
	*pte = (vtop_mon(spage->page) & PTE_ADDR) | pte_flags;
	if ((spage->paddr & 0x7) == SWAP_PAGE) {
		pte_add_mtrace(pte, spage->paddr & ~0x7, NULL);
	}
  for (e = list_begin(&spage->references); e != list_end(&spage->references);
      e = list_next(e)) {
    ref = list_entry(e, struct reference_t, l_elem);
    if (ref->pte == pte) {
      /* reference to pte already exists. */
      return;
    }
  }
	swap_pool_lock(spage);
  ref = swap_pool_malloc(sizeof *ref);
	swap_pool_unlock(spage);
  ref->pte = pte;
	ref->spage = spage;
  list_push_back(&spage->references, &ref->l_elem);
  rh = hash_insert(&swap_ptes, &ref->rh_elem);
	ASSERT(!rh);
}

static void
swap_page_remove_in_reference(swap_page_t *spage, uint32_t *pte)
{
  struct reference_t *ref, *found;
	struct hash_elem *re;
  struct list_elem *e;

	found = NULL;
  for (e = list_begin(&spage->references); e != list_end(&spage->references);
      e = list_next(e)) {
    ref = list_entry(e, struct reference_t, l_elem);
    if (ref->pte == pte) {
			ASSERT(!found);
      found = ref;
    }
  }
	ASSERT(found);
	ASSERT(found->pte == pte);
	list_remove(&found->l_elem);
  re = hash_delete(&swap_ptes, &found->rh_elem);
	ASSERT(re);
	free(found);
	ASSERT(*pte & PTE_P);
	ASSERT((*pte & PTE_ADDR) == vtop_mon(spage->page));
	invalidate_pte_entry (pte);
	//*pte &= ~PTE_P;
}

static bool
swap_page_is_used_in_cur_pagedir(void *page)
{
	size_t pd_no;
	if (vcpu.shadow_page_dir[0] == page) {
		return true;
	}
	if (vcpu.shadow_page_dir[1] == page) {
		return true;
	}
	for (pd_no = 0; pd_no < (LOADER_MONITOR_VIRT_BASE >> LPGBITS); pd_no++) {
		uint32_t *pd0, *pd1;
		pd0 = vcpu.shadow_page_dir[0];
		pd1 = vcpu.shadow_page_dir[1];
		if ((pd0[pd_no] & PTE_P) && ((pd0[pd_no] & PTE_ADDR) == vtop_mon(page))) {
			return true;
		}
		if ((pd1[pd_no] & PTE_P) && ((pd1[pd_no] & PTE_ADDR) == vtop_mon(page))) {
			return true;
		}
	}
	return false;
}

static void
swap_mtrace(target_phys_addr_t start, size_t len, void *opaque)
{
	swap_page_t *spage = (swap_page_t *)opaque;
	target_phys_addr_t paddr = spage->paddr & ~0x7;
	target_phys_addr_t pte;

	LOG(PAGING, "%s(): %x %x (%x)\n", __func__, start, len, spage->paddr);
	ASSERT(is_swap_pd(spage->paddr) || is_swap_pt(spage->paddr));
	ASSERT(start + len > paddr && paddr + PGSIZE > start);

	if (!swap_page_is_used_in_cur_pagedir(spage->page)) {
		ASSERT(!swap_page_is_locked(spage));
		swap_page_free(spage);
		return;
	}
	/* Zero out the corresponding page entry. Remove the reference to it from
	 * it's child's reference list. */
	for (pte = (start & ~0x3); pte < start + len; pte += 4) {
		struct reference_t needle, *found;
		struct hash_elem *e;
		uint32_t *shadow_pte;
		size_t pd_num, n;

		ASSERT((pte & 3) == 0);
		n = is_swap_pd(spage->paddr)?(LOADER_MONITOR_VIRT_BASE>>LPGBITS):1024;
		pd_num = (pte - paddr)/4;
		if (pd_num >= n) {
			break;
		}
		shadow_pte = (void *)((uint8_t *)spage->page + (pte - paddr));
		if (!(*shadow_pte & PTE_P)) {
			continue;
		}
		ASSERT(   (uint8_t *)shadow_pte >= (uint8_t *)spage->page
				   && (uint8_t *)shadow_pte <  (uint8_t *)spage->page + PGSIZE);
		if (!(*shadow_pte & PTE_U)) {
			printf("start=%x, len=%x, shadow_pte=%p, *shadow_pte=%x, pte=%x, "
					"paddr=%x\n", start, len, shadow_pte, *shadow_pte, pte, paddr);
		}
		ASSERT(*shadow_pte & PTE_U);
		needle.pte = shadow_pte;
		e = hash_find(&swap_ptes, &needle.rh_elem);
		if (e) {
			struct swap_page_t *child_spage;
			found = hash_entry(e, struct reference_t, rh_elem);
			child_spage = found->spage;
			ASSERT(child_spage);
			ASSERT(!is_swap_pd(child_spage->paddr));
			swap_page_remove_in_reference(child_spage, shadow_pte);
		}
		LOG(PAGING, "swap mtrace: Invalidating %s %p [%x] pointing to %s page due "
				"to write at %x-%x.\n", is_swap_pd(spage->paddr)?"pde":"pte",
				shadow_pte, *shadow_pte, e?"shadow":"guest", start, start + len);
		/* Mark it not-present. Will get filled up on next page fault. A possible
		 * optimization is to fill it up right away. */
		invalidate_pte_entry (shadow_pte);
		//*shadow_pte &= ~PTE_P;
	}
}


void
shadow_pt_scan(uint32_t *pd,
		void (*callback)(uint32_t *pte, target_phys_addr_t paddr, void *opaque),
		void *opaque)
{
  size_t pd_num;

	ASSERT(is_monitor_vaddr(pd));
	for (pd_num = 0; pd_num < (LOADER_MONITOR_VIRT_BASE>>LPGBITS); pd_num++) {
		target_ulong pde;
		size_t pt_num;
		uint32_t *pt;

		pde = pd[pd_num];
		if (!(pde & PTE_P)) {
			continue;
		}
		/* This function should only be called on shadow page dirs, so no large
		 * pages. */
		ASSERT(!(pde & PTE_PS));
		pt = ptov_mon(pde & PTE_ADDR);
		for (pt_num = 0; pt_num < (1 << 10); pt_num++) {
			struct reference_t needle;
			target_phys_addr_t paddr;
			struct hash_elem *e;
			target_ulong pte;

			pte = pt[pt_num];
			if (!(pte & PTE_P)) {
				continue;
			}
			paddr = pte & PTE_ADDR;

			needle.pte = &pt[pt_num];
			if (e = hash_find(&swap_ptes, &needle.rh_elem)) {
				struct reference_t *found;
				found = hash_entry(e, struct reference_t, rh_elem);
				ASSERT(pt[pt_num] & PTE_P);
				ASSERT(found->spage);
				paddr = found->spage->paddr & ~0x7;
			}
			(*callback)(&pt[pt_num], paddr, opaque);
		}
	}
}

static void
pd_sync_mtraces(uint32_t *pd, long long pd_mtraces_version)
{
	int i;
	ASSERT(pd_mtraces_version <= vcpu.cur_mtraces_version);
	if (pd_mtraces_version == vcpu.cur_mtraces_version) {
		return;
	}
	ASSERT(pd_mtraces_version < vcpu.cur_mtraces_version);

	shadow_pt_scan(pd, pte_remove_mtrace, NULL);
	shadow_pt_scan(pd, pte_add_mtrace, NULL);
}


static struct swap_page_t *
swap_get_spage(uint32_t *pte, target_phys_addr_t paddr,
    enum swap_pagetype ptype, uint32_t pte_flags)

{
  struct swap_page_t needle, *found;
  struct hash_elem *e;
  struct swap_page_t *spage;
  struct reference_t *ref;
  void *page;

  swap_lock_pt((void *)((target_ulong)pte & ~PGMASK));

  ASSERT((paddr & PGMASK) == 0);
  needle.paddr = paddr | ptype;
  if (e = hash_find(&swap_pages, &needle.h_elem)) {
    found = hash_entry(e, struct swap_page_t, h_elem);
    ASSERT(found->paddr == needle.paddr);
    if (!is_swap_pd(ptype)) {
			if (pte_flags & PTE_P) {
				swap_page_add_in_reference(found, pte, pte_flags);
				SWAP_ASSERT(swap_page_exists(pte));
			}
    } else {
			ASSERT(pte == NULL);
			ASSERT(num_locked_pds < 2);
      ASSERT(locked_pd[num_locked_pds] == NULL);
      locked_pd[num_locked_pds++] = found;
			pd_sync_mtraces(found->page, found->mtraces_version);
		}
    swap_unlock_pt((void *)((target_ulong)pte & ~PGMASK));
    DBGn(SWAP, "%s(%p,0x%x) returning %p from cache.\n", __func__, pte, paddr,
        found->page);
    return found;
  }

  spage = swap_pool_malloc(sizeof *spage);
	swap_pool_lock(spage);
  while (!(page = palloc_get_page (PAL_SWAP | PAL_ZERO))) {
    free_swap_space();
  }
	swap_pool_unlock(spage);

  spage->paddr = paddr | ptype;
  spage->page = page;
	if (is_swap_pd(spage->paddr) || is_swap_pt(spage->paddr)) {
		mtrace_add(paddr, PGSIZE, swap_mtrace, spage, &swap_malloc_cb);
	}
	update_stats(spage->paddr, false);
  num_swap_pages++;
	spage->mtraces_version = -1;
	if (is_swap_pd(ptype)) {
		ASSERT(pte == NULL);
		ASSERT(num_locked_pds < 2);
		ASSERT(locked_pd[num_locked_pds] == NULL);
		locked_pd[num_locked_pds++] = spage;
    swap_pd_init(spage->page);
		spage->mtraces_version = 0;
  }
  spage->on_disk = false;
  spage->dirty = false;
  list_init(&spage->references);
	if (pte_flags & PTE_P) {
		swap_page_add_in_reference(spage, pte, pte_flags);
	}
  DBGn(SWAP, "calling hash_insert(0x%x).\n", spage->paddr);
  hash_insert(&swap_pages, &spage->h_elem);
  swap_unlock_pt((void *)((target_ulong)pte & ~PGMASK));
  SWAP_ASSERT(count_swap_pages() == num_swap_pages);

  DBGn(SWAP, "%s(%p,0x%x) returning %p.\n", __func__, pte, paddr, page);
  return spage;
}

void *
swap_get_page(uint32_t *pte, target_phys_addr_t paddr,
    enum swap_pagetype ptype, uint32_t pte_flags)
{
	return swap_get_spage(pte, paddr, ptype, pte_flags)->page;
}


uint32_t
shadow_pte_flags(uint32_t pte)
{
	ASSERT((pte & PTE_ADDR) == 0);
  if (!(pte & PTE_A)) {
    /* so that it traps. */
    return 0;
  }
  if (pte & PTE_W) {
    if (!(pte & PTE_D)) {
      return (pte & PTE_FLAGS & ~PTE_W) | PTE_U;
    }
  }
  return (pte & PTE_FLAGS) | PTE_U;
}

void
swap_disk_read(void *page, target_phys_addr_t paddr)
{
  struct swap_page_t needle, *spage;
  struct hash_elem *e;

  ASSERT((paddr & PGMASK) == 0);
  ASSERT(paddr >= LOADER_MONITOR_BASE && paddr < LOADER_MONITOR_END);
  needle.paddr = paddr;
  e = hash_find(&swap_pages, &needle.h_elem);
  ASSERT(e);
  spage = hash_entry(e, struct swap_page_t, h_elem);
  if (!spage->on_disk) {
    disk_sector_t pg_no;
    int ret;
    spage->on_disk = true;
    pg_no = (paddr - LOADER_MONITOR_BASE)/PGSIZE;
    ret = bdrv_read(&swap_bdrv, swap_ofs + pg_no*PGSIZE/DISK_SECTOR_SIZE, page,
        PGSIZE/DISK_SECTOR_SIZE);
    ASSERT(ret >= 0);
  }
}


static void
swap_disk_write(struct swap_page_t *spage, target_phys_addr_t paddr)
{
  disk_sector_t pg_no;
  int ret;
  spage->on_disk = false;
  pg_no = (spage->paddr - LOADER_MONITOR_BASE)/PGSIZE;
  ret = bdrv_write(&swap_bdrv, swap_ofs + pg_no*PGSIZE/DISK_SECTOR_SIZE,
      spage->page, PGSIZE/DISK_SECTOR_SIZE);
  ASSERT(ret >= 0);

}

static void
swap_check(void)
{
  SWAP_ASSERT(count_swap_pages() == num_swap_pages);
}

void
swap_flush(void)
{
	while (num_swap_pages - num_locked()) {
		free_swap_space();
	}
}

void
swap_load_shadow_page_dirs(void)
{
	swap_page_t *shadow[2];
	struct swap_page_t needle, *found;
	struct hash_elem *e;


	if (locked_pd[0]) {
		locked_pd[0]->mtraces_version = vcpu.cur_mtraces_version;
	}
	if (locked_pd[1]) {
		locked_pd[1]->mtraces_version = vcpu.cur_mtraces_version;
	}

  swap_unlock_pds();

	shadow[0] = swap_get_spage(NULL, vcpu.cr[3], SWAP_PD_SUPERVISOR, 0);
	shadow[1] = swap_get_spage(NULL, vcpu.cr[3], SWAP_PD_USER, 0);

	shadow[0]->mtraces_version = vcpu.cur_mtraces_version;
	shadow[1]->mtraces_version = vcpu.cur_mtraces_version;

  vcpu.shadow_page_dir[0] = shadow[0]->page;
  vcpu.shadow_page_dir[1] = shadow[1]->page;
}

void
swap_print_stats(void)
{
	long long swap_num_replacements;
	swap_num_replacements = swap_pd_supervisor_num_replacements
		+ swap_pd_user_num_replacements + swap_pt_supervisor_num_replacements
		+ swap_pt_user_num_replacements + swap_pg_num_replacements;
	printf("MON-STATS: swap: %lld replacements (%lld,%lld,%lld,%lld,%lld).\n",
			swap_num_replacements, swap_pd_supervisor_num_replacements,
			swap_pd_user_num_replacements, swap_pt_supervisor_num_replacements,
			swap_pt_user_num_replacements, swap_pg_num_replacements);

	update_stats(SWAP_PD_SUPERVISOR, true);
	swap_pd_supervisor_count++;
	update_stats(SWAP_PD_USER, true);
	swap_pd_user_count++;
	update_stats(SWAP_PT_SUPERVISOR, true);
	swap_pt_supervisor_count++;
	update_stats(SWAP_PT_USER, true);
	swap_pt_user_count++;
	update_stats(SWAP_PAGE, true);
	swap_pg_count++;

	printf("MON-STATS: swap pd-supervisor: [%d avg, %d min, %d max] pages\n",
			(int)(swap_pd_supervisor_cache_size_sum/swap_pd_supervisor_num_replacements),
			swap_pd_supervisor_cache_size_min, swap_pd_supervisor_cache_size_max);
	printf("MON-STATS: swap pd-user: [%d avg, %d min, %d max] pages\n",
			(int)(swap_pd_user_cache_size_sum/swap_pd_user_num_replacements),
			swap_pd_user_cache_size_min, swap_pd_user_cache_size_max);
	printf("MON-STATS: swap pt-supervisor: [%d avg, %d min, %d max] pages\n",
			(int)(swap_pt_supervisor_cache_size_sum/swap_pt_supervisor_num_replacements),
			swap_pt_supervisor_cache_size_min, swap_pt_supervisor_cache_size_max);
	printf("MON-STATS: swap pt-user: [%d avg, %d min, %d max] pages\n",
			(int)(swap_pt_user_cache_size_sum/swap_pt_user_num_replacements),
			swap_pt_user_cache_size_min, swap_pt_user_cache_size_max);
	printf("MON-STATS: swap pg: [%d avg, %d min, %d max] pages\n",
			(int)(swap_pg_cache_size_sum/swap_pg_num_replacements),
			swap_pg_cache_size_min, swap_pg_cache_size_max);
}
