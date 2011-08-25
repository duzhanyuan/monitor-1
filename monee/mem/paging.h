#ifndef MEM_PAGING_H
#define MEM_PAGING_H
#include <stddef.h>
#include <types.h>
#include <stdbool.h>
#include "sys/vcpu.h"

#define PTE_ERR ((uint32_t)-1)
#define PDE_ERR ((uint32_t)-2)
#define CR3_INVALID 0xffffffff
#define using_cr3_page_table \
  (((vcpu.cr[0] & CR0_PE_MASK) != 0) && (vcpu.cr[3] != CR3_INVALID))

enum ptwalk_flags_t {
  PTWALK_ASSERT = 0x1,    /* Panic on failure. */
  PTWALK_SHADOW = 0x2,    /* Convert physical addresses in page table to
                             monitor addresses, while walking the page table.*/
  PTWALK_SET_A  = 0x4,    /* Set accessed bit, while walking. */
  PTWALK_SET_D  = 0x8,    /* Set dirty bit, while walking. */
  PTWALK_U  		= 0x10,   /* Access in user privilege mode. */
};

bool pde_error(target_phys_addr_t paddr, uint32_t *pde_entry,
		enum ptwalk_flags_t ptwalk_flags);
bool pte_error(target_phys_addr_t paddr, uint32_t *pte_entry,
		enum ptwalk_flags_t ptwalk_flags);

void ram_init(void);
void paging_init(void);
void shadow_pagedir_sync(void);
void pt_mark_accessed(uint32_t *pd, target_ulong vaddr);
void pt_mark_dirty(uint32_t *pd, target_ulong vaddr);

void cpu_physical_memory_read(target_phys_addr_t addr, uint8_t *buf, int len);
void cpu_physical_memory_write(target_phys_addr_t addr, uint8_t *buf, int len);

target_phys_addr_t pt_walk(uint32_t *pd, target_ulong vaddr,
    uint32_t **pde_p, uint32_t **pte_p, enum ptwalk_flags_t flags);

void *phys_map_install_page(target_ulong fault_page);
void shadow_handle_page_fault(target_ulong fault_addr,
		uint32_t *pde_entry, uint32_t *pte_entry, target_phys_addr_t shadow_paddr,
		uint32_t *shadow_pde, uint32_t *shadow_pte,
		enum ptwalk_flags_t shadow_ptwalk_flags, bool guest_cr3, bool guest_user);

void paging_enable_a20(void);

#endif
