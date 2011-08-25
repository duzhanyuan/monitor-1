#include "mem/paging.h"
#include <types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <macros.h>
#include <hash.h>
#include "mem/malloc.h"
#include "mem/vaddr.h"
#include "mem/pte.h"
#include "mem/pt_mode.h"
#include "mem/palloc.h"
#include "mem/swap.h"
#include "sys/exception.h"
#include "sys/loader.h"
#include "sys/vcpu.h"
#include "sys/mode.h"
#include "mem/palloc.h"

#define SWAP 2
#define MEM  2

#define PG_PRESENT_BIT  0
#define PG_RW_BIT 1
#define PG_USER_BIT 2
#define PG_PWT_BIT  3
#define PG_PCD_BIT  4
#define PG_ACCESSED_BIT 5
#define PG_DIRTY_BIT  6
#define PG_PSE_BIT  7
#define PG_GLOBAL_BIT 8
#define PG_NX_BIT 63

#define PG_PRESENT_MASK  (1 << PG_PRESENT_BIT)
#define PG_RW_MASK   (1 << PG_RW_BIT)
#define PG_USER_MASK   (1 << PG_USER_BIT)
#define PG_PWT_MASK  (1 << PG_PWT_BIT)
#define PG_PCD_MASK  (1 << PG_PCD_BIT)
#define PG_ACCESSED_MASK (1 << PG_ACCESSED_BIT)
#define PG_DIRTY_MASK  (1 << PG_DIRTY_BIT)
#define PG_PSE_MASK  (1 << PG_PSE_BIT)
#define PG_GLOBAL_MASK   (1 << PG_GLOBAL_BIT)
#define PG_NX_MASK   (1LL << PG_NX_BIT)


/* Size of the ram (in pages). */
size_t ram_pages;
/* Offset of monitor on disk (in sectors). */
size_t monitor_ofs;
/* Size of the monitor (in pages). */
size_t monitor_pages;
/* Size of loader (in pages). */
size_t loader_pages;
/* Size of swap space (in pages). */
size_t swap_disk_pages;

/* A20 line. */
//static bool a20_enabled;

#define MAX_PALLOCS 1025
//static void *real_pallocs[MAX_PALLOCS];
//static size_t num_real_pallocs = 0;


/* Clear BSS and obtain RAM size from loader. */
void
ram_init (void)
{
  /* The "BSS" is a segment that should be initialized to zeros.
   * It isn't actually stored on disk or zeroed by the kernel
   * loader, so we have to zero it ourselves.
   *
   * The start and end of the BSS segment is recorded by the
   * linker as _start_bss and _end_bss.  See kernel.lds.
   */
  extern char _start_bss, _end_bss;
  memset (&_start_bss, 0, &_end_bss - &_start_bss);

  /* Get size of ram from loader. */
  ram_pages = *(uint32_t *)BOOTSECTOR_RAM_PGS;

  /* Get offset of monitor on disk (in sectors). */
  monitor_ofs = *(uint32_t *) BOOTSECTOR_MONITOR_OFS;

  /* Get size of monitor from loader. */
  monitor_pages = *(uint32_t *) BOOTSECTOR_MONITOR_PGS;

  /* Get size of loader from bootsector. */
  loader_pages = *(uint32_t *) BOOTSECTOR_LOADER_PGS;

  /* Size of swap_pages, rr_log_pages. */
  swap_disk_pages = 1024;
}

/*
static void
orig_pagedir_real_cleanup(void)
{
  unsigned i;
  for (i = 0; i < num_real_pallocs; i++) {
    palloc_free_page(real_pallocs[i]);
  }
  num_real_pallocs = 0;
}
*/

static void
print_page_table(void)
{
  uint32_t *pd;
  uint32_t *orig_pd = (void *)vcpu.cr[3];
	int mode;
	for (mode = 0; mode <= 1; mode++) {
		size_t i;
		pd = vcpu.shadow_page_dir[mode];
		DBGn(MEM, "%s Page table: %p [%p]\n", mode?"User":"Supervisor",
				vcpu.shadow_page_dir[mode], orig_pd);
		for (i = 0; i < (1 << 10); i++) {
			int j;
			uint32_t pde = pd[i];
			uint32_t orig_pde = orig_pd[i];
			DBGn(MEM, "%d: %x [%x]\n", i, pde, orig_pde);
			/*
				 for (j = 0; j < (1 << 10); j++) {
				 log_printf("\t(%d,%d): (%p, %p): %lx [%lx]\n", i, j, pt, orig_pt, pt[j],
				 orig_pt[j]);
				 }
				 */
		}
	}
}

