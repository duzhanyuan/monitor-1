#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

/* How to allocate pages. */
enum palloc_flags
  {
    PAL_ASSERT = 0x1,           /* Panic on failure. */
    PAL_ZERO = 0x2,             /* Zero page contents. */
    PAL_TC = 0x4,               /* Translation cache page. */
    PAL_SWAP = 0x8,             /* Swap page. */
    PAL_NOCACHE = 0x10,         /* Disable memory caching for page. */
  };

/* Maximum number of pages to put in tc pool. */
extern size_t tc_page_limit;
/* Maximum number of pages to put in swap pool. */
extern size_t swap_page_limit;

void palloc_init (void *end);
void *palloc_get_page (enum palloc_flags);
void *palloc_get_page_with_replacement (enum palloc_flags);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);
bool is_heap_addr(const void *addr);

extern size_t free_pages, tc_page_limit, swap_page_limit;
extern int swap_page_count, tc_page_count;
extern size_t kernel_page_count;
#endif /* threads/palloc.h */
