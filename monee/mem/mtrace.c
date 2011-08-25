#include "mem/mtrace.h"
#include <stdlib.h>
#include <stdio.h>
#include <hash.h>
#include <rbtree.h>
#include <string.h>
#include "mem/malloc.h"
#include "mem/malloc_cb.h"
#include "mem/paging.h"
#include "mem/palloc.h"
#include "mem/pte.h"
#include "mem/simulate_insn.h"
#include "mem/swap.h"
#include "peep/i386-dis.h"
#include "peep/insn.h"
#include "peep/insntypes.h"
#include "peep/callouts.h"
#include "peep/peep.h"
#include "sys/exception.h"
#include "sys/interrupt.h"
#include "sys/rr_log.h"

#define MTRACE  2
#define MTRACE2 2

#define PTE_MASK (PTE_W | PTE_A | PTE_D)
static struct hash mtraces_begin_pages, mtraces_end_pages;

struct mtrace
{
	target_phys_addr_t start;
	size_t len;
	void (*callback)(target_phys_addr_t start, size_t len, void *opaque);
	void *opaque;
	struct malloc_cb *malloc_cb;

	struct hash_elem h_elem_begin;
	struct hash_elem h_elem_end;
};

struct pte_entry
{
	uint32_t *pte;
	uint32_t pte_val;
	target_phys_addr_t paddr;

	struct hash_elem h_elem;
};

/* A set of ptes that have been modified due to this mtrace. */
struct hash pte_hash;

static unsigned mtrace_begin_hash_func(struct hash_elem const *e, void *aux);
static bool mtrace_begin_equal_func(struct hash_elem const *a,
		struct hash_elem const *b, void *aux);
static unsigned mtrace_end_hash_func(struct hash_elem const *e, void *aux);
static bool mtrace_end_equal_func(struct hash_elem const *a,
		struct hash_elem const *b, void *aux);
static void pte_add_all_mtraces(uint32_t *pte, target_phys_addr_t paddr);
static void pte_remove_all_mtraces(uint32_t *pte);
static unsigned pte_hash_func (struct hash_elem const *e, void *aux);
static bool pte_equal (struct hash_elem const *a, struct hash_elem const *b,
		void *aux);

void
mtrace_init(void)
{
	hash_init(&mtraces_begin_pages, mtrace_begin_hash_func,
			mtrace_begin_equal_func, NULL);
	hash_init(&mtraces_end_pages, mtrace_end_hash_func, mtrace_end_equal_func,
			NULL);
	hash_init(&pte_hash, pte_hash_func, pte_equal, NULL);
	vcpu.cur_mtraces_version = 1;
}

static unsigned
pte_hash_func (struct hash_elem const *e, void *aux)
{
	struct pte_entry *pe;
	pe = hash_entry(e, struct pte_entry, h_elem);
	return (unsigned)pe->pte;
}

static bool
pte_equal (struct hash_elem const *a, struct hash_elem const *b,
		void *aux)
{
	struct pte_entry *mpa, *mpb;
	mpa = hash_entry(a, struct pte_entry, h_elem);
	mpb = hash_entry(b, struct pte_entry, h_elem);
	return (mpa->pte == mpb->pte);
}