static void
real_pagedir_load(bool init)
{
	static uint32_t *pd = NULL;
  uint32_t *pt;
  size_t page;
  size_t total_pages = ram_pages;

	ASSERT(init || pd);
	ASSERT(!init || !pd);
	if (!pd) {
		ASSERT(init);
		pd = palloc_get_page(PAL_ASSERT | PAL_ZERO);
		//real_pallocs[num_real_pallocs++] = pd;
	}

  for (page = 0; page < total_pages; page++) {
    /* Shadow page table in real mode. Use identity map, wrapped-around to
     * 1MB (A20 disabled).
     */
    uintptr_t paddr = page * PGSIZE;
    char *vaddr = (void *)paddr;
    size_t pde_idx = pd_no(vaddr), pte_idx = pt_no(vaddr);

    if (   paddr >= LOADER_MONITOR_BASE
        && paddr < LOADER_MONITOR_END) {
      pd[pde_idx] = 0 /* swap_get(paddr) */;
    } else {
      if (init && (paddr % LPGSIZE) == 0) {
				pt = palloc_get_page(PAL_ASSERT);
				//real_pallocs[num_real_pallocs++] = pt;
        pd[pde_idx] = pde_create(pt, true);
      }
      pt = pde_get_pt_mon(pd[pde_idx]);
      pte_idx = pt_no(vaddr);
      if (vcpu.a20_mask == 0xffefffff) {
        pt[pte_idx] = pte_create(paddr & ~(1 << 20), true);
      } else {
        ASSERT(vcpu.a20_mask == 0xffffffff);
        pt[pte_idx] = pte_create(paddr, true);
      }
    }
  }

  for (page = LOADER_MONITOR_BASE; page < LOADER_MONITOR_END; page += LPGSIZE) {
    pd[((target_ulong)ptov_mon(page)) >> LPGBITS]
      = pde_create_large_mon(page, true);
  }

  vcpu.shadow_page_dir[0] = pd;
  vcpu.shadow_page_dir[1] = NULL;
  switch_to_shadow(0);
}

void
shadow_pagedir_sync(void)
{
  if (!using_cr3_page_table) {
		real_pagedir_load(false);
		return;
	}
  mode_t mode;
	int user;

  mode = switch_to_kernel();
  asm volatile ("movl %0, %%cr3" : : "r" (vtop_mon (phys_map)));
  switch_mode(mode);

	swap_load_shadow_page_dirs();

	user = vcpu_get_privilege_level();
	switch_to_shadow(user);

	/*
	if (num_real_pallocs) {
		orig_pagedir_real_cleanup();
	}
	*/
}

/*
void
orig_pagedir_sync(void)
{
  uint32_t *orig_pd, *shadow_pd;
  pt_mode_t pt_mode;
  int i;

  if (!(vcpu.cr[0] & CR0_PE_MASK)) {
    return;
  }

  pt_mode = switch_to_phys();

  orig_pd = (void *)vcpu.cr[3];
  shadow_pd = vcpu.shadow_page_dir;

  for (i = 0; i < (1 << 10); i++) {
    uint32_t shadow_pde = shadow_pd[i];
    uint32_t orig_pde = orig_pd[i];
    uint32_t vaddr = i * LPGSIZE;
    uint32_t const *shadow_pt;
    uint32_t *orig_pt;
    int j;
    if (vaddr < LOADER_MONITOR_VIRT_BASE && (shadow_pd[i] & PG_ACCESSED_MASK)) {
      orig_pd[i] |= PG_ACCESSED_MASK;
    }
    if (!(shadow_pde & PTE_P) || (shadow_pde & PTE_PS)) {
      continue;
    }
    ASSERT(orig_pde & PTE_P);
    ASSERT(!(orig_pde & PTE_PS));
    shadow_pt = (void *)(shadow_pde & PTE_ADDR);
    if (   (long)shadow_pt >= LOADER_MONITOR_BASE
        && (long)shadow_pt < LOADER_MONITOR_END) {
      shadow_pt = ptov_mon((uint32_t)shadow_pt);
      orig_pt = (void *)(orig_pde & PTE_ADDR);
      for (j = 0; j < (1 << 10); j++) {
        if (shadow_pt[j] & PG_ACCESSED_MASK) {
          orig_pt[j] |= PG_ACCESSED_MASK;
        }
        if (shadow_pt[j] & PG_DIRTY_MASK) {
          orig_pt[j] |= PG_DIRTY_MASK;
        }
      }
    }
  }
  switch_pt(pt_mode);
}
*/

