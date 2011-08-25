#ifndef USERPROG_GDT_H
#define USERPROG_GDT_H

#include <stdint.h>
#include <lib/macros.h>
#include <stdbool.h>
#include <lib/types.h>
#include "sys/loader.h"
#include "sys/vcpu_consts.h"

void gdt_init (void);
void gdt_load(uint32_t base, uint16_t limit);

bool read_segment(uint32_t *e1_ptr, uint32_t *e2_ptr, int selector,
    bool shadow, bool set_accessed);
uint32_t get_seg_base(uint32_t e1, uint32_t e2);
uint32_t get_seg_limit(uint32_t e1, uint32_t e2);
void load_seg_cache(int segno, unsigned int selector, target_ulong base,
    unsigned int limit, unsigned int flags);
void gdt_make_shadow_segdesc(long segno);

void segcache_sync(int segno);

/* segment descriptor fields */
#define DESC_G_MASK     (1 << 23)
#define DESC_B_SHIFT    22
#define DESC_B_MASK     (1 << DESC_B_SHIFT)
#define DESC_L_SHIFT    21 /* x86_64 only : 64 bit code segment */
#define DESC_L_MASK     (1 << DESC_L_SHIFT)
#define DESC_AVL_MASK   (1 << 20)
#define DESC_P_MASK     (1 << 15)
#define DESC_DPL_SHIFT  13
#define DESC_S_MASK     (1 << 12)
#define DESC_TYPE_SHIFT 8
#define DESC_A_MASK     (1 << 8)

#define DESC_CS_MASK    (1 << 11) /* 1=code segment 0=data segment */
#define DESC_C_MASK     (1 << 10) /* code: conforming */
#define DESC_R_MASK     (1 << 9)  /* code: readable */

#define DESC_E_MASK     (1 << 10) /* data: expansion direction */
#define DESC_W_MASK     (1 << 9)  /* data: writable */

#define DESC_TSS_BUSY_MASK (1 << 9)


#endif /* userprog/gdt.h */