static struct mtrace *
page_is_mtraced(target_phys_addr_t paddr)
{
	ASSERT((paddr & PGMASK) == 0);
#define search_hash(type)		do {																							\
	struct list *eqlist;																												\
	struct list_elem *e;																												\
	eqlist = hash_find_bucket_with_hash(&mtraces_##type##_pages, paddr);				\
	for (e = list_begin(eqlist); e != list_end(eqlist); e = list_next(e)) {			\
		struct hash_elem *elem;																										\
		struct mtrace *mtrace;																										\
		/* convert list_elem to hash_elem. */																			\
		elem = list_entry(e, struct hash_elem, list_elem);												\
		/* convert hash_elem to struct mtrace. */																	\
		mtrace = hash_entry(elem, struct mtrace, h_elem_##type);									\
		if ((mtrace->start & ~PGMASK) == paddr) {																	\
			return mtrace;																													\
		}																																					\
	}																																						\
} while (0)
	search_hash(begin);
	search_hash(end);
	return NULL;
}

void
mtrace_add(target_phys_addr_t start, size_t len,
    void (*callback)(target_phys_addr_t start, size_t len, void *opaque),
    void *opaque, struct malloc_cb *malloc_cb)
{
	target_phys_addr_t mtrace_begin_page, mtrace_end_page;
	struct mtrace *mtrace, needle;
	struct hash_elem *e;

	needle.start = start;
	needle.len = len;
	needle.callback = callback;
	needle.opaque = opaque;

	ASSERT(!hash_find(&mtraces_begin_pages, &needle.h_elem_begin));
	ASSERT(!hash_find(&mtraces_end_pages, &needle.h_elem_end));

	ASSERT(malloc_cb);
	ASSERT(malloc_cb->malloc); ASSERT(malloc_cb->lock); ASSERT(malloc_cb->unlock);
	(*malloc_cb->lock)(opaque);
	mtrace = (*malloc_cb->malloc)(sizeof *mtrace);
	(*malloc_cb->unlock)(opaque);
	ASSERT(mtrace);
	mtrace->start = start;
	mtrace->len = len;
	mtrace->callback = callback;
	mtrace->opaque = opaque;
	mtrace->malloc_cb = malloc_cb;
	LOG(MTRACE, "Adding mtrace: %x-%x: %p, %p\n", start, start+len, callback,
			opaque);

	mtrace_begin_page = mtrace->start & ~PGMASK;
	mtrace_end_page = (mtrace->start + mtrace->len - 1) & ~PGMASK;
	if (   !page_is_mtraced(mtrace_begin_page)
		  || (   mtrace_begin_page != mtrace_end_page
				  && !page_is_mtraced(mtrace_end_page))) {
		ASSERT(vcpu.shadow_page_dir[0]);
		shadow_pt_scan(vcpu.shadow_page_dir[0], pte_add_mtrace, mtrace);
		if (vcpu.shadow_page_dir[1]) {
			shadow_pt_scan(vcpu.shadow_page_dir[1], pte_add_mtrace, mtrace);
		}
		vcpu.cur_mtraces_version++;
	}
	hash_insert(&mtraces_begin_pages, &mtrace->h_elem_begin);
	if (mtrace_begin_page != mtrace_end_page) {
		hash_insert(&mtraces_end_pages, &mtrace->h_elem_end);
	}
}

void
mtrace_remove(target_phys_addr_t start, size_t len,
		void (*callback)(target_phys_addr_t start, size_t len, void *opaque),
    void *opaque, struct malloc_cb *malloc_cb UNUSED)
{
	target_phys_addr_t mtrace_begin_page, mtrace_end_page;
	struct mtrace needle, *deleted;
	struct hash_elem *e;

	deleted = NULL;
	needle.start = start;
	needle.len = len;
	needle.callback = callback;
	needle.opaque = opaque;

	mtrace_begin_page = start & ~PGMASK;
	mtrace_end_page = (start + len - 1) & ~PGMASK;

	e = hash_delete(&mtraces_begin_pages, &needle.h_elem_begin);
	ASSERT(e);
	deleted = hash_entry(e, struct mtrace, h_elem_begin);
	if (mtrace_begin_page != mtrace_end_page) {
		struct hash_elem *e2;
		e2 = hash_delete(&mtraces_end_pages, &needle.h_elem_end);
		ASSERT(e2);
		ASSERT(hash_entry(e2, struct mtrace, h_elem_end) == deleted);
	}

	if (   !page_is_mtraced(mtrace_begin_page)
			|| (   mtrace_begin_page != mtrace_end_page
				  && !page_is_mtraced(mtrace_end_page))) {
		ASSERT(vcpu.shadow_page_dir[0]);
		shadow_pt_scan(vcpu.shadow_page_dir[0], pte_remove_mtrace, deleted);
		if (vcpu.shadow_page_dir[1]) {
			shadow_pt_scan(vcpu.shadow_page_dir[0], pte_remove_mtrace, deleted);
		}
		vcpu.cur_mtraces_version++;
	}
	free(deleted);
}

static bool
mtraces_equal(struct mtrace const *a, struct mtrace const *b)
{
	return (   a->start == b->start && a->len == b->len
			    && a->callback == b->callback && a->opaque == b->opaque);
}

static bool
mtrace_begin_equal_func(struct hash_elem const *a, struct hash_elem const *b,
		void *aux)
{
	struct mtrace const *ma = hash_entry(a, struct mtrace, h_elem_begin);
	struct mtrace const *mb = hash_entry(b, struct mtrace, h_elem_begin);
	return mtraces_equal(ma, mb);
}

static unsigned
mtrace_begin_hash_func(struct hash_elem const *e, void *aux)
{
	struct mtrace const *mtrace = hash_entry(e, struct mtrace, h_elem_begin);
	return mtrace->start & ~PGMASK;
}

static bool
mtrace_end_equal_func(struct hash_elem const *a, struct hash_elem const *b,
		void *aux)
{
	struct mtrace const *ma = hash_entry(a, struct mtrace, h_elem_end);
	struct mtrace const *mb = hash_entry(b, struct mtrace, h_elem_end);
	return mtraces_equal(ma, mb);
}

static unsigned
mtrace_end_hash_func(struct hash_elem const *e, void *aux)
{
	struct mtrace const *mtrace = hash_entry(e, struct mtrace, h_elem_end);
	return (mtrace->start + mtrace->len - 1) & ~PGMASK;
}


static bool
belongs_to_phys_map(uint32_t *pte)
{
	target_phys_addr_t pte_page;
	size_t pd_num;
	pte_page = vtop_mon(pte) & ~PGMASK;
	if (vtop_mon(phys_map) == pte_page) {
		return true;
	}
	for (pd_num = 0; pd_num < (LOADER_MONITOR_VIRT_BASE >> LPGBITS); pd_num ++) {
		if (!(phys_map[pd_num] & PTE_P)) {
			continue;
		}
		if (phys_map[pd_num] & PTE_PS) {
			continue;
		}
		if ((phys_map[pd_num] & PTE_ADDR) == pte_page) {
			return true;
		}
	}
	return false;
}

void
pte_add_mtrace(uint32_t *pte, target_phys_addr_t paddr, void *opaque)
{
	struct pte_entry needle, *found;
	struct mtrace *mtrace;
	struct hash_elem *e;

	ASSERT(is_monitor_vaddr(pte));
	ASSERT(!(paddr & PGMASK));
	mtrace = (struct mtrace *)opaque;
	ASSERT(*pte & PTE_P);
	if (!(*pte & PTE_W)) {
		return;
	}
	if (!mtrace) {
		pte_add_all_mtraces(pte, paddr);
		return;
	}
	ASSERT(is_monitor_vaddr(pte));

	if (!(   mtrace->start + mtrace->len > paddr
				&& paddr + PGSIZE > mtrace->start)) {
		return;
	}
	if (belongs_to_phys_map(pte)) {
		return;
	}
	needle.pte = pte;
	e = hash_find(&pte_hash, &needle.h_elem);
	if (e) {
		found = hash_entry(e, struct pte_entry, h_elem);
		ASSERT(found->pte == pte);
		ASSERT((found->pte_val & ~PTE_MASK) == (*found->pte & ~PTE_MASK));
		ASSERT(found->paddr == paddr);
		ASSERT(!(*pte & PTE_W));
	} else {
		(*mtrace->malloc_cb->lock)(mtrace->opaque);
		found = (*mtrace->malloc_cb->malloc)(sizeof *found);
		(*mtrace->malloc_cb->unlock)(mtrace->opaque);
		ASSERT(found);
		ASSERT(*pte & PTE_W);
		found->pte = pte;
		found->pte_val = *pte;
		found->paddr = paddr;
		hash_insert(&pte_hash, &found->h_elem);
		*pte &= ~PTE_W;
	}
}

void
pte_remove_mtrace(uint32_t *pte, target_phys_addr_t paddr, void *opaque)
{
	struct pte_entry needle, *found;
	struct hash_elem *e;
	struct mtrace *mtrace;

  mtrace = (struct mtrace *)opaque;
	if (!mtrace) {
		pte_remove_all_mtraces(pte);
		return;
	}
	ASSERT(is_monitor_vaddr(pte));
	if (!(   mtrace->start + mtrace->len > paddr
				&& paddr + PGSIZE > mtrace->start)) {
		return;
	}
	needle.pte = pte;
	e = hash_find(&pte_hash, &needle.h_elem);
	if (!e) {
		return;
	}
	found = hash_entry(e, struct pte_entry, h_elem);
	ASSERT(found->pte == pte);
	ASSERT(found->paddr == paddr);

	ASSERT((found->paddr & PGMASK) == 0);
	ASSERT(   found->paddr == (mtrace->start & ~PGMASK)
			   || found->paddr == ((mtrace->start + mtrace->len - 1) & ~PGMASK));

	if (!page_is_mtraced(found->paddr)) {
		e = hash_delete(&pte_hash, &needle.h_elem);
		ASSERT(e);
		ASSERT(found == hash_entry(e, struct pte_entry, h_elem));
		*found->pte = found->pte_val;
		free(found);
	}
}

static void
pte_add_all_mtraces(uint32_t *pte, target_phys_addr_t paddr)
{
	struct pte_entry *new_entry;
	struct pte_entry needle;
	struct mtrace *mtrace;

	ASSERT(is_monitor_vaddr(pte));
	ASSERT((*pte & PTE_P) && (*pte & PTE_W));
	if (belongs_to_phys_map(pte)) {
		return;
	}
	needle.pte = pte;
	ASSERT(!hash_find(&pte_hash, &needle.h_elem));
	if (mtrace = page_is_mtraced(paddr)) {
		(*mtrace->malloc_cb->lock)(mtrace->opaque);
		new_entry = (*mtrace->malloc_cb->malloc)(sizeof (struct mtrace));
		(*mtrace->malloc_cb->unlock)(mtrace->opaque);
		ASSERT(new_entry);
		new_entry->pte = pte;
		new_entry->pte_val = *pte;
		new_entry->paddr = paddr;
		hash_insert(&pte_hash, &new_entry->h_elem);
		ASSERT(*pte & PTE_W);
		*pte &= ~PTE_W; 
	}
}

static void
pte_remove_all_mtraces(uint32_t *pte)
{
	struct pte_entry needle, *pe;
	struct hash_elem *e;

	needle.pte = pte;
	ASSERT(is_monitor_vaddr(pte));
	if (!(e = hash_delete(&pte_hash, &needle.h_elem))) {
		return;
	}
	pe = hash_entry(e, struct pte_entry, h_elem);
	DBGn(MTRACE, "%s(): %p %p\n", __func__, pte, pe);
	ASSERT(pte == pe->pte);
	ASSERT((*pte & ~PTE_MASK) == (pe->pte_val & ~PTE_MASK));
	*pte = pe->pte_val;
	free(pe);
}

struct callback_ls_entry {
	void (*callback)(target_phys_addr_t start, size_t len, void *opaque);
	void *opaque;
	struct list_elem l_elem;
};

bool
mtraces_handle_page_fault(uint32_t *shadow_pte, target_ulong fault_addr,
		target_phys_addr_t paddr, struct intr_frame *f)
{
	struct pte_entry needle, *found;
	target_ulong fault_addr2;
	struct list callback_ls;
	struct h_elem *me;
	struct mtrace mneedle;
	size_t memaccess_size;
	struct hash_elem *e;

	needle.pte = shadow_pte;

	if (!(e = hash_find(&pte_hash, &needle.h_elem))) {
		return false;
	}
	LOG(MTRACE, "%s(): fault_addr=0x%x, paddr=0x%x\n", __func__, fault_addr,
			paddr);
	found = hash_entry(e, struct pte_entry, h_elem);
	ASSERT(found->pte == needle.pte);
	ASSERT(is_monitor_vaddr(found->pte));
	ASSERT((*found->pte & ~PTE_MASK) == (found->pte_val & ~PTE_MASK));
	ASSERT(*found->pte != found->pte_val);
	ASSERT((found->paddr & PGMASK) == 0);
	if ((found->paddr & ~PGMASK) != (paddr & ~PGMASK)) {
		printf("vcpu.n_exec=%llx, vcpu.record_log=%p, vcpu.replay_log=%p, "
				"vcpu.cr[3]=%x, found->paddr=%x, fault_addr=%x, paddr=%x\n",
				vcpu.n_exec, vcpu.record_log, vcpu.replay_log, vcpu.cr[3],
				found->paddr, fault_addr, paddr);
	}
	ASSERT((found->paddr & ~PGMASK) == (paddr & ~PGMASK));

	if (!simulate_faulting_instruction(f, &memaccess_size, &fault_addr2)) {
		/* simulation could not complete. */
		/* XXX: update fault_addr and return false. */
		NOT_IMPLEMENTED();
	}

	list_init(&callback_ls);

#define scan_hash(type, addr) do {																						\
	struct list *eqlist;																												\
	struct list_elem *e;																												\
	eqlist = hash_find_bucket_with_hash(&mtraces_##type##_pages, found->paddr);	\
	for (e = list_begin(eqlist); e != list_end(eqlist); e = list_next(e)) {			\
		struct hash_elem *elem;																										\
		struct mtrace *mtrace;																										\
		/* convert list_elem to hash_elem. */																			\
		elem = list_entry(e, struct hash_elem, list_elem);												\
		/* convert hash_elem to struct mtrace. */																	\
		mtrace = hash_entry(elem, struct mtrace, h_elem_##type);									\
		if (((addr) & ~PGMASK) == found->paddr) {																	\
			/* We cannot make the callback because it may spoil the iterators, rather\
			 * just store the callbacks in a list and call them outside the loop. */\
			struct callback_ls_entry *cle;																					\
			(*mtrace->malloc_cb->lock)(mtrace->opaque);															\
			cle = (*mtrace->malloc_cb->malloc)(sizeof *cle);												\
			(*mtrace->malloc_cb->unlock)(mtrace->opaque);														\
			ASSERT(cle);																														\
			cle->callback = mtrace->callback;																				\
			cle->opaque = mtrace->opaque;																						\
			list_push_back(&callback_ls, &cle->l_elem);															\
		}																																					\
	}																																						\
} while (0)

	scan_hash(begin, mtrace->start);
	scan_hash(end, (mtrace->start + mtrace->len - 1));

	ASSERT(!list_empty(&callback_ls));
	while (!list_empty(&callback_ls)) {
		struct list_elem *e = list_pop_front(&callback_ls);
		struct callback_ls_entry *cle;
		cle = list_entry(e, struct callback_ls_entry, l_elem);
		(*cle->callback)(paddr, memaccess_size, cle->opaque);
		free(cle);
	}
	return true;
}