static void
phys_map_init(void)
{
  size_t page;
  phys_map = palloc_get_page(PAL_ASSERT | PAL_ZERO);
	MSG("%s(): phys_map=%p\n", __func__, phys_map);
  for (page = 0; page < (1 << 10); page++) {
    target_ulong paddr = page * LPGSIZE;
    if (paddr >= LOADER_MONITOR_BASE && paddr < LOADER_MONITOR_END) {
      int i;
      uint32_t *swap_pt;
      swap_pt = swap_get_phys_pt();
			MSG("%s(): swap_pt=%p\n", __func__, swap_pt);
      phys_map[page] = pde_create_mon(vtop_mon(swap_pt), true);
      /*
      for (i = 0; i < (1 << 10); i++) {
        swap_page[i] = swap_pte(paddr);
        paddr += PGSIZE;
      }
      */
    } else {
      phys_map[page] = pde_create_large_mon(paddr, true);
    }
  }

  for (page = LOADER_MONITOR_BASE; page < LOADER_MONITOR_END; page += LPGSIZE) {
    phys_map[((target_ulong)ptov_mon(page)) >> LPGBITS]
      = pde_create_large_mon(page, true);
  }
}

void
paging_enable_a20(void)
{
  //a20_enabled = true;
  vcpu.a20_mask = 0xffffffff;
  if (!using_cr3_page_table) {
    real_pagedir_load(false);
  }
}

static void
ioport_enable_a20(void *opaque, uint16_t port, uint32_t data)
{
  size_t page;

  if (data & 2) {
    if (vcpu.a20_mask == 0xffefffff) {
      paging_enable_a20();
    }
  }
}

/* Populates the base page directory and the page table with the monitor's
 * virtual mapping, and then sets up the CPU to use the new page directory.
 * Points mon_page_dir to page directory it creates.
 *
 * We maintain an identity map from physical to virtual memory using 4MB pages,
 * except LOADER_MONITOR_BASE to LOADER_MONITOR_BASE+MONITOR_SIZE. This
 * range of physical addresses is mapped at virtual addresses starting at
 * LOADER_MONITOR_VIRT_BASE.
 *
 * The virtual addresses LOADER_MONITOR_BASE to LOADER_MONITOR_BASE+MONITOR_SIZE
 * are marked not-present.
 *
 * The monitor pages starting at vaddr MONITOR_BASE are user pages. They
 * are protected from the guest using segmentation.
 */
void
paging_init(void)
{
  //a20_enabled = false;
  vcpu.a20_mask = 0xffefffff;

  phys_map_init();
	vcpu.shadow_page_dir[0] = NULL;
	vcpu.shadow_page_dir[1] = NULL;
  register_ioport_write(0x60, 1, 1, ioport_enable_a20, NULL, false);// remove this on implementing keyboard emulation.
  register_ioport_write(0x92, 1, 1, ioport_enable_a20, NULL, false);
	real_pagedir_load(true);
}

static void
cpu_physical_memory_rw(target_phys_addr_t addr, uint8_t *buf, int len,
    bool is_write)
{
  uint8_t *ptr = (void *)addr;
  enum mode_t pt_mode;
  target_phys_addr_t cr3;
  int i;

  pt_mode = switch_to_phys();
  //asm volatile ("movl %%cr3, %0" : "=r" (cr3));
  //asm volatile ("movl %0, %%cr3" : : "r" (vtop_mon(phys_map)));
  if (is_write) {
    memcpy(ptr, buf, len);
  } else {
    memcpy(buf, ptr, len);
  }
  //asm volatile ("movl %0, %%cr3" : : "r" (cr3));
  switch_pt(pt_mode);
}

void
cpu_physical_memory_read(target_phys_addr_t addr, uint8_t *buf, int len)
{
  cpu_physical_memory_rw(addr, buf, len, false);
}

