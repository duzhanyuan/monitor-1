#ifndef MEM_SWAP_H
#define MEM_SWAP_H
#include <types.h>
#include <stdbool.h>

enum swap_pagetype {
  SWAP_PAGE 					= 0x0,
  SWAP_PT_USER      	= 0x1,
  SWAP_PT_SUPERVISOR  = 0x2,
  SWAP_PD_USER				= 0x3,
  SWAP_PD_SUPERVISOR	= 0x4,
};

void swap_init(void);
void *swap_get_phys_pt(void);
void *swap_get_page(uint32_t *pte, target_phys_addr_t paddr,
    enum swap_pagetype ptype, uint32_t pte_flags);
void swap_unlock_pds(void);
uint32_t shadow_pte_flags(uint32_t pte);
void swap_disk_read(void *page, target_phys_addr_t paddr);
void swap_load_shadow_page_dirs(void);
void swap_flush(void);

void swap_print_stats(void);

void shadow_pt_scan(uint32_t *pd,
		void (*callback)(uint32_t *pte, target_phys_addr_t paddr, void *opaque),
		void *opaque);

#endif
