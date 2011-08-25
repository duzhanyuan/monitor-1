#include "mem/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sys/init.h"
#include "sys/loader.h"
#include "threads/synch.h"
#include "mem/vaddr.h"

#define PALLOC 2

#ifndef MAX_NUM_TC_PAGES
#define MAX_NUM_TC_PAGES     10000
#endif
#ifndef MAX_NUM_SWAP_PAGES
#define MAX_NUM_SWAP_PAGES   1000
#endif

/* Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the monitor
   and TC (translation-cache) pools.  The TC pool is for the translation
   cache pages, the kernel pool for everything else.  The idea here is
   that the kernel needs to have memory for its own operations
   even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and
   half to the user pool.  That should be huge overkill for the
   kernel pool, but that's just fine for demonstration purposes. */

/* A memory pool. */
struct pool
  {
    struct lock lock;                   /* Mutual exclusion. */

#define NUM_BITMAPS 3
    struct bitmap *used_map;            /* Bitmap of free pages. */
    struct bitmap *tc_map;              /* Bitmap indicating if page in TC. */
    struct bitmap *swap_map;            /* Bitmap indicating if page in Swap. */

    uint8_t *base;                      /* Base of pool. */
  };

/* Three pools: one for kernel data, one for user pages, one for
 * target OS's pages that need to be shadowed (either because
 * they are on swap disk, or because their contents had to be
 * modified). */
//struct pool kernel_pool, tc_pool, swap_pool;
struct pool mem_pool;

/* Maximum number of pages to put in tc pool. */
size_t tc_page_limit = MAX_NUM_TC_PAGES;
int tc_page_count = 0;
/* Maximum number of pages to put in swap pool. */
size_t swap_page_limit = MAX_NUM_SWAP_PAGES;
int swap_page_count = 0;
size_t free_pages;

size_t kernel_page_count = 0;

static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (enum palloc_flags pool_id, void *page);

bool
is_heap_addr(const void *addr)
{
  /* End of the kernel as recorded by the linker.
     See kernel.lds.S. */
  extern char _end;

  /* Free memory. */
  uint8_t const *free_start = pg_round_up (&_end);
  uint8_t const *free_end = (void *)0xfffff000;  /* Last but one page. The
                                                    last page is reserved for
                                                    the stack. */
  uint8_t const *iaddr = addr;
  if (iaddr >= free_start && iaddr < free_end) {
    return true;
  }
  return false;
}

/* Initializes the page allocator. */
void
palloc_init (void *end) 
{
  /* End of the kernel as recorded by the linker.
     See kernel.lds.S. */
  extern char _end;

  /* Free memory. */
  uint8_t *free_start = pg_round_up (&_end);
  uint8_t *free_end = end;

  free_pages = (free_end - free_start) / PGSIZE;
  //size_t swap_pages = swap_page_limit;
  //size_t tc_pages = tc_page_limit;
  //size_t monitor_pages = max(free_pages - tc_pages - swap_pages, 0);
  uint8_t *cur;

  cur = free_start;
  init_pool(&mem_pool, cur, free_pages, "mem pool");
  /*
  init_pool (&kernel_pool, cur, kernel_pages, "monitor pool");
  cur += kernel_pages * PGSIZE;
  init_pool (&tc_pool, cur, tc_pages, "translation cache pool");
  cur += tc_pages * PGSIZE;
  init_pool (&swap_pool, cur, swap_pages, "swap pool");
  */
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  struct pool *pool;
  void *pages;
  size_t page_idx;

  if (flags & PAL_TC) {
    if (tc_page_count + page_cnt > tc_page_limit) {
      DBGn(PALLOC, "%s() %d: returning NULL\n", __func__, __LINE__);
      return NULL;
    }
  } else if (flags & PAL_SWAP) {
    if (swap_page_count + page_cnt > swap_page_limit) {
      DBGn(PALLOC, "%s() %d: returning NULL\n", __func__, __LINE__);
      return NULL;
    }
  }
  pool = &mem_pool;
  if (page_cnt == 0) {
    return NULL;
  }

  lock_acquire (&pool->lock);
  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
  lock_release (&pool->lock);

  if (page_idx != BITMAP_ERROR) {
    pages = pool->base + PGSIZE * page_idx;
  } else {
    DBGn(PALLOC, "%s() %d: returning NULL\n", __func__, __LINE__);
    pages = NULL;
  }
  /*
  printf("pages = %p. page_cnt=%d. bitmap_count=%d\n", pages, page_cnt,
      bitmap_count(pool->used_map, 0, bitmap_size(pool->used_map), false));
      */

  if (pages != NULL) {
    if (flags & PAL_ZERO) {
      memset (pages, 0, PGSIZE * page_cnt);
    }
    if (flags & PAL_TC) {
      ASSERT(bitmap_none(pool->tc_map, page_idx, page_cnt));
      bitmap_set_multiple(pool->tc_map, page_idx, page_cnt, true);
      tc_page_count += page_cnt;
    } else if (flags & PAL_SWAP) {
      ASSERT(bitmap_none(pool->swap_map, page_idx, page_cnt));
      bitmap_set_multiple(pool->swap_map, page_idx, page_cnt, true);
      swap_page_count += page_cnt;
    } else {
      kernel_page_count += page_cnt;
    }
  } else if (flags & PAL_ASSERT) {
    PANIC ("palloc_get: out of pages. request=%zu pages. "
        "tc_page_count=%zu, swap_page_count=%zu, kernel_page_count=%zu\n",
        page_cnt, tc_page_count, swap_page_count, kernel_page_count);
  }

  return pages;
}

/* Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros.  If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_page (enum palloc_flags flags) 
{
  return palloc_get_multiple (flags, 1);
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt) 
{
  struct pool *pool = &mem_pool;
  size_t page_idx;

  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  page_idx = pg_no (pages) - pg_no (pool->base);

  if (page_from_pool (PAL_TC, pages)) {
    ASSERT (bitmap_all (pool->tc_map, page_idx, page_cnt));
    ASSERT(bitmap_none(pool->swap_map, page_idx, page_cnt));
    tc_page_count -= page_cnt;
    ASSERT(tc_page_count >= 0);
  } else if (page_from_pool (PAL_SWAP, pages)) {
    ASSERT (bitmap_all (pool->swap_map, page_idx, page_cnt));
    ASSERT(bitmap_none(pool->tc_map, page_idx, page_cnt));
    swap_page_count -= page_cnt;
    ASSERT(swap_page_count >= 0);
  } else {
    kernel_page_count -= page_cnt;
  }

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
  bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
  bitmap_set_multiple (pool->tc_map, page_idx, page_cnt, false);
  bitmap_set_multiple (pool->swap_map, page_idx, page_cnt, false);
}

/* Frees the page at PAGE. */
void
palloc_free_page (void *page) 
{
  palloc_free_multiple (page, 1);
}

/* Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name) 
{
  /* We'll put the pool's used_map at its base.
     Calculate the space needed for the bitmap
     and subtract it from the pool's size. */
  size_t bm_size = bitmap_buf_size (page_cnt);
  size_t bm_pages = DIV_ROUND_UP(NUM_BITMAPS * bm_size, PGSIZE);
  uint8_t *ptr = base;
  int i;
  if (bm_pages > page_cnt) {
    PANIC ("Not enough memory in %s for bitmap.", name);
  }
  page_cnt -= bm_pages;

  MSG ("%zu pages available in %s.\n", page_cnt, name);

  /* Initialize the pool. */
  lock_init (&p->lock);
  p->used_map = bitmap_create_in_buf (page_cnt, ptr, bm_size);
  p->tc_map = bitmap_create_in_buf (page_cnt, ptr + bm_size, bm_size);
  p->swap_map = bitmap_create_in_buf (page_cnt, ptr + 2*bm_size, bm_size);
  p->base = (char *)base + bm_pages * PGSIZE;
}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool (enum palloc_flags pool_id, void *page) 
{
  struct pool *pool = &mem_pool;
  size_t page_idx;

  page_idx = pg_no (page) - pg_no (pool->base);

  ASSERT(bitmap_test(pool->used_map, page_idx));
  if (pool_id == PAL_TC) {
    if (bitmap_test(pool->tc_map, page_idx)) {
      ASSERT(!bitmap_test(pool->swap_map, page_idx));
      return true;
    }
  } else if (pool_id == PAL_SWAP) {
    if (bitmap_test(pool->swap_map, page_idx)) {
      ASSERT(!bitmap_test(pool->tc_map, page_idx));
      return true;
    }
  } else {
    NOT_REACHED();
  }
  return false;
}