void
cpu_physical_memory_write(target_phys_addr_t addr, uint8_t *buf, int len)
{
  cpu_physical_memory_rw(addr, buf, len, true);
}


target_phys_addr_t
pt_walk(uint32_t *pd, target_ulong vaddr, uint32_t **pde_p,
    uint32_t **pte_p, enum ptwalk_flags_t flags)
{
  target_phys_addr_t phys;
  target_ulong pde, pte;
  int pd_num, pt_num;
  uint32_t *pt;

	ASSERT(!pde_p || is_monitor_vaddr(pde_p));
	ASSERT(!pte_p || is_monitor_vaddr(pte_p));
	ASSERT(!(flags & PTWALK_SHADOW) || is_monitor_vaddr(pd));
  if (!using_cr3_page_table && !(flags & PTWALK_SHADOW)) {
    if (pde_p) {
      *pde_p = NULL;
    }
    if (pte_p) {
      *pte_p = NULL;
    }
    return vaddr;
  }

  pd_num = (vaddr & PDMASK) >> PDSHIFT;
	if (flags & PTWALK_SHADOW) {
		pde = pd[pd_num];
	} else {
		pde = ldl_phys(&pd[pd_num]);
	}
  if (pde_p) {
    *pde_p = &pd[pd_num];
  }
  if (!(pde & PTE_P)) {
    if (flags & PTWALK_ASSERT) {
      PANIC ("%s: pt not present.\n", __func__);
    }
		return PDE_ERR;
  }
  if (!(pde & PTE_W) && (flags & PTWALK_SET_D)) {
    if (flags & PTWALK_ASSERT) {
      PANIC ("%s: pt not writable.\n", __func__);
    }
		return PDE_ERR;
  }
	if (!(pde & PTE_U) && (flags & PTWALK_U)) {
    if (flags & PTWALK_ASSERT) {
      PANIC ("%s: pt not user-accessible.\n", __func__);
    }
		return PDE_ERR;
	}

  if (!(flags & PTWALK_SHADOW) && (flags & PTWALK_SET_A)) {
    DBGn(SWAP, "%s() %d: setting pd[pd_num](%p) accessed. vaddr=0x%x, "
        "pd_num=%d\n", __func__, __LINE__, &pd[pd_num], vaddr, pd_num);
    //pd[pd_num] |= PTE_A;
		pde |= PTE_A;
		stl_phys(&pd[pd_num], pde);
  }

  if (pde & PTE_PS) {
		ASSERT(!(flags & PTWALK_SHADOW));
    /* large page. */
    phys = pde & LPTE_ADDR;
    if (pte_p) {
      *pte_p = NULL;
    }
    return phys + (vaddr & LPGMASK);
  }
  pt = (void *)(pde & PTE_ADDR);
  pt_num = (vaddr & PTMASK) >> PTSHIFT;
  if (flags & PTWALK_SHADOW) {
		pt = ptov_mon(pt);
		pte = pt[pt_num];
  } else {
		pte = ldl_phys(&pt[pt_num]);
	}
  if (pte_p) {
    *pte_p = &pt[pt_num];
  }
  if (!(pte & PTE_P)) {
    if (flags & PTWALK_ASSERT) {
      PANIC ("%s: page not present.\n", __func__);
    }
    return PTE_ERR;
  }
  if (!(pte & PTE_W) && (flags & PTWALK_SET_D)) {
    if (flags & PTWALK_ASSERT) {
      PANIC ("%s: page not writable.\n", __func__);
    }
		return PTE_ERR;
  }
  if (!(pte & PTE_U) && (flags & PTWALK_U)) {
    if (flags & PTWALK_ASSERT) {
      PANIC ("%s: page not user-accessible.\n", __func__);
    }
		return PTE_ERR;
  }

	if (!(flags & PTWALK_SHADOW)) {
		if (flags & PTWALK_SET_A) {
			pte |= PTE_A;
			DBGn(SWAP, "%s() %d: setting pt[pt_num](%p) accessed. pte=%x, vaddr=0x%x,"
					" pd_num=%d\n", __func__, __LINE__, &pt[pt_num], pte, vaddr, pt_num);
			stl_phys(&pt[pt_num], pte);
			ASSERT(ldl_phys(&pt[pt_num]) & PTE_A);
			//pt[pt_num] |= PTE_A;
		}
		if (flags & PTWALK_SET_D) {
			DBGn(SWAP, "setting pt[%d](%p) dirty.\n", pt_num, &pt[pt_num]);
			pte |= PTE_D;
			stl_phys(&pt[pt_num], pte);
			//pt[pt_num] |= PTE_D;
		}
	}

  phys = pte & PTE_ADDR;
  return phys + (vaddr & PGMASK);
}

