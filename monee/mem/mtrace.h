#ifndef MEM_MTRACE_H
#define MEM_MTRACE_H
#include <stdlib.h>
#include <types.h>
#include <stdbool.h>

struct intr_frame;
struct malloc_cb;

struct mtrace;
void mtrace_init(void);

typedef void (mtrace_add_remove_fn)(target_phys_addr_t start, size_t len,
		void (*callback)(target_phys_addr_t start, size_t len, void *opaque),
		void *opaque, struct malloc_cb *malloc_cb);
mtrace_add_remove_fn mtrace_add;
mtrace_add_remove_fn mtrace_remove;

void pte_add_mtrace(uint32_t *pte, target_phys_addr_t paddr,
		void *opaque);
void pte_remove_mtrace(uint32_t *pte, target_phys_addr_t paddr,
		void *opaque);

bool mtraces_handle_page_fault(uint32_t *shadow_pte, target_ulong fault_addr,
		target_phys_addr_t paddr, struct intr_frame *f);


#endif /* mem/mtrace.h */
