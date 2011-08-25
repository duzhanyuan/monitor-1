#ifndef MEM_VADDR_H
#define MEM_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "sys/loader.h"

/* Functions and macros for working with virtual addresses.

   See pte.h for functions and macros specifically for x86
   hardware page tables. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* Page offset (bits 0:12). */
#define PGSHIFT 0                          /* Index of first offset bit. */
#define PGBITS  12                         /* Number of offset bits. */
#define PGSIZE  (1 << PGBITS)              /* Bytes in a page. */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /* Page offset bits (0:12). */

/* Large page offset (bits 0:22). */
#define LPGSHIFT 0                            /* Index of first offset bit. */
#define LPGBITS  22                           /* Number of offset bits. */
#define LPGSIZE  (1 << LPGBITS)               /* Bytes in a page. */
#define LPGMASK  BITMASK(LPGSHIFT, LPGBITS)   /* Page offset bits (0:22). */


/* Offset within a page. */
static inline unsigned pg_ofs (const void *va) {
  return (uintptr_t) va & PGMASK;
}

/* Offset within a page. */
static inline unsigned lpg_ofs (const void *va) {
  return (uintptr_t) va & LPGMASK;
}

/* Virtual page number. */
static inline uintptr_t pg_no (const void *va) {
  return (uintptr_t) va >> PGBITS;
}

/* Round up to nearest page boundary. */
static inline void *pg_round_up (const void *va) {
  return (void *) (((uintptr_t) va + PGSIZE - 1) & ~PGMASK);
}

/* Round down to nearest page boundary. */
static inline void *pg_round_down (const void *va) {
  return (void *) ((uintptr_t) va & ~PGMASK);
}

/* MONITOR_BASE marks the end of guest OS's address
 * space.  Up to this point in memory, guest OS's are allowed
 * to map whatever they like.  At this point and above, the
 * virtual address space belongs to the monitor.
 * The monitor is protected from the guest using segmentation.
 * The address space is mapped as follows:
 *
 *   0xffffffff     +------------------------------------+
 *                  |                                    |
 *                  |                                    |
 *                  |                                    |
 *                  |                                    |
 *                  |    Pages allocated dynamically     |
 *                  |       using   palloc()             |
 *                  |                                    |
 *                  |                                    |
 *                  |                                    |
 *                  |                                    |
 *                  |------------------------------------|
 *                  |          Monitor's code            |
 *   0xffc00000     +------------------------------------+ <-- truncated
 *                  |                                    |     segments'
 *                  |        Guest's address space       |     limit
 *                  |                                    |
 *                  |                                    |
 *                  ...                                ...
 *                  |                                    |
 *                  |                                    |
 *   0x00000000     +------------------------------------+
 */
#define MONITOR_BASE ((void *)LOADER_MONITOR_VIRT_BASE)


/* Returns true if VADDR is a kernel virtual address. */
static inline bool
is_monitor_vaddr (const void *vaddr) 
{
  return vaddr >= (void *)MONITOR_BASE;
}

#define ptov_phy(x) ({                                                        \
    /* ASSERT(((uintptr_t)(x)) < (uintptr_t)LOADER_MONITOR_BASE               \
      || (uintptr_t)(x) > (uintptr_t)LOADER_MONITOR_BASE + (MONITOR_SIZE-1)); \
    ASSERT(((uintptr_t)(x)) < (uintptr_t)MONITOR_BASE                         \
      || (uintptr_t)(x) > (uintptr_t)MONITOR_BASE + (MONITOR_SIZE-1));    */  \
    ((void *)(x)); })

#define vtop_phy(x) ({                                                        \
     /* ASSERT((uintptr_t)(x) < (uintptr_t)LOADER_MONITOR_BASE                \
      || (uintptr_t)(x) >= (uintptr_t)LOADER_MONITOR_END); */                 \
    (uintptr_t)(x); })

#define ptov_mon(x) ({                                                        \
    ASSERT(((uintptr_t)(x)) >= (uintptr_t)LOADER_MONITOR_BASE                 \
      && (uintptr_t)(x) < (uintptr_t)LOADER_MONITOR_BASE + (MONITOR_SIZE-1)); \
    ((void *)((uintptr_t)(x)+ ((uintptr_t)MONITOR_BASE-LOADER_MONITOR_BASE)));\
    })

#define vtop_mon(x) ({                                                        \
    ASSERT((uintptr_t)(x) >= (uintptr_t)MONITOR_BASE                          \
      &&(uintptr_t)(x)<=(uintptr_t)MONITOR_BASE+(MONITOR_SIZE-1));            \
    (uintptr_t)((uintptr_t)(x) -                                              \
      (uintptr_t)((uintptr_t)MONITOR_BASE - LOADER_MONITOR_BASE)); })

#ifdef __MONITOR__
#define ptov(x) ((((uint32_t)(x))&0x1)?({ASSERT((uint32_t)(x)==0x1); (void *)(x);}):((x)>=LOADER_MONITOR_BASE && (x)<LOADER_MONITOR_END)?ptov_mon(x):(void *)(x))
#define vtop(x) (((uint32_t)(x) >= LOADER_MONITOR_VIRT_BASE)?vtop_mon(x):(uint32_t)(x))
#else
#define ptov(x) ((void *)(x))
#define vtop(x) ((uint32_t)(x))
#endif

#endif /* mem/vaddr.h */