static void
pt_mark_accessed_dirty(uint32_t *pd, target_ulong vaddr, bool dirty)
{
  target_ulong pde, pte;
  int pd_num, pt_num;
  uint32_t *pt;
  pt_mode_t pt_mode;

  if (!using_cr3_page_table) {
    return;
  }
  pt_mode = switch_to_phys();
  pd_num = (vaddr & PDMASK) >> PDSHIFT;
  pde = pd[pd_num];
  if (!(pde & PTE_P)) {
    goto done;
  }
  if (pde & PTE_PS) {
    goto done;
  }
  pd[pd_num] |= PG_ACCESSED_MASK;
  pt = (void *)(pde & PTE_ADDR);
  pt_num = (vaddr & PTMASK) >> PTSHIFT;
  pte = pt[pt_num];
  if (!(pte & PTE_P)) {
    goto done;
  }
  pt[pt_num] |= PG_ACCESSED_MASK;
  if (dirty) {
    pt[pt_num] |= PG_DIRTY_MASK;
  }
done:
  switch_pt(pt_mode);
}

void
pt_mark_accessed(uint32_t *pd, target_ulong vaddr)
{
  pt_mark_accessed_dirty(pd, vaddr, false);
}

void
pt_mark_dirty(uint32_t *pd, target_ulong vaddr)
{
  pt_mark_accessed_dirty(pd, vaddr, true);
}

void *
phys_map_install_page(target_ulong fault_page)
{
  uint32_t *swap_pt;
  int pd_num, pt_num;
  mode_t mode;
  void *page;

  ASSERT((fault_page & PGMASK) == 0);
  ASSERT(fault_page >= LOADER_MONITOR_BASE && fault_page < LOADER_MONITOR_END);
  pt_num = (fault_page & PTMASK) >> PTSHIFT;
  pd_num = (fault_page & PDMASK) >> PDSHIFT;
  ASSERT(phys_map[pd_num] & PTE_P);
  swap_pt = ptov_mon(phys_map[pd_num] & PTE_ADDR);

  ASSERT(!(swap_pt[pt_num] & PTE_P));
  page = swap_get_page(&swap_pt[pt_num], fault_page, SWAP_PAGE,
			PTE_P | PTE_W | PTE_U);
  swap_disk_read(page, fault_page);
  //swap_pt[pt_num] = pte_create(vtop_mon(page), true);
  DBGn(SWAP, "setting swap_pt[0x%x](%p) to 0x%x\n", pt_num, &swap_pt[pt_num],
      swap_pt[pt_num]);

  mode = switch_to_kernel();
  asm volatile ("movl %0, %%cr3" : : "r" (vtop_mon (phys_map)));
  switch_mode(mode);
  return page;
}

static void
pd_install_shadow_pt(uint32_t pde, uint32_t *pde_shadow, bool user)
{
  uint32_t *swap_pt, *pt;
	int cpl;
  int i;

  if (pde & PTE_PS) {
    pt = NULL;
  } else {
    pt = ptov_phy(pde & PTE_ADDR);
  }

  swap_pt = swap_get_page(pde_shadow, vtop_phy(pt),
			user?SWAP_PT_USER:SWAP_PT_SUPERVISOR, (pde & PTE_FLAGS & ~PTE_PS)|PTE_U);
  DBGn(SWAP, "%d: pde_shadow=%p *pde_shadow=0x%x\n", __LINE__, pde_shadow,
      *pde_shadow);
}

static void
pd_install_shadow_page(uint32_t pte, uint32_t *pte_shadow)
{
  target_phys_addr_t paddr;
  void *swap_page;
  paddr = pte & PTE_ADDR;
  if (paddr >= LOADER_MONITOR_BASE && paddr < LOADER_MONITOR_END) {
    swap_page = swap_get_page(pte_shadow, paddr, SWAP_PAGE,
				shadow_pte_flags(pte & PTE_FLAGS));
    swap_disk_read(swap_page, paddr);
    DBGn(SWAP, "%d: pte_shadow=%p, *pte_shadow=0x%x\n", __LINE__, pte_shadow,
        *pte_shadow);
    return;
  }
  *pte_shadow = (paddr & PTE_ADDR) | shadow_pte_flags(pte & PTE_FLAGS);
	pte_add_mtrace(pte_shadow, paddr, NULL);
  DBGn(SWAP, "%d: pte_shadow=%p pte_shadow=0x%x\n", __LINE__, pte_shadow,
      *pte_shadow);
}

void
shadow_handle_page_fault(target_ulong fault_addr,
		uint32_t *pde_entry, uint32_t *pte_entry, target_phys_addr_t shadow_paddr,
		uint32_t *pde_shadow, uint32_t *pte_shadow,
		enum ptwalk_flags_t shadow_ptwalk_flags, bool guest_cr3, bool guest_user)
{
  bool large_page = false;
  uint32_t pde = 0;

	if (guest_cr3) {
		pde = ldl_phys(pde_entry);
		large_page = (pde & PTE_PS)?true:false;
		ASSERT(pde);
	}

  if (pde_error(shadow_paddr, pde_shadow, shadow_ptwalk_flags)) {
    if (guest_cr3) {
			ASSERT(pde);
      pd_install_shadow_pt(pde, (uint32_t *)pde_shadow, guest_user);
    } else {
      /* must be real mode */
      uint32_t pde_new;
      ASSERT(!pde_entry);
			ASSERT(!pde);
      /* Pretend it is a large page. */
      pde_new = (fault_addr & LPTE_ADDR) | PTE_PS | PTE_P | PTE_W
        | PTE_A | PTE_D;
      pd_install_shadow_pt(pde_new, (uint32_t *)pde_shadow, guest_user);
    }
  } else if (pte_error(shadow_paddr, pte_shadow, shadow_ptwalk_flags)) {
    if (guest_cr3) {
      if (large_page) {
        uint32_t pte_new;
        ASSERT(!pte_entry);
        pte_new = (fault_addr & PTE_ADDR) | (pde & PTE_FLAGS & ~PTE_PS
            & ~PTE_G);
        pd_install_shadow_page(pte_new, (uint32_t *)pte_shadow) ;
      } else {
				uint32_t pte;
				pte = ldl_phys(pte_entry);
        pd_install_shadow_page(pte, (uint32_t *)pte_shadow) ;
      }
    } else {
      /* must be real mode */
      uint32_t pte_new;
      ASSERT(!pte_entry);
      pte_new = (fault_addr & PTE_ADDR) | PTE_P | PTE_W | PTE_A | PTE_D;
      pd_install_shadow_page(pte_new, (uint32_t *)pte_shadow);
    }
  }
}

static inline bool
pde_pte_error(target_phys_addr_t paddr, uint32_t *pte_entry,
		enum ptwalk_flags_t ptwalk_flags, uint32_t errno)
{
	if (paddr == errno && pte_entry) {
		if (   (ptwalk_flags & PTWALK_SET_A)
				|| (ptwalk_flags & PTWALK_SET_D)
				|| (ptwalk_flags & PTWALK_U)) {
			uint32_t pte;
			if (is_monitor_vaddr(pte_entry)) {
				ASSERT(ptwalk_flags & PTWALK_SHADOW);
				pte = *pte_entry;
			} else {
				pte = ldl_phys(pte_entry);
			}
			if ((ptwalk_flags & PTWALK_SET_A) && !(pte & PTE_P)) {
				return true;
			}
			if ((ptwalk_flags & PTWALK_SET_D) && !(pte & PTE_W)) {
				return true;
			}
			if ((ptwalk_flags & PTWALK_U) && !(pte & PTE_U)) {
				return true;
			}
		}
	}
	return false;
}

bool
pde_error(target_phys_addr_t paddr, uint32_t *pde_entry,
		enum ptwalk_flags_t ptwalk_flags)
{
	return pde_pte_error(paddr, pde_entry, ptwalk_flags, PDE_ERR);
}

bool
pte_error(target_phys_addr_t paddr, uint32_t *pte_entry,
		enum ptwalk_flags_t ptwalk_flags)
{
	return pde_pte_error(paddr, pte_entry, ptwalk_flags, PTE_ERR);
}
