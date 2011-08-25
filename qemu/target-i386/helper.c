/*
 *  i386 helpers
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <assert.h>
#include <errno.h>
#include "exec.h"
#include "mdbg.h"
#include "profile_log.h"

FILE *fopen(char const *, char const *);

//#define DEBUG_PCALL

const uint8_t parity_table[256] = {
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    CC_P, 0, 0, CC_P, 0, CC_P, CC_P, 0,
    0, CC_P, CC_P, 0, CC_P, 0, 0, CC_P,
};

/* modulo 17 table */
const uint8_t rclw_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 
    8, 9,10,11,12,13,14,15,
   16, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 9,10,11,12,13,14,
};

/* modulo 9 table */
const uint8_t rclb_table[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 
    8, 0, 1, 2, 3, 4, 5, 6,
    7, 8, 0, 1, 2, 3, 4, 5, 
    6, 7, 8, 0, 1, 2, 3, 4,
};

const CPU86_LDouble f15rk[7] =
{
    0.00000000000000000000L,
    1.00000000000000000000L,
    3.14159265358979323851L,  /*pi*/
    0.30102999566398119523L,  /*lg2*/
    0.69314718055994530943L,  /*ln2*/
    1.44269504088896340739L,  /*l2e*/
    3.32192809488736234781L,  /*l2t*/
};

static long aregs[4];
/* save_caller_regs() must be called before restore_caller_regs() in function's
 * main scope.
 */
#define save_caller_regs() do {                                                \
  asm volatile("movl %%" AREG0 ", %0" : "=m"(aregs[0]));                       \
  asm volatile("movl %%" AREG1 ", %0" : "=m"(aregs[1]));                       \
  asm volatile("movl %%" AREG2 ", %0" : "=m"(aregs[2]));                       \
  asm volatile("movl %%" AREG3 ", %0" : "=m"(aregs[3]));                       \
} while(0)
/* restore_caller_regs() must be called at most once in a function. */
#define restore_caller_regs() do {                                            \
  asm volatile("movl %0, %%" AREG0 : "=m"(aregs[0]));                       \
  asm volatile("movl %0, %%" AREG1 : "=m"(aregs[1]));                       \
  asm volatile("movl %0, %%" AREG2 : "=m"(aregs[2]));                       \
  asm volatile("movl %0, %%" AREG3 : "=m"(aregs[3]));                       \
} while(0)
 
    
/* thread support */

spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void cpu_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void cpu_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

void cpu_loop_exit(void)
{
    /* NOTE: the register at this point must be saved by hand because
       longjmp restore them */
    regs_to_env();
    longjmp(env->jmp_env, 1);
}

/* return non zero if error */
static inline int load_segment(uint32_t *e1_ptr, uint32_t *e2_ptr,
                               int selector)
{
    SegmentCache *dt;
    int index;
    target_ulong ptr;

    if (selector & 0x4) {
        dt = &env->ldt;
    } else {
        dt = &env->gdt;
    }
    index = selector & ~7;
    if ((index + 7) > dt->limit) {
        MDBG("selector=0x%x, index=%d, dt->limit=%d\n", selector, index,
            dt->limit);
        return -1;
    }
    ptr = dt->base + index;
    *e1_ptr = ldl_kernel(ptr);
    *e2_ptr = ldl_kernel(ptr + 4);

    /* sorav */
    if (!(*e2_ptr  & DESC_A_MASK)) {
      *e2_ptr |= DESC_A_MASK;
      stl_kernel(ptr + 4, *e2_ptr);
    }
    /* sorav */
    return 0;
}
                                     
static inline unsigned int get_seg_limit(uint32_t e1, uint32_t e2)
{
    unsigned int limit;
    limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & DESC_G_MASK)
        limit = (limit << 12) | 0xfff;
    return limit;
}

static inline uint32_t get_seg_base(uint32_t e1, uint32_t e2)
{
    return ((e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000));
}

static inline void load_seg_cache_raw_dt(SegmentCache *sc, uint32_t e1, uint32_t e2)
{
    sc->base = get_seg_base(e1, e2);
    sc->limit = get_seg_limit(e1, e2);
    sc->flags = e2;
}

/* init the segment cache in vm86 mode. */
static inline void load_seg_vm(int seg, int selector)
{
    selector &= 0xffff;
    cpu_x86_load_seg_cache(env, seg, selector, 
                           (selector << 4), 0xffff, 0);
}

static inline void get_ss_esp_from_tss(uint32_t *ss_ptr, 
                                       uint32_t *esp_ptr, int dpl)
{
    int type, index, shift;

    if (!(env->tr.flags & DESC_P_MASK))
        cpu_abort(env, "invalid tss");
    type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
    if ((type & 7) != 1)
        cpu_abort(env, "invalid tss type");
    shift = type >> 3;
    index = (dpl * 4 + 2) << shift;
    if (index + (4 << shift) - 1 > env->tr.limit) {
        raise_exception_err(EXCP0A_TSS, env->tr.selector & 0xfffc);
    }
    if (shift == 0) {
        *esp_ptr = lduw_kernel(env->tr.base + index);
        *ss_ptr = lduw_kernel(env->tr.base + index + 2);
    } else {
        *esp_ptr = ldl_kernel(env->tr.base + index);
        *ss_ptr = lduw_kernel(env->tr.base + index + 4);
    }
}

/* XXX: merge with load_seg() */
static void tss_load_seg(int seg_reg, int selector)
{
    uint32_t e1, e2;
    int rpl, dpl, cpl;

    if ((selector & 0xfffc) != 0) {
        if (load_segment(&e1, &e2, selector) != 0) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
        }
        if (!(e2 & DESC_S_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
        }
        rpl = selector & 3;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        cpl = env->hflags & HF_CPL_MASK;
        if (seg_reg == R_CS) {
            if (!(e2 & DESC_CS_MASK)) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
                raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
            }
            /* XXX: is it correct ? */
            if (dpl != rpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
                raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
            }
            if ((e2 & DESC_C_MASK) && dpl > rpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
                raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
            }
        } else if (seg_reg == R_SS) {
            /* SS must be writable data */
            if ((e2 & DESC_CS_MASK) || !(e2 & DESC_W_MASK)) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
                raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
            }
            if (dpl != cpl || dpl != rpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
                raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
            }
        } else {
            /* not readable code */
            if ((e2 & DESC_CS_MASK) && !(e2 & DESC_R_MASK)) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
                raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
            }
            /* if data or non conforming code, checks the rights */
            if (((e2 >> DESC_TYPE_SHIFT) & 0xf) < 12) {
                if (dpl < cpl || dpl < rpl) {
                    MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
                    raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
                }
            }
        }
        if (!(e2 & DESC_P_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
        }
        cpu_x86_load_seg_cache(env, seg_reg, selector, 
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
    } else {
        if (seg_reg == R_SS || seg_reg == R_CS) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, selector & 0xfffc);
        }
    }
}

#define SWITCH_TSS_JMP  0
#define SWITCH_TSS_IRET 1
#define SWITCH_TSS_CALL 2

/* XXX: restore CPU state in registers (PowerPC case) */
static void switch_tss(int tss_selector, 
                       uint32_t e1, uint32_t e2, int source,
                       uint32_t next_eip)
{
    int tss_limit, tss_limit_max, type, old_tss_limit_max, old_type, v1, v2, i;
    target_ulong tss_base;
    uint32_t new_regs[8], new_segs[6];
    uint32_t new_eflags, new_eip, new_cr3, new_ldt, new_trap;
    uint32_t old_eflags, eflags_mask;
    SegmentCache *dt;
    int index;
    target_ulong ptr;

    type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_PCALL)
        fprintf(logfile, "switch_tss: sel=0x%04x type=%d src=%d\n", tss_selector, type, source);
#endif

    /* if task gate, we read the TSS segment and we load it */
    if (type == 5) {
        if (!(e2 & DESC_P_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0B_NOSEG);
            raise_exception_err(EXCP0B_NOSEG, tss_selector & 0xfffc);
        }
        tss_selector = e1 >> 16;
        if (tss_selector & 4) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, tss_selector & 0xfffc);
        }
        if (load_segment(&e1, &e2, tss_selector) != 0) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, tss_selector & 0xfffc);
        }
        if (e2 & DESC_S_MASK) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, tss_selector & 0xfffc);
        }
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        if ((type & 7) != 1) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, tss_selector & 0xfffc);
        }
    }

    if (!(e2 & DESC_P_MASK)) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0B_NOSEG);
        raise_exception_err(EXCP0B_NOSEG, tss_selector & 0xfffc);
    }

    if (type & 8)
        tss_limit_max = 103;
    else
        tss_limit_max = 43;
    tss_limit = get_seg_limit(e1, e2);
    tss_base = get_seg_base(e1, e2);
    if ((tss_selector & 4) != 0 || 
        tss_limit < tss_limit_max) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
        raise_exception_err(EXCP0A_TSS, tss_selector & 0xfffc);
    }
    old_type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
    if (old_type & 8)
        old_tss_limit_max = 103;
    else
        old_tss_limit_max = 43;

    /* read all the registers from the new TSS */
    if (type & 8) {
        /* 32 bit */
        new_cr3 = ldl_kernel(tss_base + 0x1c);
        new_eip = ldl_kernel(tss_base + 0x20);
        new_eflags = ldl_kernel(tss_base + 0x24);
        for(i = 0; i < 8; i++)
            new_regs[i] = ldl_kernel(tss_base + (0x28 + i * 4));
        for(i = 0; i < 6; i++)
            new_segs[i] = lduw_kernel(tss_base + (0x48 + i * 4));
        new_ldt = lduw_kernel(tss_base + 0x60);
        new_trap = ldl_kernel(tss_base + 0x64);
    } else {
        /* 16 bit */
        new_cr3 = 0;
        new_eip = lduw_kernel(tss_base + 0x0e);
        new_eflags = lduw_kernel(tss_base + 0x10);
        for(i = 0; i < 8; i++)
            new_regs[i] = lduw_kernel(tss_base + (0x12 + i * 2)) | 0xffff0000;
        for(i = 0; i < 4; i++)
            new_segs[i] = lduw_kernel(tss_base + (0x22 + i * 4));
        new_ldt = lduw_kernel(tss_base + 0x2a);
        new_segs[R_FS] = 0;
        new_segs[R_GS] = 0;
        new_trap = 0;
    }
    
    /* NOTE: we must avoid memory exceptions during the task switch,
       so we make dummy accesses before */
    /* XXX: it can still fail in some cases, so a bigger hack is
       necessary to valid the TLB after having done the accesses */

    v1 = ldub_kernel(env->tr.base);
    v2 = ldub_kernel(env->tr.base + old_tss_limit_max);
    stb_kernel(env->tr.base, v1);
    stb_kernel(env->tr.base + old_tss_limit_max, v2);
    
    /* clear busy bit (it is restartable) */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_IRET) {
        target_ulong ptr;
        uint32_t e2;
        ptr = env->gdt.base + (env->tr.selector & ~7);
        e2 = ldl_kernel(ptr + 4);
        e2 &= ~DESC_TSS_BUSY_MASK;
        stl_kernel(ptr + 4, e2);
    }
    old_eflags = compute_eflags();
    if (source == SWITCH_TSS_IRET)
        old_eflags &= ~NT_MASK;
    
    /* save the current state in the old TSS */
    if (type & 8) {
        /* 32 bit */
        stl_kernel(env->tr.base + 0x20, next_eip);
        stl_kernel(env->tr.base + 0x24, old_eflags);
        stl_kernel(env->tr.base + (0x28 + 0 * 4), EAX);
        stl_kernel(env->tr.base + (0x28 + 1 * 4), ECX);
        stl_kernel(env->tr.base + (0x28 + 2 * 4), EDX);
        stl_kernel(env->tr.base + (0x28 + 3 * 4), EBX);
        stl_kernel(env->tr.base + (0x28 + 4 * 4), ESP);
        stl_kernel(env->tr.base + (0x28 + 5 * 4), EBP);
        stl_kernel(env->tr.base + (0x28 + 6 * 4), ESI);
        stl_kernel(env->tr.base + (0x28 + 7 * 4), EDI);
        for(i = 0; i < 6; i++)
            stw_kernel(env->tr.base + (0x48 + i * 4), env->segs[i].selector);
    } else {
        /* 16 bit */
        stw_kernel(env->tr.base + 0x0e, next_eip);
        stw_kernel(env->tr.base + 0x10, old_eflags);
        stw_kernel(env->tr.base + (0x12 + 0 * 2), EAX);
        stw_kernel(env->tr.base + (0x12 + 1 * 2), ECX);
        stw_kernel(env->tr.base + (0x12 + 2 * 2), EDX);
        stw_kernel(env->tr.base + (0x12 + 3 * 2), EBX);
        stw_kernel(env->tr.base + (0x12 + 4 * 2), ESP);
        stw_kernel(env->tr.base + (0x12 + 5 * 2), EBP);
        stw_kernel(env->tr.base + (0x12 + 6 * 2), ESI);
        stw_kernel(env->tr.base + (0x12 + 7 * 2), EDI);
        for(i = 0; i < 4; i++)
            stw_kernel(env->tr.base + (0x22 + i * 4), env->segs[i].selector);
    }
    
    /* now if an exception occurs, it will occurs in the next task
       context */

    if (source == SWITCH_TSS_CALL) {
        stw_kernel(tss_base, env->tr.selector);
        new_eflags |= NT_MASK;
    }

    /* set busy bit */
    if (source == SWITCH_TSS_JMP || source == SWITCH_TSS_CALL) {
        target_ulong ptr;
        uint32_t e2;
        ptr = env->gdt.base + (tss_selector & ~7);
        e2 = ldl_kernel(ptr + 4);
        e2 |= DESC_TSS_BUSY_MASK;
        stl_kernel(ptr + 4, e2);
    }

    /* set the new CPU state */
    /* from this point, any exception which occurs can give problems */
    env->cr[0] |= CR0_TS_MASK;
    env->hflags |= HF_TS_MASK;
    env->tr.selector = tss_selector;
    env->tr.base = tss_base;
    env->tr.limit = tss_limit;
    env->tr.flags = e2 & ~DESC_TSS_BUSY_MASK;
    
    if ((type & 8) && (env->cr[0] & CR0_PG_MASK)) {
        cpu_x86_update_cr3(env, new_cr3);
    }
    
    /* load all registers without an exception, then reload them with
       possible exception */
    env->eip = new_eip;
    eflags_mask = TF_MASK | AC_MASK | ID_MASK | 
        IF_MASK | IOPL_MASK | VM_MASK | RF_MASK | NT_MASK;
    if (!(type & 8))
        eflags_mask &= 0xffff;
    load_eflags(new_eflags, eflags_mask);
    /* XXX: what to do in 16 bit case ? */
    EAX = new_regs[0];
    ECX = new_regs[1];
    EDX = new_regs[2];
    EBX = new_regs[3];
    ESP = new_regs[4];
    EBP = new_regs[5];
    ESI = new_regs[6];
    EDI = new_regs[7];
    if (new_eflags & VM_MASK) {
        for(i = 0; i < 6; i++) 
            load_seg_vm(i, new_segs[i]);
        /* in vm86, CPL is always 3 */
        cpu_x86_set_cpl(env, 3);
    } else {
        /* CPL is set the RPL of CS */
        cpu_x86_set_cpl(env, new_segs[R_CS] & 3);
        /* first just selectors as the rest may trigger exceptions */
        for(i = 0; i < 6; i++)
            cpu_x86_load_seg_cache(env, i, new_segs[i], 0, 0, 0);
    }
    
    env->ldt.selector = new_ldt & ~4;
    env->ldt.base = 0;
    env->ldt.limit = 0;
    env->ldt.flags = 0;

    /* load the LDT */
    if (new_ldt & 4) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
        raise_exception_err(EXCP0A_TSS, new_ldt & 0xfffc);
    }

    if ((new_ldt & 0xfffc) != 0) {
        dt = &env->gdt;
        index = new_ldt & ~7;
        if ((index + 7) > dt->limit) {
            MDBG("new_ldt=0x%x, index=%d, dt->limit=%d. Calling "
                "raise_exception_err(0x%x).\n", new_ldt, index, dt->limit,
                EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, new_ldt & 0xfffc);
        }
        ptr = dt->base + index;
        e1 = ldl_kernel(ptr);
        e2 = ldl_kernel(ptr + 4);
        if ((e2 & DESC_S_MASK) || ((e2 >> DESC_TYPE_SHIFT) & 0xf) != 2) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, new_ldt & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, new_ldt & 0xfffc);
        }
        load_seg_cache_raw_dt(&env->ldt, e1, e2);
    }
    
    /* load the segments */
    if (!(new_eflags & VM_MASK)) {
        tss_load_seg(R_CS, new_segs[R_CS]);
        tss_load_seg(R_SS, new_segs[R_SS]);
        tss_load_seg(R_ES, new_segs[R_ES]);
        tss_load_seg(R_DS, new_segs[R_DS]);
        tss_load_seg(R_FS, new_segs[R_FS]);
        tss_load_seg(R_GS, new_segs[R_GS]);
    }
    
    /* check that EIP is in the CS segment limits */
    if (new_eip > env->segs[R_CS].limit) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        /* XXX: different exception if CALL ? */
        raise_exception_err(EXCP0D_GPF, 0);
    }
}

/* check if Port I/O is allowed in TSS */
static inline void check_io(int addr, int size)
{
    int io_offset, val, mask;
    
    /* TSS must be a valid 32 bit one */
    if (!(env->tr.flags & DESC_P_MASK) ||
        ((env->tr.flags >> DESC_TYPE_SHIFT) & 0xf) != 9 ||
        env->tr.limit < 103)
        goto fail;
    io_offset = lduw_kernel(env->tr.base + 0x66);
    io_offset += (addr >> 3);
    /* Note: the check needs two bytes */
    if ((io_offset + 1) > env->tr.limit)
        goto fail;
    val = lduw_kernel(env->tr.base + io_offset);
    val >>= (addr & 7);
    mask = (1 << size) - 1;
    /* all bits must be zero to allow the I/O */
    if ((val & mask) != 0) {
    fail:
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, 0);
    }
}

void check_iob_T0(void)
{
    check_io(T0, 1);
}

void check_iow_T0(void)
{
    check_io(T0, 2);
}

void check_iol_T0(void)
{
    check_io(T0, 4);
}

void check_iob_DX(void)
{
    check_io(EDX & 0xffff, 1);
}

void check_iow_DX(void)
{
    check_io(EDX & 0xffff, 2);
}

void check_iol_DX(void)
{
    check_io(EDX & 0xffff, 4);
}

static inline unsigned int get_sp_mask(unsigned int e2)
{
    if (e2 & DESC_B_MASK)
        return 0xffffffff;
    else
        return 0xffff;
}

#ifdef TARGET_X86_64
#define SET_ESP(val, sp_mask)\
do {\
    if ((sp_mask) == 0xffff)\
        ESP = (ESP & ~0xffff) | ((val) & 0xffff);\
    else if ((sp_mask) == 0xffffffffLL)\
        ESP = (uint32_t)(val);\
    else\
        ESP = (val);\
} while (0)
#else
#define SET_ESP(val, sp_mask) ESP = (ESP & ~(sp_mask)) | ((val) & (sp_mask))
#endif

/* XXX: add a is_user flag to have proper security support */
#define PUSHW(ssp, sp, sp_mask, val)\
{\
    sp -= 2;\
    stw_kernel((ssp) + (sp & (sp_mask)), (val));\
}

#define PUSHL(ssp, sp, sp_mask, val)\
{\
    sp -= 4;\
    stl_kernel((ssp) + (sp & (sp_mask)), (val));\
}

#define POPW(ssp, sp, sp_mask, val)\
{\
    val = lduw_kernel((ssp) + (sp & (sp_mask)));\
    sp += 2;\
}

#define POPL(ssp, sp, sp_mask, val)\
{\
    val = (uint32_t)ldl_kernel((ssp) + (sp & (sp_mask)));\
    sp += 4;\
}

/* protected mode interrupt */
static void do_interrupt_protected(int intno, int is_int, int error_code,
                                   unsigned int next_eip, int is_hw)
{
    SegmentCache *dt;
    target_ulong ptr, ssp;
    int type, dpl, selector, ss_dpl, cpl;
    int has_error_code, new_stack, shift;
    uint32_t e1, e2, offset, ss, esp, ss_e1, ss_e2;
    uint32_t old_eip, sp_mask;

    has_error_code = 0;
    if (!is_int && !is_hw) {
        switch(intno) {
        case 8:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 17:
            has_error_code = 1;
            break;
        }
    }
    //if (is_int /*sorav*/ || rr_log)
    if (is_int)
        old_eip = next_eip;
    else
        old_eip = env->eip;

    dt = &env->idt;
    MDBG("intno=0x%x\n", intno);
    if (intno * 8 + 7 > dt->limit) {
        MDBG("calling raise_exception_err(GPF)%c\n", ' ');
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
    }
    ptr = dt->base + intno * 8;
    e1 = ldl_kernel(ptr);
    e2 = ldl_kernel(ptr + 4);
    /* check gate type */
    type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
    switch(type) {
    case 5: /* task gate */
        /* must do that check here to return the correct error code */
        if (!(e2 & DESC_P_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0B_NOSEG);
            raise_exception_err(EXCP0B_NOSEG, intno * 8 + 2);
        }
        switch_tss(intno * 8, e1, e2, SWITCH_TSS_CALL, old_eip);
        if (has_error_code) {
            int type;
            uint32_t mask;
            /* push the error code */
            type = (env->tr.flags >> DESC_TYPE_SHIFT) & 0xf;
            shift = type >> 3;
            if (env->segs[R_SS].flags & DESC_B_MASK)
                mask = 0xffffffff;
            else
                mask = 0xffff;
            esp = (ESP - (2 << shift)) & mask;
            ssp = env->segs[R_SS].base + esp;
            if (shift)
                stl_kernel(ssp, error_code);
            else
                stw_kernel(ssp, error_code);
            SET_ESP(esp, mask);
        }
        return;
    case 6: /* 286 interrupt gate */
    case 7: /* 286 trap gate */
    case 14: /* 386 interrupt gate */
    case 15: /* 386 trap gate */
        break;
    default:
        MDBG("calling raise_exception_err(GPF). type=%d\n", type);
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
        break;
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    /* check privledge if software int */
    if (is_int && dpl < cpl) {
        MDBG("calling raise_exception_err(GPF)%c\n", ' ');
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
    }
    /* check valid bit */
    if (!(e2 & DESC_P_MASK)) {
        MDBG("calling raise_exception_err(GPF)%c\n", ' ');
        raise_exception_err(EXCP0B_NOSEG, intno * 8 + 2);
    }
    selector = e1 >> 16;
    offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
    if ((selector & 0xfffc) == 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, 0);
    }

    if (load_segment(&e1, &e2, selector) != 0) {
        MDBG("calling raise_exception_err(GPF)%c\n", ' ');
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK))) {
        MDBG("calling raise_exception_err(GPF)%c\n", ' ');
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (dpl > cpl) {
        MDBG("calling raise_exception_err(GPF)%c\n", ' ');
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_P_MASK)) {
        MDBG("calling raise_exception_err(GPF)%c\n", ' ');
        raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
    }
    if (!(e2 & DESC_C_MASK) && dpl < cpl) {
        /* to inner priviledge */
        get_ss_esp_from_tss(&ss, &esp, dpl);
        if ((ss & 0xfffc) == 0) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        }
        if ((ss & 3) != dpl) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        }
        if (load_segment(&ss_e1, &ss_e2, ss) != 0) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        }
        ss_dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
        if (ss_dpl != dpl) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        }
        if (!(ss_e2 & DESC_S_MASK) ||
            (ss_e2 & DESC_CS_MASK) ||
            !(ss_e2 & DESC_W_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        }
        if (!(ss_e2 & DESC_P_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
            raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
        }
        new_stack = 1;
        sp_mask = get_sp_mask(ss_e2);
        ssp = get_seg_base(ss_e1, ss_e2);
    } else if ((e2 & DESC_C_MASK) || dpl == cpl) {
        /* to same priviledge */
        if (env->eflags & VM_MASK) {
            MDBG("calling raise_exception_err(GPF)%c\n", ' ');
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        sp_mask = get_sp_mask(env->segs[R_SS].flags);
        ssp = env->segs[R_SS].base;
        esp = ESP;
        dpl = cpl;
    } else {
        MDBG("calling raise_exception_err(GPF)%c\n", ' ');
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        new_stack = 0; /* avoid warning */
        sp_mask = 0; /* avoid warning */
        ssp = 0; /* avoid warning */
        esp = 0; /* avoid warning */
    }

    shift = type >> 3;

#if 0
    /* XXX: check that enough room is available */
    push_size = 6 + (new_stack << 2) + (has_error_code << 1);
    if (env->eflags & VM_MASK)
        push_size += 8;
    push_size <<= shift;
#endif
    if (shift == 1) {
        if (new_stack) {
            if (env->eflags & VM_MASK) {
                PUSHL(ssp, esp, sp_mask, env->segs[R_GS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_FS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_DS].selector);
                PUSHL(ssp, esp, sp_mask, env->segs[R_ES].selector);
            }
            PUSHL(ssp, esp, sp_mask, env->segs[R_SS].selector);
            PUSHL(ssp, esp, sp_mask, ESP);
        }
				uint32_t tmp = compute_eflags();
        PUSHL(ssp, esp, sp_mask, tmp);
        PUSHL(ssp, esp, sp_mask, env->segs[R_CS].selector);
        PUSHL(ssp, esp, sp_mask, old_eip);
				//printf("%s(): pushed 0x%x at 0x%x\n", __func__, old_eip, esp);
        if (has_error_code) {
            PUSHL(ssp, esp, sp_mask, error_code);
        }
    } else {
        if (new_stack) {
            if (env->eflags & VM_MASK) {
                PUSHW(ssp, esp, sp_mask, env->segs[R_GS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_FS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_DS].selector);
                PUSHW(ssp, esp, sp_mask, env->segs[R_ES].selector);
            }
            PUSHW(ssp, esp, sp_mask, env->segs[R_SS].selector);
            PUSHW(ssp, esp, sp_mask, ESP);
        }
        PUSHW(ssp, esp, sp_mask, compute_eflags());
        PUSHW(ssp, esp, sp_mask, env->segs[R_CS].selector);
        PUSHW(ssp, esp, sp_mask, old_eip);
        if (has_error_code) {
            PUSHW(ssp, esp, sp_mask, error_code);
        }
    }
    
    if (new_stack) {
        if (env->eflags & VM_MASK) {
            cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0, 0);
            cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0, 0);
            cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0, 0);
            cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0, 0);
        }
        ss = (ss & ~3) | dpl;
        cpu_x86_load_seg_cache(env, R_SS, ss, 
                               ssp, get_seg_limit(ss_e1, ss_e2), ss_e2);
    }
    SET_ESP(esp, sp_mask);

    selector = (selector & ~3) | dpl;
    cpu_x86_load_seg_cache(env, R_CS, selector, 
                   get_seg_base(e1, e2),
                   get_seg_limit(e1, e2),
                   e2);
    cpu_x86_set_cpl(env, dpl);
    env->eip = offset;

    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);
}

#ifdef TARGET_X86_64

#define PUSHQ(sp, val)\
{\
    sp -= 8;\
    stq_kernel(sp, (val));\
}

#define POPQ(sp, val)\
{\
    val = ldq_kernel(sp);\
    sp += 8;\
}

static inline target_ulong get_rsp_from_tss(int level)
{
    int index;
    
#if 0
    printf("TR: base=" TARGET_FMT_lx " limit=%x\n", 
           env->tr.base, env->tr.limit);
#endif

    if (!(env->tr.flags & DESC_P_MASK))
        cpu_abort(env, "invalid tss");
    index = 8 * level + 4;
    if ((index + 7) > env->tr.limit) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0A_TSS);
        raise_exception_err(EXCP0A_TSS, env->tr.selector & 0xfffc);
    }
    return ldq_kernel(env->tr.base + index);
}

/* 64 bit interrupt */
static void do_interrupt64(int intno, int is_int, int error_code,
                           target_ulong next_eip, int is_hw)
{
    SegmentCache *dt;
    target_ulong ptr;
    int type, dpl, selector, cpl, ist;
    int has_error_code, new_stack;
    uint32_t e1, e2, e3, ss;
    target_ulong old_eip, esp, offset;

    has_error_code = 0;
    if (!is_int && !is_hw) {
        switch(intno) {
        case 8:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 17:
            has_error_code = 1;
            break;
        }
    }
    if (is_int)
        old_eip = next_eip;
    else
        old_eip = env->eip;

    dt = &env->idt;
    if (intno * 16 + 15 > dt->limit) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, intno * 16 + 2);
    }
    ptr = dt->base + intno * 16;
    e1 = ldl_kernel(ptr);
    e2 = ldl_kernel(ptr + 4);
    e3 = ldl_kernel(ptr + 8);
    /* check gate type */
    type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
    switch(type) {
    case 14: /* 386 interrupt gate */
    case 15: /* 386 trap gate */
        break;
    default:
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, intno * 16 + 2);
        break;
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    /* check privledge if software int */
    if (is_int && dpl < cpl) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, intno * 16 + 2);
    }
    /* check valid bit */
    if (!(e2 & DESC_P_MASK)) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0B_NOSEG, intno * 16 + 2);
    }
    selector = e1 >> 16;
    offset = ((target_ulong)e3 << 32) | (e2 & 0xffff0000) | (e1 & 0x0000ffff);
    ist = e2 & 7;
    if ((selector & 0xfffc) == 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, 0);
    }

    if (load_segment(&e1, &e2, selector) != 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK))) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (dpl > cpl) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_P_MASK)) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0B_NOSEG);
        raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
    }
    if (!(e2 & DESC_L_MASK) || (e2 & DESC_B_MASK)) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    if ((!(e2 & DESC_C_MASK) && dpl < cpl) || ist != 0) {
        /* to inner priviledge */
        if (ist != 0)
            esp = get_rsp_from_tss(ist + 3);
        else
            esp = get_rsp_from_tss(dpl);
        esp &= ~0xfLL; /* align stack */
        ss = 0;
        new_stack = 1;
    } else if ((e2 & DESC_C_MASK) || dpl == cpl) {
        /* to same priviledge */
        if (env->eflags & VM_MASK) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        new_stack = 0;
        if (ist != 0)
            esp = get_rsp_from_tss(ist + 3);
        else
            esp = ESP;
        esp &= ~0xfLL; /* align stack */
        dpl = cpl;
    } else {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        new_stack = 0; /* avoid warning */
        esp = 0; /* avoid warning */
    }

    PUSHQ(esp, env->segs[R_SS].selector);
    PUSHQ(esp, ESP);
    PUSHQ(esp, compute_eflags());
    PUSHQ(esp, env->segs[R_CS].selector);
    PUSHQ(esp, old_eip);
    if (has_error_code) {
        PUSHQ(esp, error_code);
    }
    
    if (new_stack) {
        ss = 0 | dpl;
        cpu_x86_load_seg_cache(env, R_SS, ss, 0, 0, 0);
    }
    ESP = esp;

    selector = (selector & ~3) | dpl;
    cpu_x86_load_seg_cache(env, R_CS, selector, 
                   get_seg_base(e1, e2),
                   get_seg_limit(e1, e2),
                   e2);
    cpu_x86_set_cpl(env, dpl);
    env->eip = offset;

    /* interrupt gate clear IF mask */
    if ((type & 1) == 0) {
        env->eflags &= ~IF_MASK;
    }
    env->eflags &= ~(TF_MASK | VM_MASK | RF_MASK | NT_MASK);
}
#endif

void helper_syscall(int next_eip_addend)
{
    int selector;

    if (!(env->efer & MSR_EFER_SCE)) {
        raise_exception_err(EXCP06_ILLOP, 0);
    }
    selector = (env->star >> 32) & 0xffff;
#ifdef TARGET_X86_64
    if (env->hflags & HF_LMA_MASK) {
        int code64;

        ECX = env->eip + next_eip_addend;
        env->regs[11] = compute_eflags();
        
        code64 = env->hflags & HF_CS64_MASK;

        cpu_x86_set_cpl(env, 0);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc, 
                           0, 0xffffffff, 
                               DESC_G_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK | DESC_L_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc, 
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        env->eflags &= ~env->fmask;
        if (code64)
            env->eip = env->lstar;
        else
            env->eip = env->cstar;
    } else 
#endif
    {
        ECX = (uint32_t)(env->eip + next_eip_addend);
        
        cpu_x86_set_cpl(env, 0);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc, 
                           0, 0xffffffff, 
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc, 
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        env->eflags &= ~(IF_MASK | RF_MASK | VM_MASK);
        env->eip = (uint32_t)env->star;
    }
}

void helper_sysret(int dflag)
{
    int cpl, selector;

    if (!(env->efer & MSR_EFER_SCE)) {
        raise_exception_err(EXCP06_ILLOP, 0);
    }
    cpl = env->hflags & HF_CPL_MASK;
    if (!(env->cr[0] & CR0_PE_MASK) || cpl != 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, 0);
    }
    selector = (env->star >> 48) & 0xffff;
#ifdef TARGET_X86_64
    if (env->hflags & HF_LMA_MASK) {
        if (dflag == 2) {
            cpu_x86_load_seg_cache(env, R_CS, (selector + 16) | 3, 
                                   0, 0xffffffff, 
                                   DESC_G_MASK | DESC_P_MASK |
                                   DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                                   DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK | 
                                   DESC_L_MASK);
            env->eip = ECX;
        } else {
            cpu_x86_load_seg_cache(env, R_CS, selector | 3, 
                                   0, 0xffffffff, 
                                   DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                                   DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                                   DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
            env->eip = (uint32_t)ECX;
        }
        cpu_x86_load_seg_cache(env, R_SS, selector + 8, 
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
        load_eflags((uint32_t)(env->regs[11]), TF_MASK | AC_MASK | ID_MASK | 
                    IF_MASK | IOPL_MASK | VM_MASK | RF_MASK | NT_MASK);
        cpu_x86_set_cpl(env, 3);
    } else 
#endif
    {
        cpu_x86_load_seg_cache(env, R_CS, selector | 3, 
                               0, 0xffffffff, 
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        env->eip = (uint32_t)ECX;
        cpu_x86_load_seg_cache(env, R_SS, selector + 8, 
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                               DESC_W_MASK | DESC_A_MASK);
        env->eflags |= IF_MASK;
        cpu_x86_set_cpl(env, 3);
    }
}

/* real mode interrupt */
static void do_interrupt_real(int intno, int is_int, int error_code,
                              unsigned int next_eip)
{
    SegmentCache *dt;
    target_ulong ptr, ssp;
    int selector;
    uint32_t offset, esp;
    uint32_t old_cs, old_eip;

    /* real mode (simpler !) */
    dt = &env->idt;
    if (intno * 4 + 3 > dt->limit) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
    }
    ptr = dt->base + intno * 4;
    offset = lduw_kernel(ptr);
    selector = lduw_kernel(ptr + 2);
    esp = ESP;
    ssp = env->segs[R_SS].base;
    //if (is_int /*sorav*/|| rr_log)
    if (is_int)
        old_eip = next_eip;
    else
        old_eip = env->eip;
    old_cs = env->segs[R_CS].selector;
    /* XXX: use SS segment size ? */
    PUSHW(ssp, esp, 0xffff, compute_eflags());
    PUSHW(ssp, esp, 0xffff, old_cs);
    PUSHW(ssp, esp, 0xffff, old_eip);
    
    /* update processor state */
    ESP = (ESP & ~0xffff) | (esp & 0xffff);
    env->eip = offset;
    env->segs[R_CS].selector = selector;
    env->segs[R_CS].base = (selector << 4);
    env->eflags &= ~(IF_MASK | TF_MASK | AC_MASK | RF_MASK);
}

/* fake user mode interrupt */
void do_interrupt_user(int intno, int is_int, int error_code, 
                       target_ulong next_eip)
{
    SegmentCache *dt;
    target_ulong ptr;
    int dpl, cpl;
    uint32_t e2;

    dt = &env->idt;
    ptr = dt->base + (intno * 8);
    e2 = ldl_kernel(ptr + 4);
    
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    /* check privledge if software int */
    if (is_int && dpl < cpl) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, intno * 8 + 2);
    }

    /* Since we emulate only user space, we cannot do more than
       exiting the emulation with the suitable exception and error
       code */
    if (is_int)
        EIP = next_eip;
}

/*
 * Begin execution of an interruption. is_int is TRUE if coming from
 * the int instruction. next_eip is the EIP value AFTER the interrupt
 * instruction. It is only relevant if is_int is TRUE.  
 */
void do_interrupt(int intno, int is_int, int error_code, 
                  target_ulong next_eip, int is_hw)
{
    //env->n_exec++;
		if (rr_log) {
			//printf("%s(): env->n_exec=%llx\n", __func__, env->n_exec);
			helper_check_breakpoint(is_int?next_eip:env->eip);
		}
		if (intno == EXCP0E_PAGE) {
			env->cr[2] = env->exception_cr2;
		}

    if (next_breakpoint && env->n_exec > next_breakpoint) {
      printf("%s(): n_exec[%#llx] > next_breakpoint[%#llx]\n", __func__,
          env->n_exec, next_breakpoint);
      exit(INSN_COUNT_ERROR_EXITCODE);
    }
    if (loglevel & CPU_LOG_INT) {
        if ((env->cr[0] & CR0_PE_MASK)) {
            static int count;
            fprintf(logfile, "%6d: v=%02x e=%04x i=%d cpl=%d IP=%04x:" TARGET_FMT_lx " pc=" TARGET_FMT_lx " SP=%04x:" TARGET_FMT_lx,
                    count, intno, error_code, is_int,
                    env->hflags & HF_CPL_MASK,
                    env->segs[R_CS].selector, EIP,
                    (int)env->segs[R_CS].base + EIP,
                    env->segs[R_SS].selector, ESP);
            if (intno == 0x0e) {
                fprintf(logfile, " CR2=" TARGET_FMT_lx, env->cr[2]);
            } else {
                fprintf(logfile, " EAX=" TARGET_FMT_lx, EAX);
            }
            fprintf(logfile, "\n");
            cpu_dump_state(env, logfile, fprintf, X86_DUMP_CCOP);
            count++;
        }
    }
    if (env->cr[0] & CR0_PE_MASK) {
#if TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            do_interrupt64(intno, is_int, error_code, next_eip, is_hw);
        } else
#endif
        {
            MDBG("%" PRIx64 ": calling do_interrupt_protected().\n", EIP);
            do_interrupt_protected(intno, is_int, error_code, next_eip, is_hw);
        }
    } else {
        MDBG("%" PRIx64 ": calling do_interrupt_real().\n", EIP);
        do_interrupt_real(intno, is_int, error_code, next_eip);
    }
}

/*
 * Signal an interruption. It is executed in the main CPU loop.
 * is_int is TRUE if coming from the int instruction. next_eip is the
 * EIP value AFTER the interrupt instruction. It is only relevant if
 * is_int is TRUE.  
 */
void raise_interrupt(int intno, int is_int, int error_code, 
                     int next_eip_addend)
{
    env->exception_index = intno;
    env->error_code = error_code;
    env->exception_is_int = is_int;
    env->exception_next_eip = env->eip + next_eip_addend;
    MDBG("Calling cpu_loop_exit()%c\n", '.');
    cpu_loop_exit();
}

/* same as raise_exception_err, but do not restore global registers */
static void raise_exception_err_norestore(int exception_index, int error_code)
{
    env->exception_index = exception_index;
    env->error_code = error_code;
    env->exception_is_int = 0;
    env->exception_next_eip = 0;
    MDBG("Calling longjmp()%c\n", '.');
    longjmp(env->jmp_env, 1);
}

/* shortcuts to generate exceptions */

void (raise_exception_err)(int exception_index, int error_code)
{
    MDBG("Calling raise_interrupt(0x%x)\n", exception_index);
		env->n_exec--;		//because the instruction never executed!
    raise_interrupt(exception_index, 0, error_code, 0);
}

void raise_exception(int exception_index)
{
    MDBG("Calling raise_interrupt(0x%x)\n", exception_index);
		env->n_exec--;		//because the instruction never executed!
    raise_interrupt(exception_index, 0, 0, 0);
}

/* SMM support */


#ifdef TARGET_X86_64
#define SMM_REVISION_ID 0x00020064
#else
#define SMM_REVISION_ID 0x00020000
#endif

void do_smm_enter(void)
{
    target_ulong sm_state;
    SegmentCache *dt;
    int i, offset;

    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "SMM: enter\n");
        cpu_dump_state(env, logfile, fprintf, X86_DUMP_CCOP);
    }

    env->hflags |= HF_SMM_MASK;
    cpu_smm_update(env);

    sm_state = env->smbase + 0x8000;
    
#ifdef TARGET_X86_64
    for(i = 0; i < 6; i++) {
        dt = &env->segs[i];
        offset = 0x7e00 + i * 16;
        stw_phys(sm_state + offset, dt->selector);
        stw_phys(sm_state + offset + 2, (dt->flags >> 8) & 0xf0ff);
        stl_phys(sm_state + offset + 4, dt->limit);
        stq_phys(sm_state + offset + 8, dt->base);
    }

    stq_phys(sm_state + 0x7e68, env->gdt.base);
    stl_phys(sm_state + 0x7e64, env->gdt.limit);

    stw_phys(sm_state + 0x7e70, env->ldt.selector);
    stq_phys(sm_state + 0x7e78, env->ldt.base);
    stl_phys(sm_state + 0x7e74, env->ldt.limit);
    stw_phys(sm_state + 0x7e72, (env->ldt.flags >> 8) & 0xf0ff);
    
    stq_phys(sm_state + 0x7e88, env->idt.base);
    stl_phys(sm_state + 0x7e84, env->idt.limit);

    stw_phys(sm_state + 0x7e90, env->tr.selector);
    stq_phys(sm_state + 0x7e98, env->tr.base);
    stl_phys(sm_state + 0x7e94, env->tr.limit);
    stw_phys(sm_state + 0x7e92, (env->tr.flags >> 8) & 0xf0ff);
    
    stq_phys(sm_state + 0x7ed0, env->efer);

    stq_phys(sm_state + 0x7ff8, EAX);
    stq_phys(sm_state + 0x7ff0, ECX);
    stq_phys(sm_state + 0x7fe8, EDX);
    stq_phys(sm_state + 0x7fe0, EBX);
    stq_phys(sm_state + 0x7fd8, ESP);
    stq_phys(sm_state + 0x7fd0, EBP);
    stq_phys(sm_state + 0x7fc8, ESI);
    stq_phys(sm_state + 0x7fc0, EDI);
    for(i = 8; i < 16; i++) 
        stq_phys(sm_state + 0x7ff8 - i * 8, env->regs[i]);
    stq_phys(sm_state + 0x7f78, env->eip);
    stl_phys(sm_state + 0x7f70, compute_eflags());
    stl_phys(sm_state + 0x7f68, env->dr[6]);
    stl_phys(sm_state + 0x7f60, env->dr[7]);

    stl_phys(sm_state + 0x7f48, env->cr[4]);
    stl_phys(sm_state + 0x7f50, env->cr[3]);
    stl_phys(sm_state + 0x7f58, env->cr[0]);

    stl_phys(sm_state + 0x7efc, SMM_REVISION_ID);
    stl_phys(sm_state + 0x7f00, env->smbase);
#else
    stl_phys(sm_state + 0x7ffc, env->cr[0]);
    stl_phys(sm_state + 0x7ff8, env->cr[3]);
    stl_phys(sm_state + 0x7ff4, compute_eflags());
    stl_phys(sm_state + 0x7ff0, env->eip);
    stl_phys(sm_state + 0x7fec, EDI);
    stl_phys(sm_state + 0x7fe8, ESI);
    stl_phys(sm_state + 0x7fe4, EBP);
    stl_phys(sm_state + 0x7fe0, ESP);
    stl_phys(sm_state + 0x7fdc, EBX);
    stl_phys(sm_state + 0x7fd8, EDX);
    stl_phys(sm_state + 0x7fd4, ECX);
    stl_phys(sm_state + 0x7fd0, EAX);
    stl_phys(sm_state + 0x7fcc, env->dr[6]);
    stl_phys(sm_state + 0x7fc8, env->dr[7]);
    
    stl_phys(sm_state + 0x7fc4, env->tr.selector);
    stl_phys(sm_state + 0x7f64, env->tr.base);
    stl_phys(sm_state + 0x7f60, env->tr.limit);
    stl_phys(sm_state + 0x7f5c, (env->tr.flags >> 8) & 0xf0ff);
    
    stl_phys(sm_state + 0x7fc0, env->ldt.selector);
    stl_phys(sm_state + 0x7f80, env->ldt.base);
    stl_phys(sm_state + 0x7f7c, env->ldt.limit);
    stl_phys(sm_state + 0x7f78, (env->ldt.flags >> 8) & 0xf0ff);
    
    stl_phys(sm_state + 0x7f74, env->gdt.base);
    stl_phys(sm_state + 0x7f70, env->gdt.limit);

    stl_phys(sm_state + 0x7f58, env->idt.base);
    stl_phys(sm_state + 0x7f54, env->idt.limit);

    for(i = 0; i < 6; i++) {
        dt = &env->segs[i];
        if (i < 3)
            offset = 0x7f84 + i * 12;
        else
            offset = 0x7f2c + (i - 3) * 12;
        stl_phys(sm_state + 0x7fa8 + i * 4, dt->selector);
        stl_phys(sm_state + offset + 8, dt->base);
        stl_phys(sm_state + offset + 4, dt->limit);
        stl_phys(sm_state + offset, (dt->flags >> 8) & 0xf0ff);
    }
    stl_phys(sm_state + 0x7f14, env->cr[4]);

    stl_phys(sm_state + 0x7efc, SMM_REVISION_ID);
    stl_phys(sm_state + 0x7ef8, env->smbase);
#endif
    /* init SMM cpu state */

#ifdef TARGET_X86_64
    env->efer = 0;
    env->hflags &= ~HF_LMA_MASK;
#endif
    load_eflags(0, ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->eip = 0x00008000;
    cpu_x86_load_seg_cache(env, R_CS, (env->smbase >> 4) & 0xffff, env->smbase,
                           0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_SS, 0, 0, 0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0xffffffff, 0);
    cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0xffffffff, 0);
    
    cpu_x86_update_cr0(env, 
                       env->cr[0] & ~(CR0_PE_MASK | CR0_EM_MASK | CR0_TS_MASK | CR0_PG_MASK));
    cpu_x86_update_cr4(env, 0);
    env->dr[7] = 0x00000400;
    CC_OP = CC_OP_EFLAGS;
}

void helper_set_mem_monitor(long val_low, long val_high)
{
  static int exception;
  target_ulong val = (uint32_t)val_low + ((target_ulong)val_high << 32);

  memory_monitors[num_memory_monitors] = val;
  prev_memory_monitor_value[num_memory_monitors] = ld_kernel_no_exception(val,
      &exception, 1);
  if (exception) {
    prev_memory_monitor_value[num_memory_monitors] = -1;
  }
  num_memory_monitors++;
}

void helper_reset_mem_monitor(long val_low, long val_high)
{
  int i, j;
  target_ulong val = val_low + ((target_ulong)val_high << 32);

  for (i = 0; i < num_memory_monitors; i++) {
    if ((uint32_t)memory_monitors[i] == (uint32_t)val) {
      break;
    }
  }
  if (i == num_memory_monitors)
    return;
  for (j = i + 1; j < num_memory_monitors; j++) {
    memory_monitors[j - 1] = memory_monitors[j];
  }
  num_memory_monitors--;
}

void
helper_check_stack_pointers(void)
{
  int cpl = env->hflags & HF_CPL_MASK;
  uint32_t esp = ESP;
  uint32_t ebp = EBP;
  uint16_t GDT_SIZE = 8192;
  uint16_t SEL_SIZE = 9*8;
  uint16_t SEL_BASE = GDT_SIZE - SEL_SIZE;
  uint16_t SEL_UDSEG = SEL_BASE + 0x23;
  uint16_t SEL_KDSEG = SEL_BASE + 0x10;
  int exception;
  void **frame;
  uint32_t frame0, frame1;
  int i;

  ASSERT(mdbg_level >= 1);

  ASSERT(cpl == 0 || cpl == 3);
  if (   (uint16_t)env->segs[R_SS].selector != SEL_UDSEG
      && (uint16_t)env->segs[R_SS].selector != SEL_KDSEG) {
    return;
  }
  if (esp > 0xfffff000) {
    return;
  }
  ASSERT(   env->segs[R_SS].selector == SEL_UDSEG
         || env->segs[R_SS].selector == SEL_KDSEG);
  printf("qemu: Kernel thread stack overflow detected for monitor at "
      "%#x: esp[%#x], ebp[%#x] ss[%#hx] (SEL_BASE %#hx, SEL_KDSEG %#hx, "
      "SEL_UDSEG %#hx)\n",
      (uint32_t)env->eip, esp, ebp, (uint16_t)env->segs[R_SS].selector,
      SEL_BASE, SEL_KDSEG, SEL_UDSEG);
  printf("Call stack:\n");
#define vtop(x) ((x)?(void *)((x) - 0xffc00000 + 0x400000):NULL)
  for (frame = vtop(ebp); frame != NULL; frame = vtop(frame0)) {
    cpu_physical_memory_read(frame, (uint8_t *)&frame0, 4);
    cpu_physical_memory_read(&frame[1], (uint8_t *)&frame1, 4);
    //printf("frame=%p, frame0=%#x, frame1=%#x\n", frame, frame0, frame1);
    printf(" %#x", frame1);
  }
  printf("\n");
  printf("Call stack pointers:\n");
  for (frame = vtop(ebp); frame != NULL; frame = vtop(frame0)) {
    cpu_physical_memory_read(frame, (uint8_t *)&frame0, 4);
    printf(" %#x", frame0);
  }
  printf("\n");
  helper_panic();
}

void helper_temporary_check(uint32_t eip)
{
  static uint32_t prev_val = 0;
  uint32_t cur_val;
  cpu_physical_memory_read(0x6d19c0, (uint8_t *)&cur_val, 4);
  if (prev_val != cur_val) {
    printf("0x6d19c0: 0x%x: 0x%x->0x%x\n", eip, prev_val, cur_val);
    prev_val = cur_val;
  }
}

void
helper_check_breakpoint(target_ulong new_eip)
{
	target_ulong eip;
  eip = env->eip;
	env->eip = new_eip;
  if (next_breakpoint != 0 && env->n_exec > next_breakpoint) {
    printf("%s(): env->n_exec[%#llx] > next_breakpoint[%#llx]!\n", __func__,
        env->n_exec, next_breakpoint);
    exit(INSN_COUNT_ERROR_EXITCODE);
  }
  /*
  printf("eip=%x, n_exec=%llx, next_breakpoint=%llx.\n", (uint32_t)env->eip,
      env->n_exec, next_breakpoint);
      */
  while (env->n_exec == next_breakpoint) {
    if (env->next_tag == RR_LOG_TAG_MS) {
      rr_log_loadvm(env, 0);
      //compare_cpu_states(first_cpu, &cpu_next);
    } else if (env->next_tag == RR_LOG_TAG_INTR) {
      int intno;
      rr_log_interrupt(env, &intno);
      raise_interrupt(intno, 0, 0, 0);
    } else if (env->next_tag == RR_LOG_TAG_PANIC) {
      /* end. */
      helper_panic();
    } else if (env->next_tag == RR_LOG_TAG_EXIT) {
      /* end. */
      helper_exit();
    } else {
      helper_check_breakpoint_invalid_tag();
    }
  }
  env->eip = eip;
  env->n_exec++;
}

void
helper_check_breakpoint_invalid_tag(void)
{
  printf("Invalid tag seen. Expected MS or INTR, seen %d. "
      "eip=%x, n_exec=%llx, next_breakpoint=%llx.\n", env->next_tag,
      (uint32_t)env->eip, env->n_exec, next_breakpoint);
  exit(MISMATCH_EXITCODE);
}


void helper_read_mdbg_level(void)
{
  printf("mdbg_level = %d, flushing tbs.", mdbg_level);
  if (mdbg_level == 0) {
    inspect_memory_addr = 0;
    printf(" inspect_memory_addr = 0.");
  }
  printf("\n");
  tb_flush(cpu_single_env);
}

void helper_print_errormsg(void)
{
  printf("Error Msg() invoked!\n");
}

void helper_print_debugmsg(void)
{
  printf("Debug Msg() invoked!\n");
}

FILE *helper_fp = NULL;

void helper_print_eip(target_ulong eip)
{
  if (!helper_fp) {
    helper_fp = fopen("/tmp/log", "w");
  }
  fprintf(helper_fp, "0x%x:\n", eip);
  fflush(helper_fp);
}

void helper_print_state(target_ulong eip)
{
  target_ulong ptr;
  int selector;
  uint32_t offset = 0;
  uint16_t limit;
  uint32_t base = 0, flags;
  static const char *seg_name[6] = { "ES", "CS", "SS", "DS", "FS" ,"GS" };
  int i;
  uint32_t eflags;

  eflags = cc_table[CC_OP].compute_all();
  eflags |= (DF & DF_MASK);
  eflags |= env->eflags & ~(VM_MASK | RF_MASK);

  if (!helper_fp) {
    helper_fp = fopen("/tmp/log", "w");
  }
  fprintf(helper_fp, "0x%x:\n", eip);
  fprintf(helper_fp, "\teax : %08x\n", env->regs[0]);
  fprintf(helper_fp, "\tebx : %08x\n", env->regs[3]);
  fprintf(helper_fp, "\tecx : %08x\n", env->regs[1]);
  fprintf(helper_fp, "\tedx : %08x\n", env->regs[2]);
  fprintf(helper_fp, "\tesi : %08x\n", env->regs[6]);
  fprintf(helper_fp, "\tedi : %08x\n", env->regs[7]);
  fprintf(helper_fp, "\tebp : %08x\n", env->regs[5]);
  fprintf(helper_fp, "\tesp : %08x\n", env->regs[4]);
  fprintf(helper_fp, "\tesp : %08x\n", env->regs[4]);
  fprintf(helper_fp, "\teflags : %08x\n", eflags);

  for (i = 0; i < 6; i++) {
    SegmentCache *sc = &env->segs[i];
    fprintf(helper_fp, "\t%s  : %04x %08x %08x %08x\n", seg_name[i], sc->selector,
        (uint32_t)sc->base, sc->limit, sc->flags);
  }
  fflush(helper_fp);
#if 0
  printf("GDTR=%016" PRIx64 ":%08x\n", env->gdt.base, env->gdt.limit);
  for (i = 0; i < env->gdt.limit/8; i++) {
    ptr = env->gdt.base + 4*i;
    offset = lduw_kernel(ptr);
    selector = lduw_kernel(ptr + 2);
    if (selector <= 8192*8) {
      base = lduw_kernel(env->gdt.base + selector + 2);
      base |= ldub_kernel(env->gdt.base + selector + 4) << 16;
      base |= ldub_kernel(env->gdt.base + selector + 7) << 24;
      limit = lduw_kernel(env->gdt.base + selector);
      flags = ldul_kernel(env->gdt.base + selector + 4);
      flags &= 0x00f0ff00;
    } else {
      base = 0;
      limit = 0;
      flags = 0;
    }

    printf("#0x%x =%04x %08x %08x %08x [offset %04x]\n", i,
        selector, base, limit, flags, offset);
  }

  if (env->cr[0] & CR0_PE_MASK) {
    assert(0);
  } else {
    /* real mode */
    printf("\nIDTR=%016" PRIx64 ":%08x\n", env->idt.base, env->idt.limit);
    for (i = 0; i < env->idt.limit/4; i++) {
      uint16_t old_base = base;
      uint32_t old_offset = offset;

      ptr = env->idt.base + 4*i;
      offset = lduw_kernel(ptr);
      selector = lduw_kernel(ptr + 2);
      if (selector <= 8192*8) {
        base = lduw_kernel(env->gdt.base + selector + 2);
        base |= ldub_kernel(env->gdt.base + selector + 4) << 16;
        base |= ldub_kernel(env->gdt.base + selector + 7) << 24;
        limit = lduw_kernel(env->gdt.base + selector);
        flags = ldul_kernel(env->gdt.base + selector + 4);
        flags &= 0x00f0ff00;
      } else {
        base = 0;
        limit = 0;
        flags = 0;
      }

      if (base != old_base || offset != old_offset) {
        printf("#0x%x =%04x %08x %08x %08x [offset %04x]\n", i,
            selector, base, limit, flags, offset);
      }
    }
  }
#endif
}


void helper_print_memaccess(int seg, int base, int scale, int index,
    target_ulong disp)
{
  if (!helper_fp) {
    helper_fp = fopen("/tmp/log", "w");
  }
  target_ulong addr = 0;
  
  addr += (seg == -1)?0:env->segs[seg].base;
  addr += (base == -1)?0:env->regs[base];
  addr += scale * ((index == -1)?0:env->regs[index]);
  addr += disp;
  //fprintf(helper_fp, "\t[%08" PRIx64 "]: ", addr);
  fprintf(helper_fp, "\t[%08x]: ", addr);
  /*
  fprintf(fp, "\t[%08" PRIx64 " (%08" PRIx64 " + %08" PRIx64 " + %d*%08"
      PRIx64 " + %08" PRIx64 "]:\n", addr,
      (seg == -1)?0:env->segs[seg].base,
      (base == -1)?0:env->regs[base],
      scale,
      (index == -1)?0:env->regs[index],
      disp); 
      */
  /*
  for (i = 0; i < 8; i++) {
    fprintf(fp, " %02hhx", ldub_kernel(addr+ i));
  }
  fprintf(fp, " (%x)\n", ldul_kernel(addr));
  */
  fprintf(helper_fp, "\n");
  fflush(helper_fp);
}

void helper_inspect_memory(long addr_low, long addr_high)
{
  int i;
  target_ulong addr = addr_low + ((target_ulong)addr_high << 32);
  printf("\n%x : ", addr);
  for (i = 0; i < 8; i++) {
    static int exception;
    uint8_t byte;
    byte = ld_kernel_no_exception(addr+i, &exception, 1);
    if (exception) {
      printf(" EX");
    } else {
      printf(" %02x", byte);
    }
  }
  printf("\n");
  inspect_memory_addr = addr;
}

void helper_memory_monitor(long eip)
{
  int i;

  if (!helper_fp) {
    helper_fp = fopen("/tmp/log", "w");
  }
  for (i = 0; i < num_memory_monitors; i++) {
    static int exception;
    uint8_t byte;
    
    byte = ld_kernel_no_exception(memory_monitors[i], &exception, 1);
    if (i == 0) {
      fprintf(helper_fp, "0x%x [%d]:\n", (uint32_t)eip, num_memory_monitors);
    }
    if (!exception) {
      fprintf(helper_fp, "\t%x: %02hhx\n", (uint32_t)memory_monitors[i], byte);
    } else {
      fprintf(helper_fp, "\t%x: EX\n", (uint32_t)memory_monitors[i]);
    }
  }
  fflush(helper_fp);
}

void helper_rsm(void)
{
    target_ulong sm_state;
    int i, offset;
    uint32_t val;

    sm_state = env->smbase + 0x8000;
#ifdef TARGET_X86_64
    env->efer = ldq_phys(sm_state + 0x7ed0);
    if (env->efer & MSR_EFER_LMA)
        env->hflags |= HF_LMA_MASK;
    else
        env->hflags &= ~HF_LMA_MASK;

    for(i = 0; i < 6; i++) {
        offset = 0x7e00 + i * 16;
        cpu_x86_load_seg_cache(env, i, 
                               lduw_phys(sm_state + offset),
                               ldq_phys(sm_state + offset + 8),
                               ldl_phys(sm_state + offset + 4),
                               (lduw_phys(sm_state + offset + 2) & 0xf0ff) << 8);
    }

    env->gdt.base = ldq_phys(sm_state + 0x7e68);
    env->gdt.limit = ldl_phys(sm_state + 0x7e64);

    env->ldt.selector = lduw_phys(sm_state + 0x7e70);
    env->ldt.base = ldq_phys(sm_state + 0x7e78);
    env->ldt.limit = ldl_phys(sm_state + 0x7e74);
    env->ldt.flags = (lduw_phys(sm_state + 0x7e72) & 0xf0ff) << 8;
    
    env->idt.base = ldq_phys(sm_state + 0x7e88);
    env->idt.limit = ldl_phys(sm_state + 0x7e84);

    env->tr.selector = lduw_phys(sm_state + 0x7e90);
    env->tr.base = ldq_phys(sm_state + 0x7e98);
    env->tr.limit = ldl_phys(sm_state + 0x7e94);
    env->tr.flags = (lduw_phys(sm_state + 0x7e92) & 0xf0ff) << 8;
    
    EAX = ldq_phys(sm_state + 0x7ff8);
    ECX = ldq_phys(sm_state + 0x7ff0);
    EDX = ldq_phys(sm_state + 0x7fe8);
    EBX = ldq_phys(sm_state + 0x7fe0);
    ESP = ldq_phys(sm_state + 0x7fd8);
    EBP = ldq_phys(sm_state + 0x7fd0);
    ESI = ldq_phys(sm_state + 0x7fc8);
    EDI = ldq_phys(sm_state + 0x7fc0);
    for(i = 8; i < 16; i++) 
        env->regs[i] = ldq_phys(sm_state + 0x7ff8 - i * 8);
    env->eip = ldq_phys(sm_state + 0x7f78);
    load_eflags(ldl_phys(sm_state + 0x7f70), 
                ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->dr[6] = ldl_phys(sm_state + 0x7f68);
    env->dr[7] = ldl_phys(sm_state + 0x7f60);

    cpu_x86_update_cr4(env, ldl_phys(sm_state + 0x7f48));
    cpu_x86_update_cr3(env, ldl_phys(sm_state + 0x7f50));
    cpu_x86_update_cr0(env, ldl_phys(sm_state + 0x7f58));

    val = ldl_phys(sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = ldl_phys(sm_state + 0x7f00) & ~0x7fff;
    }
#else
    cpu_x86_update_cr0(env, ldl_phys(sm_state + 0x7ffc));
    cpu_x86_update_cr3(env, ldl_phys(sm_state + 0x7ff8));
    load_eflags(ldl_phys(sm_state + 0x7ff4), 
                ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C | DF_MASK));
    env->eip = ldl_phys(sm_state + 0x7ff0);
    EDI = ldl_phys(sm_state + 0x7fec);
    ESI = ldl_phys(sm_state + 0x7fe8);
    EBP = ldl_phys(sm_state + 0x7fe4);
    ESP = ldl_phys(sm_state + 0x7fe0);
    EBX = ldl_phys(sm_state + 0x7fdc);
    EDX = ldl_phys(sm_state + 0x7fd8);
    ECX = ldl_phys(sm_state + 0x7fd4);
    EAX = ldl_phys(sm_state + 0x7fd0);
    env->dr[6] = ldl_phys(sm_state + 0x7fcc);
    env->dr[7] = ldl_phys(sm_state + 0x7fc8);
    
    env->tr.selector = ldl_phys(sm_state + 0x7fc4) & 0xffff;
    env->tr.base = ldl_phys(sm_state + 0x7f64);
    env->tr.limit = ldl_phys(sm_state + 0x7f60);
    env->tr.flags = (ldl_phys(sm_state + 0x7f5c) & 0xf0ff) << 8;
    
    env->ldt.selector = ldl_phys(sm_state + 0x7fc0) & 0xffff;
    env->ldt.base = ldl_phys(sm_state + 0x7f80);
    env->ldt.limit = ldl_phys(sm_state + 0x7f7c);
    env->ldt.flags = (ldl_phys(sm_state + 0x7f78) & 0xf0ff) << 8;
    
    env->gdt.base = ldl_phys(sm_state + 0x7f74);
    env->gdt.limit = ldl_phys(sm_state + 0x7f70);

    env->idt.base = ldl_phys(sm_state + 0x7f58);
    env->idt.limit = ldl_phys(sm_state + 0x7f54);

    for(i = 0; i < 6; i++) {
        if (i < 3)
            offset = 0x7f84 + i * 12;
        else
            offset = 0x7f2c + (i - 3) * 12;
        cpu_x86_load_seg_cache(env, i, 
                               ldl_phys(sm_state + 0x7fa8 + i * 4) & 0xffff,
                               ldl_phys(sm_state + offset + 8),
                               ldl_phys(sm_state + offset + 4),
                               (ldl_phys(sm_state + offset) & 0xf0ff) << 8);
    }
    cpu_x86_update_cr4(env, ldl_phys(sm_state + 0x7f14));

    val = ldl_phys(sm_state + 0x7efc); /* revision ID */
    if (val & 0x20000) {
        env->smbase = ldl_phys(sm_state + 0x7ef8) & ~0x7fff;
    }
#endif
    CC_OP = CC_OP_EFLAGS;
    env->hflags &= ~HF_SMM_MASK;
    cpu_smm_update(env);

    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "SMM: after RSM\n");
        cpu_dump_state(env, logfile, fprintf, X86_DUMP_CCOP);
    }
}



#ifdef BUGGY_GCC_DIV64
/* gcc 2.95.4 on PowerPC does not seem to like using __udivdi3, so we
   call it from another function */
uint32_t div32(uint64_t *q_ptr, uint64_t num, uint32_t den)
{
    *q_ptr = num / den;
    return num % den;
}

int32_t idiv32(int64_t *q_ptr, int64_t num, int32_t den)
{
    *q_ptr = num / den;
    return num % den;
}
#endif

void helper_divl_EAX_T0(void)
{
    unsigned int den, r;
    uint64_t num, q;
    
    num = ((uint32_t)EAX) | ((uint64_t)((uint32_t)EDX) << 32);
    den = T0;
    if (den == 0) {
        raise_exception(EXCP00_DIVZ);
    }
#ifdef BUGGY_GCC_DIV64
    r = div32(&q, num, den);
#else
    q = (num / den);
    r = (num % den);
#endif
    if (q > 0xffffffff)
        raise_exception(EXCP00_DIVZ);
    EAX = (uint32_t)q;
    EDX = (uint32_t)r;
}

void helper_idivl_EAX_T0(void)
{
    int den, r;
    int64_t num, q;
    
    num = ((uint32_t)EAX) | ((uint64_t)((uint32_t)EDX) << 32);
    den = T0;
    if (den == 0) {
        raise_exception(EXCP00_DIVZ);
    }
#ifdef BUGGY_GCC_DIV64
    r = idiv32(&q, num, den);
#else
    q = (num / den);
    r = (num % den);
#endif
    if (q != (int32_t)q)
        raise_exception(EXCP00_DIVZ);
    EAX = (uint32_t)q;
    EDX = (uint32_t)r;
}

void helper_cmpxchg8b(void)
{
    uint64_t d;
    int eflags;

    eflags = cc_table[CC_OP].compute_all();
    d = ldq(A0);
    if (d == (((uint64_t)EDX << 32) | EAX)) {
        stq(A0, ((uint64_t)ECX << 32) | EBX);
        eflags |= CC_Z;
    } else {
        EDX = d >> 32;
        EAX = d;
        eflags &= ~CC_Z;
    }
    CC_SRC = eflags;
}

void helper_cpuid(void)
{
    uint32_t index;
    index = (uint32_t)EAX;
    
    /* test if maximum index reached */
    if (index & 0x80000000) {
        if (index > env->cpuid_xlevel) 
            index = env->cpuid_level;
    } else {
        if (index > env->cpuid_level) 
            index = env->cpuid_level;
    }

    switch(index) {
    case 0:
        EAX = env->cpuid_level;
        EBX = env->cpuid_vendor1;
        EDX = env->cpuid_vendor2;
        ECX = env->cpuid_vendor3;
        break;
    case 1:
        EAX = env->cpuid_version;
        EBX = 8 << 8; /* CLFLUSH size in quad words, Linux wants it. */
        ECX = env->cpuid_ext_features;
        EDX = env->cpuid_features;
        break;
    case 2:
        /* cache info: needed for Pentium Pro compatibility */
        EAX = 0x410601;
        EBX = 0;
        ECX = 0;
        EDX = 0;
        break;
    case 0x80000000:
        EAX = env->cpuid_xlevel;
        EBX = env->cpuid_vendor1;
        EDX = env->cpuid_vendor2;
        ECX = env->cpuid_vendor3;
        break;
    case 0x80000001:
        EAX = env->cpuid_features;
        EBX = 0;
        ECX = 0;
        EDX = env->cpuid_ext2_features;
        break;
    case 0x80000002:
    case 0x80000003:
    case 0x80000004:
        EAX = env->cpuid_model[(index - 0x80000002) * 4 + 0];
        EBX = env->cpuid_model[(index - 0x80000002) * 4 + 1];
        ECX = env->cpuid_model[(index - 0x80000002) * 4 + 2];
        EDX = env->cpuid_model[(index - 0x80000002) * 4 + 3];
        break;
    case 0x80000005:
        /* cache info (L1 cache) */
        EAX = 0x01ff01ff;
        EBX = 0x01ff01ff;
        ECX = 0x40020140;
        EDX = 0x40020140;
        break;
    case 0x80000006:
        /* cache info (L2 cache) */
        EAX = 0;
        EBX = 0x42004200;
        ECX = 0x02008140;
        EDX = 0;
        break;
    case 0x80000008:
        /* virtual & phys address size in low 2 bytes. */
        EAX = 0x00003028;
        EBX = 0;
        ECX = 0;
        EDX = 0;
        break;
    default:
        /* reserved values: zero */
        EAX = 0;
        EBX = 0;
        ECX = 0;
        EDX = 0;
        break;
    }
}

void helper_enter_level(int level, int data32)
{
    target_ulong ssp;
    uint32_t esp_mask, esp, ebp;

    esp_mask = get_sp_mask(env->segs[R_SS].flags);
    ssp = env->segs[R_SS].base;
    ebp = EBP;
    esp = ESP;
    if (data32) {
        /* 32 bit */
        esp -= 4;
        while (--level) {
            esp -= 4;
            ebp -= 4;
            stl(ssp + (esp & esp_mask), ldl(ssp + (ebp & esp_mask)));
        }
        esp -= 4;
        stl(ssp + (esp & esp_mask), T1);
    } else {
        /* 16 bit */
        esp -= 2;
        while (--level) {
            esp -= 2;
            ebp -= 2;
            stw(ssp + (esp & esp_mask), lduw(ssp + (ebp & esp_mask)));
        }
        esp -= 2;
        stw(ssp + (esp & esp_mask), T1);
    }
}

#ifdef TARGET_X86_64
void helper_enter64_level(int level, int data64)
{
    target_ulong esp, ebp;
    ebp = EBP;
    esp = ESP;

    if (data64) {
        /* 64 bit */
        esp -= 8;
        while (--level) {
            esp -= 8;
            ebp -= 8;
            stq(esp, ldq(ebp));
        }
        esp -= 8;
        stq(esp, T1);
    } else {
        /* 16 bit */
        esp -= 2;
        while (--level) {
            esp -= 2;
            ebp -= 2;
            stw(esp, lduw(ebp));
        }
        esp -= 2;
        stw(esp, T1);
    }
}
#endif

void helper_lldt_T0(void)
{
    int selector;
    SegmentCache *dt;
    uint32_t e1, e2;
    int index, entry_limit;
    target_ulong ptr;
    
    selector = T0 & 0xffff;
    if ((selector & 0xfffc) == 0) {
        /* XXX: NULL selector case: invalid LDT */
        env->ldt.base = 0;
        env->ldt.limit = 0;
    } else {
        if (selector & 0x4) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        dt = &env->gdt;
        index = selector & ~7;
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK)
            entry_limit = 15;
        else
#endif            
            entry_limit = 7;
        if ((index + entry_limit) > dt->limit) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        ptr = dt->base + index;
        e1 = ldl_kernel(ptr);
        e2 = ldl_kernel(ptr + 4);
        if ((e2 & DESC_S_MASK) || ((e2 >> DESC_TYPE_SHIFT) & 0xf) != 2) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0B_NOSEG);
            raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint32_t e3;
            e3 = ldl_kernel(ptr + 8);
            load_seg_cache_raw_dt(&env->ldt, e1, e2);
            env->ldt.base |= (target_ulong)e3 << 32;
        } else
#endif
        {
            load_seg_cache_raw_dt(&env->ldt, e1, e2);
        }
    }
    env->ldt.selector = selector;
}

void helper_ltr_T0(void)
{
    int selector;
    SegmentCache *dt;
    uint32_t e1, e2;
    int index, type, entry_limit;
    target_ulong ptr;
    
    selector = T0 & 0xffff;
    if ((selector & 0xfffc) == 0) {
        /* NULL selector case: invalid TR */
        env->tr.base = 0;
        env->tr.limit = 0;
        env->tr.flags = 0;
    } else {
        if (selector & 0x4) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        dt = &env->gdt;
        index = selector & ~7;
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK)
            entry_limit = 15;
        else
#endif            
            entry_limit = 7;
        if ((index + entry_limit) > dt->limit) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        ptr = dt->base + index;
        e1 = ldl_kernel(ptr);
        e2 = ldl_kernel(ptr + 4);
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        if ((e2 & DESC_S_MASK) || 
            (type != 1 && type != 9)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0B_NOSEG);
            raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint32_t e3;
            e3 = ldl_kernel(ptr + 8);
            load_seg_cache_raw_dt(&env->tr, e1, e2);
            env->tr.base |= (target_ulong)e3 << 32;
        } else 
#endif
        {
            load_seg_cache_raw_dt(&env->tr, e1, e2);
        }
        e2 |= DESC_TSS_BUSY_MASK;
        stl_kernel(ptr + 4, e2);
    }
    env->tr.selector = selector;
}

/* only works if protected mode and not VM86. seg_reg must be != R_CS */
void load_seg(int seg_reg, int selector)
{
    uint32_t e1, e2;
    int cpl, dpl, rpl;
    SegmentCache *dt;
    int index;
    target_ulong ptr;
//#undef MDBG
//#define MDBG(f,args...) printf("%s() %d: " f, __func__, __LINE__, ##args)

    selector &= 0xffff;
    cpl = env->hflags & HF_CPL_MASK;
    if ((selector & 0xfffc) == 0) {
        /* null selector case */
        if (seg_reg == R_SS
#ifdef TARGET_X86_64
            && (!(env->hflags & HF_CS64_MASK) || cpl == 3)
#endif
            ) {
            printf("eip=%p, selector=%#x, Calling raise_exception_err(0x%x).\n", env->eip, (uint32_t)selector, EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, 0);
        }
        cpu_x86_load_seg_cache(env, seg_reg, selector, 0, 0, 0);
    } else {
        
        if (selector & 0x4)
            dt = &env->ldt;
        else
            dt = &env->gdt;
        index = selector & ~7;
        if ((index + 7) > dt->limit) {
            printf("%d: %p: Calling raise_exception_err(0x%x).\n", __LINE__, env->eip, EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        ptr = dt->base + index;
        MDBG("ptr=0x%x,base=0x%x,index=0x%x\n", (uint32_t)ptr, (uint32_t)dt->base, (uint32_t)index);
        e1 = ldl_kernel(ptr);
        e2 = ldl_kernel(ptr + 4);
        
        if (!(e2 & DESC_S_MASK)) {
          /*
            printf("Calling raise_exception_err(0x%x). ptr=0x%x. dt->base=0x%x, "
                "index=0x%x\n", EXCP0D_GPF, (uint32_t)ptr, (uint32_t)dt->base,
                (uint32_t)index);
                */
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        rpl = selector & 3;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        //MDBG("rpl=%#x, dpl=%#x\n", rpl, dpl);
        if (seg_reg == R_SS) {
            /* must be writable segment */
            if ((e2 & DESC_CS_MASK) || !(e2 & DESC_W_MASK)) {
							  printf("%d: %p: Calling raise_exception_err(0x%x).\n", __LINE__, env->eip, EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
            }
            if (rpl != cpl || dpl != cpl) {
                printf("Calling raise_exception_err(0x%x). cpl=%d, rpl=%d, "
                    "dpl=%d.\n", EXCP0D_GPF, cpl, rpl, dpl);
                raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
            }
        } else {
            /* must be readable segment */
            if ((e2 & (DESC_CS_MASK | DESC_R_MASK)) == DESC_CS_MASK) {
							  printf("%d: %p: Calling raise_exception_err(0x%x).\n", __LINE__, env->eip, EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
            }
            
            if (!(e2 & DESC_CS_MASK) || !(e2 & DESC_C_MASK)) {
                /* if not conforming code, test rights */
                if (dpl < cpl || dpl < rpl) {
							      printf("%d: %p: selector=%x, cpl=%d, dpl=%d, rpl=%d, Calling raise_exception_err(0x%x).\n", __LINE__, env->eip, (uint32_t)selector, (int)cpl, (int)dpl, (int)rpl, EXCP0D_GPF);
                    raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
                }
            }
        }

        if (!(e2 & DESC_P_MASK)) {
            if (seg_reg == R_SS)
                raise_exception_err(EXCP0C_STACK, selector & 0xfffc);
            else
                raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
        }

        /* set the access bit if not already set */
        if (!(e2 & DESC_A_MASK)) {
            e2 |= DESC_A_MASK;
            stl_kernel(ptr + 4, e2);
        }

        cpu_x86_load_seg_cache(env, seg_reg, selector, 
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
#if 0
        fprintf(logfile, "load_seg: sel=0x%04x base=0x%08lx limit=0x%08lx flags=%08x\n", 
                selector, (unsigned long)sc->base, sc->limit, sc->flags);
#endif
    }
}

/* protected mode jump */
void helper_ljmp_protected_T0_T1(int next_eip_addend)
{
    int new_cs, gate_cs, type;
    uint32_t e1, e2, cpl, dpl, rpl, limit;
    target_ulong new_eip, next_eip;

    new_cs = T0;
    new_eip = T1;
    if ((new_cs & 0xfffc) == 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, 0);
    }
    if (load_segment(&e1, &e2, new_cs) != 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    }
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_C_MASK) {
            /* conforming code segment */
            if (dpl > cpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
            if (dpl != cpl) {
                MDBG("e2=0x%x, e1=0x%x, dpl=%d, cpl=%d. Calling "
                    "raise_exception_err(0x%x).\n", e2, e1, dpl, cpl,
                    EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
        }
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, new_cs & 0xfffc);
        limit = get_seg_limit(e1, e2);
        if (new_eip > limit && 
            !(env->hflags & HF_LMA_MASK) && !(e2 & DESC_L_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
        cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                       get_seg_base(e1, e2), limit, e2);
        EIP = new_eip;
    } else {
        /* jump to call or task gate */
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        rpl = new_cs & 3;
        cpl = env->hflags & HF_CPL_MASK;
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        switch(type) {
        case 1: /* 286 TSS */
        case 9: /* 386 TSS */
        case 5: /* task gate */
            if (dpl < cpl || dpl < rpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
            next_eip = env->eip + next_eip_addend;
            switch_tss(new_cs, e1, e2, SWITCH_TSS_JMP, next_eip);
            CC_OP = CC_OP_EFLAGS;
            break;
        case 4: /* 286 call gate */
        case 12: /* 386 call gate */
            if ((dpl < cpl) || (dpl < rpl)) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
            if (!(e2 & DESC_P_MASK))
                raise_exception_err(EXCP0B_NOSEG, new_cs & 0xfffc);
            gate_cs = e1 >> 16;
            new_eip = (e1 & 0xffff);
            if (type == 12)
                new_eip |= (e2 & 0xffff0000);
            if (load_segment(&e1, &e2, gate_cs) != 0) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, gate_cs & 0xfffc);
            }
            dpl = (e2 >> DESC_DPL_SHIFT) & 3;
            /* must be code segment */
            if (((e2 & (DESC_S_MASK | DESC_CS_MASK)) != 
                 (DESC_S_MASK | DESC_CS_MASK))) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, gate_cs & 0xfffc);
            }
            if (((e2 & DESC_C_MASK) && (dpl > cpl)) || 
                (!(e2 & DESC_C_MASK) && (dpl != cpl))) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, gate_cs & 0xfffc);
            }
            if (!(e2 & DESC_P_MASK)) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, gate_cs & 0xfffc);
            }
            limit = get_seg_limit(e1, e2);
            if (new_eip > limit) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, 0);
            }
            cpu_x86_load_seg_cache(env, R_CS, (gate_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), limit, e2);
            EIP = new_eip;
            break;
        default:
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            break;
        }
    }
}

/* real mode call */
void helper_lcall_real_T0_T1(int shift, int next_eip)
{
    int new_cs, new_eip;
    uint32_t esp, esp_mask;
    target_ulong ssp;

    new_cs = T0;
    new_eip = T1;
    esp = ESP;
    esp_mask = get_sp_mask(env->segs[R_SS].flags);
    ssp = env->segs[R_SS].base;
    if (shift) {
        PUSHL(ssp, esp, esp_mask, env->segs[R_CS].selector);
        PUSHL(ssp, esp, esp_mask, next_eip);
    } else {
        PUSHW(ssp, esp, esp_mask, env->segs[R_CS].selector);
        PUSHW(ssp, esp, esp_mask, next_eip);
    }

    SET_ESP(esp, esp_mask);
    env->eip = new_eip;
    env->segs[R_CS].selector = new_cs;
    env->segs[R_CS].base = (new_cs << 4);
}

/* protected mode call */
void helper_lcall_protected_T0_T1(int shift, int next_eip_addend)
{
    int new_cs, new_stack, i;
    uint32_t e1, e2, cpl, dpl, rpl, selector, offset, param_count;
    uint32_t ss, ss_e1, ss_e2, sp, type, ss_dpl, sp_mask;
    uint32_t val, limit, old_sp_mask;
    target_ulong ssp, old_ssp, next_eip, new_eip;
    
    new_cs = T0;
    new_eip = T1;
    next_eip = env->eip + next_eip_addend;
#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_PCALL) {
        fprintf(logfile, "lcall %04x:%08x s=%d\n",
                new_cs, (uint32_t)new_eip, shift);
        cpu_dump_state(env, logfile, fprintf, X86_DUMP_CCOP);
    }
#endif
    if ((new_cs & 0xfffc) == 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, 0);
    }
    if (load_segment(&e1, &e2, new_cs) != 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    }
    cpl = env->hflags & HF_CPL_MASK;
#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_PCALL) {
        fprintf(logfile, "desc=%08x:%08x\n", e1, e2);
    }
#endif
    if (e2 & DESC_S_MASK) {
        if (!(e2 & DESC_CS_MASK)) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (e2 & DESC_C_MASK) {
            /* conforming code segment */
            if (dpl > cpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
        } else {
            /* non conforming code segment */
            rpl = new_cs & 3;
            if (rpl > cpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
            if (dpl != cpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
        }
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, new_cs & 0xfffc);

#ifdef TARGET_X86_64
        /* XXX: check 16/32 bit cases in long mode */
        if (shift == 2) {
            target_ulong rsp;
            /* 64 bit case */
            rsp = ESP;
            PUSHQ(rsp, env->segs[R_CS].selector);
            PUSHQ(rsp, next_eip);
            /* from this point, not restartable */
            ESP = rsp;
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), 
                                   get_seg_limit(e1, e2), e2);
            EIP = new_eip;
        } else 
#endif
        {
            sp = ESP;
            sp_mask = get_sp_mask(env->segs[R_SS].flags);
            ssp = env->segs[R_SS].base;
            if (shift) {
                PUSHL(ssp, sp, sp_mask, env->segs[R_CS].selector);
                PUSHL(ssp, sp, sp_mask, next_eip);
            } else {
                PUSHW(ssp, sp, sp_mask, env->segs[R_CS].selector);
                PUSHW(ssp, sp, sp_mask, next_eip);
            }
            
            limit = get_seg_limit(e1, e2);
            if (new_eip > limit) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
            /* from this point, not restartable */
            SET_ESP(sp, sp_mask);
            cpu_x86_load_seg_cache(env, R_CS, (new_cs & 0xfffc) | cpl,
                                   get_seg_base(e1, e2), limit, e2);
            EIP = new_eip;
        }
    } else {
        /* check gate type */
        type = (e2 >> DESC_TYPE_SHIFT) & 0x1f;
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        rpl = new_cs & 3;
        switch(type) {
        case 1: /* available 286 TSS */
        case 9: /* available 386 TSS */
        case 5: /* task gate */
            if (dpl < cpl || dpl < rpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            }
            switch_tss(new_cs, e1, e2, SWITCH_TSS_CALL, next_eip);
            CC_OP = CC_OP_EFLAGS;
            return;
        case 4: /* 286 call gate */
        case 12: /* 386 call gate */
            break;
        default:
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
            break;
        }
        shift = type >> 3;

        if (dpl < cpl || dpl < rpl) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
        /* check valid bit */
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG,  new_cs & 0xfffc);
        selector = e1 >> 16;
        offset = (e2 & 0xffff0000) | (e1 & 0x0000ffff);
        param_count = e2 & 0x1f;
        if ((selector & 0xfffc) == 0) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, 0);
        }

        if (load_segment(&e1, &e2, selector) != 0) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        if (!(e2 & DESC_S_MASK) || !(e2 & (DESC_CS_MASK))) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        if (dpl > cpl) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
        }
        if (!(e2 & DESC_P_MASK))
            raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);

        if (!(e2 & DESC_C_MASK) && dpl < cpl) {
            /* to inner priviledge */
            get_ss_esp_from_tss(&ss, &sp, dpl);
#ifdef DEBUG_PCALL
            if (loglevel & CPU_LOG_PCALL)
                fprintf(logfile, "new ss:esp=%04x:%08x param_count=%d ESP=" TARGET_FMT_lx "\n", 
                        ss, sp, param_count, ESP);
#endif
            if ((ss & 0xfffc) == 0)
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            if ((ss & 3) != dpl)
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            if (load_segment(&ss_e1, &ss_e2, ss) != 0)
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            ss_dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
            if (ss_dpl != dpl)
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            if (!(ss_e2 & DESC_S_MASK) ||
                (ss_e2 & DESC_CS_MASK) ||
                !(ss_e2 & DESC_W_MASK))
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            if (!(ss_e2 & DESC_P_MASK))
                raise_exception_err(EXCP0A_TSS, ss & 0xfffc);
            
            //            push_size = ((param_count * 2) + 8) << shift;

            old_sp_mask = get_sp_mask(env->segs[R_SS].flags);
            old_ssp = env->segs[R_SS].base;
            
            sp_mask = get_sp_mask(ss_e2);
            ssp = get_seg_base(ss_e1, ss_e2);
            if (shift) {
                PUSHL(ssp, sp, sp_mask, env->segs[R_SS].selector);
                PUSHL(ssp, sp, sp_mask, ESP);
                for(i = param_count - 1; i >= 0; i--) {
                    val = ldl_kernel(old_ssp + ((ESP + i * 4) & old_sp_mask));
                    PUSHL(ssp, sp, sp_mask, val);
                }
            } else {
                PUSHW(ssp, sp, sp_mask, env->segs[R_SS].selector);
                PUSHW(ssp, sp, sp_mask, ESP);
                for(i = param_count - 1; i >= 0; i--) {
                    val = lduw_kernel(old_ssp + ((ESP + i * 2) & old_sp_mask));
                    PUSHW(ssp, sp, sp_mask, val);
                }
            }
            new_stack = 1;
        } else {
            /* to same priviledge */
            sp = ESP;
            sp_mask = get_sp_mask(env->segs[R_SS].flags);
            ssp = env->segs[R_SS].base;
            //            push_size = (4 << shift);
            new_stack = 0;
        }

        if (shift) {
            PUSHL(ssp, sp, sp_mask, env->segs[R_CS].selector);
            PUSHL(ssp, sp, sp_mask, next_eip);
        } else {
            PUSHW(ssp, sp, sp_mask, env->segs[R_CS].selector);
            PUSHW(ssp, sp, sp_mask, next_eip);
        }

        /* from this point, not restartable */

        if (new_stack) {
            ss = (ss & ~3) | dpl;
            cpu_x86_load_seg_cache(env, R_SS, ss, 
                                   ssp,
                                   get_seg_limit(ss_e1, ss_e2),
                                   ss_e2);
        }

        selector = (selector & ~3) | dpl;
        cpu_x86_load_seg_cache(env, R_CS, selector, 
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
        cpu_x86_set_cpl(env, dpl);
        SET_ESP(sp, sp_mask);
        EIP = offset;
    }
}

/* real and vm86 mode iret */
void helper_iret_real(int shift)
{
    uint32_t sp, new_cs, new_eip, new_eflags, sp_mask;
    target_ulong ssp;
    int eflags_mask;

    sp_mask = 0xffff; /* XXXX: use SS segment size ? */
    sp = ESP;
    ssp = env->segs[R_SS].base;
    if (shift == 1) {
        /* 32 bits */
        POPL(ssp, sp, sp_mask, new_eip);
        POPL(ssp, sp, sp_mask, new_cs);
        new_cs &= 0xffff;
        POPL(ssp, sp, sp_mask, new_eflags);
    } else {
        /* 16 bits */
        POPW(ssp, sp, sp_mask, new_eip);
        POPW(ssp, sp, sp_mask, new_cs);
        POPW(ssp, sp, sp_mask, new_eflags);
    }
    ESP = (ESP & ~sp_mask) | (sp & sp_mask);
    load_seg_vm(R_CS, new_cs);
    env->eip = new_eip;
    if (env->eflags & VM_MASK)
        eflags_mask = TF_MASK | AC_MASK | ID_MASK | IF_MASK | RF_MASK | NT_MASK;
    else
        eflags_mask = TF_MASK | AC_MASK | ID_MASK | IF_MASK | IOPL_MASK | RF_MASK | NT_MASK;
    if (shift == 0)
        eflags_mask &= 0xffff;
    load_eflags(new_eflags, eflags_mask);
}

static inline void validate_seg(int seg_reg, int cpl)
{
    int dpl;
    uint32_t e2;

    /* XXX: on x86_64, we do not want to nullify FS and GS because
       they may still contain a valid base. I would be interested to
       know how a real x86_64 CPU behaves */
    if ((seg_reg == R_FS || seg_reg == R_GS) && 
        (env->segs[seg_reg].selector & 0xfffc) == 0)
        return;

    e2 = env->segs[seg_reg].flags;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (!(e2 & DESC_CS_MASK) || !(e2 & DESC_C_MASK)) {
        /* data or non conforming code segment */
        if (dpl < cpl) {
            cpu_x86_load_seg_cache(env, seg_reg, 0, 0, 0, 0);
        }
    }
}

/* protected mode iret */
static inline void helper_ret_protected(int shift, int is_iret, int addend)
{
    uint32_t new_cs, new_eflags, new_ss;
    uint32_t new_es, new_ds, new_fs, new_gs;
    uint32_t e1, e2, ss_e1, ss_e2;
    int cpl, dpl, rpl, eflags_mask, iopl;
    target_ulong ssp, sp, new_eip, new_esp, sp_mask;
    
#ifdef TARGET_X86_64
    if (shift == 2)
        sp_mask = -1;
    else
#endif
        sp_mask = get_sp_mask(env->segs[R_SS].flags);
    sp = ESP;
    ssp = env->segs[R_SS].base;
    new_eflags = 0; /* avoid warning */
#ifdef TARGET_X86_64
    if (shift == 2) {
        POPQ(sp, new_eip);
        POPQ(sp, new_cs);
        new_cs &= 0xffff;
        if (is_iret) {
            POPQ(sp, new_eflags);
        }
    } else
#endif
    if (shift == 1) {
        /* 32 bits */
        POPL(ssp, sp, sp_mask, new_eip);
        POPL(ssp, sp, sp_mask, new_cs);
        new_cs &= 0xffff;
        if (is_iret) {
            POPL(ssp, sp, sp_mask, new_eflags);
            if (new_eflags & VM_MASK)
                goto return_to_vm86;
        }
    } else {
        /* 16 bits */
        POPW(ssp, sp, sp_mask, new_eip);
        POPW(ssp, sp, sp_mask, new_cs);
        if (is_iret)
            POPW(ssp, sp, sp_mask, new_eflags);
    }
#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_PCALL) {
        fprintf(logfile, "lret new %04x:" TARGET_FMT_lx " s=%d addend=0x%x\n",
                new_cs, new_eip, shift, addend);
        cpu_dump_state(env, logfile, fprintf, X86_DUMP_CCOP);
    }
#endif
    if ((new_cs & 0xfffc) == 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    }
    if (load_segment(&e1, &e2, new_cs) != 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    }
    if (!(e2 & DESC_S_MASK) ||
        !(e2 & DESC_CS_MASK)) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    }
    cpl = env->hflags & HF_CPL_MASK;
    rpl = new_cs & 3; 
    if (rpl < cpl) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    }
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    if (e2 & DESC_C_MASK) {
        if (dpl > rpl) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
    } else {
        if (dpl != rpl) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
    }
    if (!(e2 & DESC_P_MASK))
        raise_exception_err(EXCP0B_NOSEG, new_cs & 0xfffc);
    
    sp += addend;
    if (rpl == cpl && (!(env->hflags & HF_CS64_MASK) || 
                       ((env->hflags & HF_CS64_MASK) && !is_iret))) {
        /* return to same priledge level */
        cpu_x86_load_seg_cache(env, R_CS, new_cs, 
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
    } else {
        /* return to different priviledge level */
#ifdef TARGET_X86_64
        if (shift == 2) {
            POPQ(sp, new_esp);
            POPQ(sp, new_ss);
            new_ss &= 0xffff;
        } else
#endif
        if (shift == 1) {
            /* 32 bits */
            POPL(ssp, sp, sp_mask, new_esp);
            POPL(ssp, sp, sp_mask, new_ss);
            new_ss &= 0xffff;
        } else {
            /* 16 bits */
            POPW(ssp, sp, sp_mask, new_esp);
            POPW(ssp, sp, sp_mask, new_ss);
        }
#ifdef DEBUG_PCALL
        if (loglevel & CPU_LOG_PCALL) {
            fprintf(logfile, "new ss:esp=%04x:" TARGET_FMT_lx "\n",
                    new_ss, new_esp);
        }
#endif
        if ((new_ss & 0xfffc) == 0) {
#ifdef TARGET_X86_64
            /* NULL ss is allowed in long mode if cpl != 3*/
            /* XXX: test CS64 ? */
            if ((env->hflags & HF_LMA_MASK) && rpl != 3) {
                cpu_x86_load_seg_cache(env, R_SS, new_ss, 
                                       0, 0xffffffff,
                                       DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                                       DESC_S_MASK | (rpl << DESC_DPL_SHIFT) |
                                       DESC_W_MASK | DESC_A_MASK);
                ss_e2 = DESC_B_MASK; /* XXX: should not be needed ? */
            } else 
#endif
            {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, 0);
            }
        } else {
            if ((new_ss & 3) != rpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_ss & 0xfffc);
            }
            if (load_segment(&ss_e1, &ss_e2, new_ss) != 0) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_ss & 0xfffc);
            }
            if (!(ss_e2 & DESC_S_MASK) ||
                (ss_e2 & DESC_CS_MASK) ||
                !(ss_e2 & DESC_W_MASK)) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_ss & 0xfffc);
            }
            dpl = (ss_e2 >> DESC_DPL_SHIFT) & 3;
            if (dpl != rpl) {
                MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
                raise_exception_err(EXCP0D_GPF, new_ss & 0xfffc);
            }
            if (!(ss_e2 & DESC_P_MASK))
                raise_exception_err(EXCP0B_NOSEG, new_ss & 0xfffc);
            cpu_x86_load_seg_cache(env, R_SS, new_ss, 
                                   get_seg_base(ss_e1, ss_e2),
                                   get_seg_limit(ss_e1, ss_e2),
                                   ss_e2);
        }

        cpu_x86_load_seg_cache(env, R_CS, new_cs, 
                       get_seg_base(e1, e2),
                       get_seg_limit(e1, e2),
                       e2);
        cpu_x86_set_cpl(env, rpl);
        sp = new_esp;
#ifdef TARGET_X86_64
        if (env->hflags & HF_CS64_MASK)
            sp_mask = -1;
        else
#endif
            sp_mask = get_sp_mask(ss_e2);

        /* validate data segments */
        validate_seg(R_ES, rpl);
        validate_seg(R_DS, rpl);
        validate_seg(R_FS, rpl);
        validate_seg(R_GS, rpl);

        sp += addend;
    }
    SET_ESP(sp, sp_mask);
    env->eip = new_eip;
    if (is_iret) {
        /* NOTE: 'cpl' is the _old_ CPL */
        eflags_mask = TF_MASK | AC_MASK | ID_MASK | RF_MASK | NT_MASK;
        if (cpl == 0)
            eflags_mask |= IOPL_MASK;
        iopl = (env->eflags >> IOPL_SHIFT) & 3;
        if (cpl <= iopl)
            eflags_mask |= IF_MASK;
        if (shift == 0)
            eflags_mask &= 0xffff;
        load_eflags(new_eflags, eflags_mask);
    }
    return;

 return_to_vm86:
    POPL(ssp, sp, sp_mask, new_esp);
    POPL(ssp, sp, sp_mask, new_ss);
    POPL(ssp, sp, sp_mask, new_es);
    POPL(ssp, sp, sp_mask, new_ds);
    POPL(ssp, sp, sp_mask, new_fs);
    POPL(ssp, sp, sp_mask, new_gs);
    
    /* modify processor state */
    load_eflags(new_eflags, TF_MASK | AC_MASK | ID_MASK | 
                IF_MASK | IOPL_MASK | VM_MASK | NT_MASK | VIF_MASK | VIP_MASK);
    load_seg_vm(R_CS, new_cs & 0xffff);
    cpu_x86_set_cpl(env, 3);
    load_seg_vm(R_SS, new_ss & 0xffff);
    load_seg_vm(R_ES, new_es & 0xffff);
    load_seg_vm(R_DS, new_ds & 0xffff);
    load_seg_vm(R_FS, new_fs & 0xffff);
    load_seg_vm(R_GS, new_gs & 0xffff);

    env->eip = new_eip & 0xffff;
    ESP = new_esp;
}

void helper_iret_protected(int shift, int next_eip)
{
    int tss_selector, type;
    uint32_t e1, e2;
    
    /* specific case for TSS */
    if (env->eflags & NT_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
            raise_exception_err(EXCP0D_GPF, 0);
        }
#endif
        tss_selector = lduw_kernel(env->tr.base + 0);
        if (tss_selector & 4)
            raise_exception_err(EXCP0A_TSS, tss_selector & 0xfffc);
        if (load_segment(&e1, &e2, tss_selector) != 0)
            raise_exception_err(EXCP0A_TSS, tss_selector & 0xfffc);
        type = (e2 >> DESC_TYPE_SHIFT) & 0x17;
        /* NOTE: we check both segment and busy TSS */
        if (type != 3)
            raise_exception_err(EXCP0A_TSS, tss_selector & 0xfffc);
        switch_tss(tss_selector, e1, e2, SWITCH_TSS_IRET, next_eip);
    } else {
        helper_ret_protected(shift, 1, 0);
    }
}

void helper_lret_protected(int shift, int addend)
{
    helper_ret_protected(shift, 0, addend);
}

void helper_sysenter(void)
{
    if (env->sysenter_cs == 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, 0);
    }
    env->eflags &= ~(VM_MASK | IF_MASK | RF_MASK);
    cpu_x86_set_cpl(env, 0);
    cpu_x86_load_seg_cache(env, R_CS, env->sysenter_cs & 0xfffc, 
                           0, 0xffffffff, 
                           DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                           DESC_S_MASK |
                           DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_SS, (env->sysenter_cs + 8) & 0xfffc, 
                           0, 0xffffffff,
                           DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                           DESC_S_MASK |
                           DESC_W_MASK | DESC_A_MASK);
    ESP = env->sysenter_esp;
    EIP = env->sysenter_eip;
}

void helper_sysexit(void)
{
    int cpl;

    cpl = env->hflags & HF_CPL_MASK;
    if (env->sysenter_cs == 0 || cpl != 0) {
        MDBG("Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
        raise_exception_err(EXCP0D_GPF, 0);
    }
    cpu_x86_set_cpl(env, 3);
    cpu_x86_load_seg_cache(env, R_CS, ((env->sysenter_cs + 16) & 0xfffc) | 3, 
                           0, 0xffffffff, 
                           DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                           DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                           DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_SS, ((env->sysenter_cs + 24) & 0xfffc) | 3, 
                           0, 0xffffffff,
                           DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                           DESC_S_MASK | (3 << DESC_DPL_SHIFT) |
                           DESC_W_MASK | DESC_A_MASK);
    ESP = ECX;
    EIP = EDX;
}

void helper_movl_crN_T0(int reg)
{
  static int exception;
#if !defined(CONFIG_USER_ONLY) 
    switch(reg) {
    case 0:
        cpu_x86_update_cr0(env, T0);
        break;
    case 3:
        cpu_x86_update_cr3(env, T0);
        break;
    case 4:
        cpu_x86_update_cr4(env, T0);
        break;
    case 8:
        cpu_set_apic_tpr(env, T0);
        break;
    default:
        env->cr[reg] = T0;
        break;
    }
#endif
}

/* XXX: do more */
void helper_movl_drN_T0(int reg)
{
    env->dr[reg] = T0;
}

void helper_invlpg(target_ulong addr)
{
    cpu_x86_flush_tlb(env, addr);
}

void helper_rdtsc(void)
{
    uint64_t val;

    if ((env->cr[4] & CR4_TSD_MASK) && ((env->hflags & HF_CPL_MASK) != 0)) {
        MDBG("Calling raise_exception(0x%x).\n", EXCP0D_GPF);
        raise_exception(EXCP0D_GPF);
    }
    val = cpu_get_tsc(env);
    EAX = (uint32_t)(val);
    EDX = (uint32_t)(val >> 32);
}

#if defined(CONFIG_USER_ONLY) 
void helper_wrmsr(void)
{
}

void helper_rdmsr(void)
{
}
#else
void helper_wrmsr(void)
{
    uint64_t val;

    val = ((uint32_t)EAX) | ((uint64_t)((uint32_t)EDX) << 32);

    switch((uint32_t)ECX) {
    case MSR_IA32_SYSENTER_CS:
        env->sysenter_cs = val & 0xffff;
        break;
    case MSR_IA32_SYSENTER_ESP:
        env->sysenter_esp = val;
        break;
    case MSR_IA32_SYSENTER_EIP:
        env->sysenter_eip = val;
        break;
    case MSR_IA32_APICBASE:
        cpu_set_apic_base(env, val);
        break;
    case MSR_EFER:
        {
            uint64_t update_mask;
            update_mask = 0;
            if (env->cpuid_ext2_features & CPUID_EXT2_SYSCALL)
                update_mask |= MSR_EFER_SCE;
            if (env->cpuid_ext2_features & CPUID_EXT2_LM)
                update_mask |= MSR_EFER_LME;
            if (env->cpuid_ext2_features & CPUID_EXT2_FFXSR)
                update_mask |= MSR_EFER_FFXSR;
            if (env->cpuid_ext2_features & CPUID_EXT2_NX)
                update_mask |= MSR_EFER_NXE;
            env->efer = (env->efer & ~update_mask) | 
            (val & update_mask);
        }
        break;
    case MSR_STAR:
        env->star = val;
        break;
    case MSR_PAT:
        env->pat = val;
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        env->lstar = val;
        break;
    case MSR_CSTAR:
        env->cstar = val;
        break;
    case MSR_FMASK:
        env->fmask = val;
        break;
    case MSR_FSBASE:
        env->segs[R_FS].base = val;
        break;
    case MSR_GSBASE:
        env->segs[R_GS].base = val;
        break;
    case MSR_KERNELGSBASE:
        env->kernelgsbase = val;
        break;
#endif
    default:
        /* XXX: exception ? */
        break; 
    }
}

void helper_rdmsr(void)
{
    uint64_t val;
    switch((uint32_t)ECX) {
    case MSR_IA32_SYSENTER_CS:
        val = env->sysenter_cs;
        break;
    case MSR_IA32_SYSENTER_ESP:
        val = env->sysenter_esp;
        break;
    case MSR_IA32_SYSENTER_EIP:
        val = env->sysenter_eip;
        break;
    case MSR_IA32_APICBASE:
        val = cpu_get_apic_base(env);
        break;
    case MSR_EFER:
        val = env->efer;
        break;
    case MSR_STAR:
        val = env->star;
        break;
    case MSR_PAT:
        val = env->pat;
        break;
#ifdef TARGET_X86_64
    case MSR_LSTAR:
        val = env->lstar;
        break;
    case MSR_CSTAR:
        val = env->cstar;
        break;
    case MSR_FMASK:
        val = env->fmask;
        break;
    case MSR_FSBASE:
        val = env->segs[R_FS].base;
        break;
    case MSR_GSBASE:
        val = env->segs[R_GS].base;
        break;
    case MSR_KERNELGSBASE:
        val = env->kernelgsbase;
        break;
#endif
    default:
        /* XXX: exception ? */
        val = 0;
        break; 
    }
    EAX = (uint32_t)(val);
    EDX = (uint32_t)(val >> 32);
}
#endif

void helper_lsl(void)
{
    unsigned int selector, limit;
    uint32_t e1, e2, eflags;
    int rpl, dpl, cpl, type;

    eflags = cc_table[CC_OP].compute_all();
    selector = T0 & 0xffff;
    if (load_segment(&e1, &e2, selector) != 0)
        goto fail;
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if ((e2 & DESC_CS_MASK) && (e2 & DESC_C_MASK)) {
            /* conforming */
        } else {
            if (dpl < cpl || dpl < rpl)
                goto fail;
        }
    } else {
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        switch(type) {
        case 1:
        case 2:
        case 3:
        case 9:
        case 11:
            break;
        default:
            goto fail;
        }
        if (dpl < cpl || dpl < rpl) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return;
        }
    }
    limit = get_seg_limit(e1, e2);
    T1 = limit;
    CC_SRC = eflags | CC_Z;
}

void helper_lar(void)
{
    unsigned int selector;
    uint32_t e1, e2, eflags;
    int rpl, dpl, cpl, type;

    eflags = cc_table[CC_OP].compute_all();
    selector = T0 & 0xffff;
    if ((selector & 0xfffc) == 0)
        goto fail;
    if (load_segment(&e1, &e2, selector) != 0)
        goto fail;
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_S_MASK) {
        if ((e2 & DESC_CS_MASK) && (e2 & DESC_C_MASK)) {
            /* conforming */
        } else {
            if (dpl < cpl || dpl < rpl)
                goto fail;
        }
    } else {
        type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
        switch(type) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 9:
        case 11:
        case 12:
            break;
        default:
            goto fail;
        }
        if (dpl < cpl || dpl < rpl) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return;
        }
    }
    T1 = e2 & 0x00f0ff00;
    CC_SRC = eflags | CC_Z;
}

void helper_verr(void)
{
    unsigned int selector;
    uint32_t e1, e2, eflags;
    int rpl, dpl, cpl;

    eflags = cc_table[CC_OP].compute_all();
    selector = T0 & 0xffff;
    if ((selector & 0xfffc) == 0)
        goto fail;
    if (load_segment(&e1, &e2, selector) != 0)
        goto fail;
    if (!(e2 & DESC_S_MASK))
        goto fail;
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_CS_MASK) {
        if (!(e2 & DESC_R_MASK))
            goto fail;
        if (!(e2 & DESC_C_MASK)) {
            if (dpl < cpl || dpl < rpl)
                goto fail;
        }
    } else {
        if (dpl < cpl || dpl < rpl) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return;
        }
    }
    CC_SRC = eflags | CC_Z;
}

void helper_verw(void)
{
    unsigned int selector;
    uint32_t e1, e2, eflags;
    int rpl, dpl, cpl;

    eflags = cc_table[CC_OP].compute_all();
    selector = T0 & 0xffff;
    if ((selector & 0xfffc) == 0)
        goto fail;
    if (load_segment(&e1, &e2, selector) != 0)
        goto fail;
    if (!(e2 & DESC_S_MASK))
        goto fail;
    rpl = selector & 3;
    dpl = (e2 >> DESC_DPL_SHIFT) & 3;
    cpl = env->hflags & HF_CPL_MASK;
    if (e2 & DESC_CS_MASK) {
        goto fail;
    } else {
        if (dpl < cpl || dpl < rpl)
            goto fail;
        if (!(e2 & DESC_W_MASK)) {
        fail:
            CC_SRC = eflags & ~CC_Z;
            return;
        }
    }
    CC_SRC = eflags | CC_Z;
}

/* FPU helpers */

void helper_fldt_ST0_A0(void)
{
    int new_fpstt;
    new_fpstt = (env->fpstt - 1) & 7;
    env->fpregs[new_fpstt].d = helper_fldt(A0);
    env->fpstt = new_fpstt;
    env->fptags[new_fpstt] = 0; /* validate stack entry */
}

void helper_fstt_ST0_A0(void)
{
    helper_fstt(ST0, A0);
}

void fpu_set_exception(int mask)
{
    env->fpus |= mask;
    if (env->fpus & (~env->fpuc & FPUC_EM))
        env->fpus |= FPUS_SE | FPUS_B;
}

CPU86_LDouble helper_fdiv(CPU86_LDouble a, CPU86_LDouble b)
{
    if (b == 0.0) 
        fpu_set_exception(FPUS_ZE);
    return a / b;
}

void fpu_raise_exception(void)
{
    if (env->cr[0] & CR0_NE_MASK) {
        raise_exception(EXCP10_COPR);
    } 
#if !defined(CONFIG_USER_ONLY) 
    else {
        cpu_set_ferr(env);
    }
#endif
}

/* BCD ops */

void helper_fbld_ST0_A0(void)
{
    CPU86_LDouble tmp;
    uint64_t val;
    unsigned int v;
    int i;

    val = 0;
    for(i = 8; i >= 0; i--) {
        v = ldub(A0 + i);
        val = (val * 100) + ((v >> 4) * 10) + (v & 0xf);
    }
    tmp = val;
    if (ldub(A0 + 9) & 0x80)
        tmp = -tmp;
    fpush();
    ST0 = tmp;
}

void helper_fbst_ST0_A0(void)
{
    int v;
    target_ulong mem_ref, mem_end;
    int64_t val;

    val = floatx_to_int64(ST0, &env->fp_status);
    mem_ref = A0;
    mem_end = mem_ref + 9;
    if (val < 0) {
        stb(mem_end, 0x80);
        val = -val;
    } else {
        stb(mem_end, 0x00);
    }
    while (mem_ref < mem_end) {
        if (val == 0)
            break;
        v = val % 100;
        val = val / 100;
        v = ((v / 10) << 4) | (v % 10);
        stb(mem_ref++, v);
    }
    while (mem_ref < mem_end) {
        stb(mem_ref++, 0);
    }
}

void helper_f2xm1(void)
{
    ST0 = pow(2.0,ST0) - 1.0;
}

void helper_fyl2x(void)
{
    CPU86_LDouble fptemp;
    
    fptemp = ST0;
    if (fptemp>0.0){
        fptemp = log(fptemp)/log(2.0);	 /* log2(ST) */
        ST1 *= fptemp;
        fpop();
    } else { 
        env->fpus &= (~0x4700);
        env->fpus |= 0x400;
    }
}

void helper_fptan(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = tan(fptemp);
        fpush();
        ST0 = 1.0;
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg| < 2**52 only */
    }
}

void helper_fpatan(void)
{
    CPU86_LDouble fptemp, fpsrcop;

    fpsrcop = ST1;
    fptemp = ST0;
    ST1 = atan2(fpsrcop,fptemp);
    fpop();
}

void helper_fxtract(void)
{
    CPU86_LDoubleU temp;
    unsigned int expdif;

    temp.d = ST0;
    expdif = EXPD(temp) - EXPBIAS;
    /*DP exponent bias*/
    ST0 = expdif;
    fpush();
    BIASEXPONENT(temp);
    ST0 = temp.d;
}

void helper_fprem1(void)
{
    CPU86_LDouble dblq, fpsrcop, fptemp;
    CPU86_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    int q;

    fpsrcop = ST0;
    fptemp = ST1;
    fpsrcop1.d = fpsrcop;
    fptemp1.d = fptemp;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);
    if (expdif < 53) {
        dblq = fpsrcop / fptemp;
        dblq = (dblq < 0.0)? ceil(dblq): floor(dblq);
        ST0 = fpsrcop - fptemp*dblq;
        q = (int)dblq; /* cutting off top bits is assumed here */
        env->fpus &= (~0x4700); /* (C3,C2,C1,C0) <-- 0000 */
				/* (C0,C1,C3) <-- (q2,q1,q0) */
        env->fpus |= (q&0x4) << 6; /* (C0) <-- q2 */
        env->fpus |= (q&0x2) << 8; /* (C1) <-- q1 */
        env->fpus |= (q&0x1) << 14; /* (C3) <-- q0 */
    } else {
        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, expdif-50);
        fpsrcop = (ST0 / ST1) / fptemp;
        /* fpsrcop = integer obtained by rounding to the nearest */
        fpsrcop = (fpsrcop-floor(fpsrcop) < ceil(fpsrcop)-fpsrcop)?
            floor(fpsrcop): ceil(fpsrcop);
        ST0 -= (ST1 * fpsrcop * fptemp);
    }
}

void helper_fprem(void)
{
    CPU86_LDouble dblq, fpsrcop, fptemp;
    CPU86_LDoubleU fpsrcop1, fptemp1;
    int expdif;
    int q;
    
    fpsrcop = ST0;
    fptemp = ST1;
    fpsrcop1.d = fpsrcop;
    fptemp1.d = fptemp;
    expdif = EXPD(fpsrcop1) - EXPD(fptemp1);
    if ( expdif < 53 ) {
        dblq = fpsrcop / fptemp;
        dblq = (dblq < 0.0)? ceil(dblq): floor(dblq);
        ST0 = fpsrcop - fptemp*dblq;
        q = (int)dblq; /* cutting off top bits is assumed here */
        env->fpus &= (~0x4700); /* (C3,C2,C1,C0) <-- 0000 */
				/* (C0,C1,C3) <-- (q2,q1,q0) */
        env->fpus |= (q&0x4) << 6; /* (C0) <-- q2 */
        env->fpus |= (q&0x2) << 8; /* (C1) <-- q1 */
        env->fpus |= (q&0x1) << 14; /* (C3) <-- q0 */
    } else {
        env->fpus |= 0x400;  /* C2 <-- 1 */
        fptemp = pow(2.0, expdif-50);
        fpsrcop = (ST0 / ST1) / fptemp;
        /* fpsrcop = integer obtained by chopping */
        fpsrcop = (fpsrcop < 0.0)?
            -(floor(fabs(fpsrcop))): floor(fpsrcop);
        ST0 -= (ST1 * fpsrcop * fptemp);
    }
}

void helper_fyl2xp1(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if ((fptemp+1.0)>0.0) {
        fptemp = log(fptemp+1.0) / log(2.0); /* log2(ST+1.0) */
        ST1 *= fptemp;
        fpop();
    } else { 
        env->fpus &= (~0x4700);
        env->fpus |= 0x400;
    }
}

void helper_fsqrt(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if (fptemp<0.0) { 
        env->fpus &= (~0x4700);  /* (C3,C2,C1,C0) <-- 0000 */
        env->fpus |= 0x400;
    }
    ST0 = sqrt(fptemp);
}

void helper_fsincos(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if ((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = sin(fptemp);
        fpush();
        ST0 = cos(fptemp);
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg| < 2**63 only */
    }
}

void helper_frndint(void)
{
    ST0 = floatx_round_to_int(ST0, &env->fp_status);
}

void helper_fscale(void)
{
    ST0 = ldexp (ST0, (int)(ST1)); 
}

void helper_fsin(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if ((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = sin(fptemp);
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg| < 2**53 only */
    }
}

void helper_fcos(void)
{
    CPU86_LDouble fptemp;

    fptemp = ST0;
    if((fptemp > MAXTAN)||(fptemp < -MAXTAN)) {
        env->fpus |= 0x400;
    } else {
        ST0 = cos(fptemp);
        env->fpus &= (~0x400);  /* C2 <-- 0 */
        /* the above code is for  |arg5 < 2**63 only */
    }
}

void helper_fxam_ST0(void)
{
    CPU86_LDoubleU temp;
    int expdif;

    temp.d = ST0;

    env->fpus &= (~0x4700);  /* (C3,C2,C1,C0) <-- 0000 */
    if (SIGND(temp))
        env->fpus |= 0x200; /* C1 <-- 1 */

    /* XXX: test fptags too */
    expdif = EXPD(temp);
    if (expdif == MAXEXPD) {
#ifdef USE_X86LDOUBLE
        if (MANTD(temp) == 0x8000000000000000ULL)
#else
        if (MANTD(temp) == 0)
#endif
            env->fpus |=  0x500 /*Infinity*/;
        else
            env->fpus |=  0x100 /*NaN*/;
    } else if (expdif == 0) {
        if (MANTD(temp) == 0)
            env->fpus |=  0x4000 /*Zero*/;
        else
            env->fpus |= 0x4400 /*Denormal*/;
    } else {
        env->fpus |= 0x400;
    }
}

void helper_fstenv(target_ulong ptr, int data32)
{
    int fpus, fptag, exp, i;
    uint64_t mant;
    CPU86_LDoubleU tmp;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i=7; i>=0; i--) {
	fptag <<= 2;
	if (env->fptags[i]) {
            fptag |= 3;
	} else {
            tmp.d = env->fpregs[i].d;
            exp = EXPD(tmp);
            mant = MANTD(tmp);
            if (exp == 0 && mant == 0) {
                /* zero */
	        fptag |= 1;
	    } else if (exp == 0 || exp == MAXEXPD
#ifdef USE_X86LDOUBLE
                       || (mant & (1LL << 63)) == 0
#endif
                       ) {
                /* NaNs, infinity, denormal */
                fptag |= 2;
            }
        }
    }
    if (data32) {
        /* 32 bit */
        stl(ptr, env->fpuc);
        stl(ptr + 4, fpus);
        stl(ptr + 8, fptag);
        stl(ptr + 12, 0); /* fpip */
        stl(ptr + 16, 0); /* fpcs */
        stl(ptr + 20, 0); /* fpoo */
        stl(ptr + 24, 0); /* fpos */
    } else {
        /* 16 bit */
        stw(ptr, env->fpuc);
        stw(ptr + 2, fpus);
        stw(ptr + 4, fptag);
        stw(ptr + 6, 0);
        stw(ptr + 8, 0);
        stw(ptr + 10, 0);
        stw(ptr + 12, 0);
    }
}

void helper_fldenv(target_ulong ptr, int data32)
{
    int i, fpus, fptag;

    if (data32) {
	env->fpuc = lduw(ptr);
        fpus = lduw(ptr + 4);
        fptag = lduw(ptr + 8);
    }
    else {
	env->fpuc = lduw(ptr);
        fpus = lduw(ptr + 2);
        fptag = lduw(ptr + 4);
    }
    env->fpstt = (fpus >> 11) & 7;
    env->fpus = fpus & ~0x3800;
    for(i = 0;i < 8; i++) {
        env->fptags[i] = ((fptag & 3) == 3);
        fptag >>= 2;
    }
}

void helper_fsave(target_ulong ptr, int data32)
{
    CPU86_LDouble tmp;
    int i;

    helper_fstenv(ptr, data32);

    ptr += (14 << data32);
    for(i = 0;i < 8; i++) {
        tmp = ST(i);
        helper_fstt(tmp, ptr);
        ptr += 10;
    }

    /* fninit */
    env->fpus = 0;
    env->fpstt = 0;
    env->fpuc = 0x37f;
    env->fptags[0] = 1;
    env->fptags[1] = 1;
    env->fptags[2] = 1;
    env->fptags[3] = 1;
    env->fptags[4] = 1;
    env->fptags[5] = 1;
    env->fptags[6] = 1;
    env->fptags[7] = 1;
}

void helper_frstor(target_ulong ptr, int data32)
{
    CPU86_LDouble tmp;
    int i;

    helper_fldenv(ptr, data32);
    ptr += (14 << data32);

    for(i = 0;i < 8; i++) {
        tmp = helper_fldt(ptr);
        ST(i) = tmp;
        ptr += 10;
    }
}

void helper_fxsave(target_ulong ptr, int data64)
{
    int fpus, fptag, i, nb_xmm_regs;
    CPU86_LDouble tmp;
    target_ulong addr;

    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for(i = 0; i < 8; i++) {
        fptag |= (env->fptags[i] << i);
    }
    stw(ptr, env->fpuc);
    stw(ptr + 2, fpus);
    stw(ptr + 4, fptag ^ 0xff);

    addr = ptr + 0x20;
    for(i = 0;i < 8; i++) {
        tmp = ST(i);
        helper_fstt(tmp, addr);
        addr += 16;
    }
    
    if (env->cr[4] & CR4_OSFXSR_MASK) {
        /* XXX: finish it */
        stl(ptr + 0x18, env->mxcsr); /* mxcsr */
        stl(ptr + 0x1c, 0x0000ffff); /* mxcsr_mask */
        nb_xmm_regs = 8 << data64;
        addr = ptr + 0xa0;
        for(i = 0; i < nb_xmm_regs; i++) {
            stq(addr, env->xmm_regs[i].XMM_Q(0));
            stq(addr + 8, env->xmm_regs[i].XMM_Q(1));
            addr += 16;
        }
    }
}

void helper_fxrstor(target_ulong ptr, int data64)
{
    int i, fpus, fptag, nb_xmm_regs;
    CPU86_LDouble tmp;
    target_ulong addr;

    env->fpuc = lduw(ptr);
    fpus = lduw(ptr + 2);
    fptag = lduw(ptr + 4);
    env->fpstt = (fpus >> 11) & 7;
    env->fpus = fpus & ~0x3800;
    fptag ^= 0xff;
    for(i = 0;i < 8; i++) {
        env->fptags[i] = ((fptag >> i) & 1);
    }

    addr = ptr + 0x20;
    for(i = 0;i < 8; i++) {
        tmp = helper_fldt(addr);
        ST(i) = tmp;
        addr += 16;
    }

    if (env->cr[4] & CR4_OSFXSR_MASK) {
        /* XXX: finish it */
        env->mxcsr = ldl(ptr + 0x18);
        //ldl(ptr + 0x1c);
        nb_xmm_regs = 8 << data64;
        addr = ptr + 0xa0;
        for(i = 0; i < nb_xmm_regs; i++) {
            env->xmm_regs[i].XMM_Q(0) = ldq(addr);
            env->xmm_regs[i].XMM_Q(1) = ldq(addr + 8);
            addr += 16;
        }
    }
}

#ifndef USE_X86LDOUBLE

void cpu_get_fp80(uint64_t *pmant, uint16_t *pexp, CPU86_LDouble f)
{
    CPU86_LDoubleU temp;
    int e;

    temp.d = f;
    /* mantissa */
    *pmant = (MANTD(temp) << 11) | (1LL << 63);
    /* exponent + sign */
    e = EXPD(temp) - EXPBIAS + 16383;
    e |= SIGND(temp) >> 16;
    *pexp = e;
}

CPU86_LDouble cpu_set_fp80(uint64_t mant, uint16_t upper)
{
    CPU86_LDoubleU temp;
    int e;
    uint64_t ll;

    /* XXX: handle overflow ? */
    e = (upper & 0x7fff) - 16383 + EXPBIAS; /* exponent */
    e |= (upper >> 4) & 0x800; /* sign */
    ll = (mant >> 11) & ((1LL << 52) - 1);
#ifdef __arm__
    temp.l.upper = (e << 20) | (ll >> 32);
    temp.l.lower = ll;
#else
    temp.ll = ll | ((uint64_t)e << 52);
#endif
    return temp.d;
}

#else

void cpu_get_fp80(uint64_t *pmant, uint16_t *pexp, CPU86_LDouble f)
{
    CPU86_LDoubleU temp;

    temp.d = f;
    *pmant = temp.l.lower;
    *pexp = temp.l.upper;
}

CPU86_LDouble cpu_set_fp80(uint64_t mant, uint16_t upper)
{
    CPU86_LDoubleU temp;

    temp.l.upper = upper;
    temp.l.lower = mant;
    return temp.d;
}
#endif

#ifdef TARGET_X86_64

//#define DEBUG_MULDIV

static void add128(uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b)
{
    *plow += a;
    /* carry test */
    if (*plow < a)
        (*phigh)++;
    *phigh += b;
}

static void neg128(uint64_t *plow, uint64_t *phigh)
{
    *plow = ~ *plow;
    *phigh = ~ *phigh;
    add128(plow, phigh, 1, 0);
}

static void mul64(uint64_t *plow, uint64_t *phigh, uint64_t a, uint64_t b)
{
    uint32_t a0, a1, b0, b1;
    uint64_t v;

    a0 = a;
    a1 = a >> 32;

    b0 = b;
    b1 = b >> 32;
    
    v = (uint64_t)a0 * (uint64_t)b0;
    *plow = v;
    *phigh = 0;

    v = (uint64_t)a0 * (uint64_t)b1;
    add128(plow, phigh, v << 32, v >> 32);
    
    v = (uint64_t)a1 * (uint64_t)b0;
    add128(plow, phigh, v << 32, v >> 32);
    
    v = (uint64_t)a1 * (uint64_t)b1;
    *phigh += v;
#ifdef DEBUG_MULDIV
    printf("mul: 0x%016" PRIx64 " * 0x%016" PRIx64 " = 0x%016" PRIx64 "%016" PRIx64 "\n",
           a, b, *phigh, *plow);
#endif
}

static void imul64(uint64_t *plow, uint64_t *phigh, int64_t a, int64_t b)
{
    int sa, sb;
    sa = (a < 0);
    if (sa)
        a = -a;
    sb = (b < 0);
    if (sb)
        b = -b;
    mul64(plow, phigh, a, b);
    if (sa ^ sb) {
        neg128(plow, phigh);
    }
}

/* return TRUE if overflow */
static int div64(uint64_t *plow, uint64_t *phigh, uint64_t b)
{
    uint64_t q, r, a1, a0;
    int i, qb, ab;

    a0 = *plow;
    a1 = *phigh;
    if (a1 == 0) {
        q = a0 / b;
        r = a0 % b;
        *plow = q;
        *phigh = r;
    } else {
        if (a1 >= b)
            return 1;
        /* XXX: use a better algorithm */
        for(i = 0; i < 64; i++) {
            ab = a1 >> 63;
            a1 = (a1 << 1) | (a0 >> 63);
            if (ab || a1 >= b) {
                a1 -= b;
                qb = 1;
            } else {
                qb = 0;
            }
            a0 = (a0 << 1) | qb;
        }
#if defined(DEBUG_MULDIV)
        printf("div: 0x%016" PRIx64 "%016" PRIx64 " / 0x%016" PRIx64 ": q=0x%016" PRIx64 " r=0x%016" PRIx64 "\n",
               *phigh, *plow, b, a0, a1);
#endif
        *plow = a0;
        *phigh = a1;
    }
    return 0;
}

/* return TRUE if overflow */
static int idiv64(uint64_t *plow, uint64_t *phigh, int64_t b)
{
    int sa, sb;
    sa = ((int64_t)*phigh < 0);
    if (sa)
        neg128(plow, phigh);
    sb = (b < 0);
    if (sb)
        b = -b;
    if (div64(plow, phigh, b) != 0)
        return 1;
    if (sa ^ sb) {
        if (*plow > (1ULL << 63))
            return 1;
        *plow = - *plow;
    } else {
        if (*plow >= (1ULL << 63))
            return 1;
    }
    if (sa)
        *phigh = - *phigh;
    return 0;
}

void helper_mulq_EAX_T0(void)
{
    uint64_t r0, r1;

    mul64(&r0, &r1, EAX, T0);
    EAX = r0;
    EDX = r1;
    CC_DST = r0;
    CC_SRC = r1;
}

void helper_imulq_EAX_T0(void)
{
    uint64_t r0, r1;

    imul64(&r0, &r1, EAX, T0);
    EAX = r0;
    EDX = r1;
    CC_DST = r0;
    CC_SRC = ((int64_t)r1 != ((int64_t)r0 >> 63));
}

void helper_imulq_T0_T1(void)
{
    uint64_t r0, r1;

    imul64(&r0, &r1, T0, T1);
    T0 = r0;
    CC_DST = r0;
    CC_SRC = ((int64_t)r1 != ((int64_t)r0 >> 63));
}

void helper_divq_EAX_T0(void)
{
    uint64_t r0, r1;
    if (T0 == 0) {
        raise_exception(EXCP00_DIVZ);
    }
    r0 = EAX;
    r1 = EDX;
    if (div64(&r0, &r1, T0))
        raise_exception(EXCP00_DIVZ);
    EAX = r0;
    EDX = r1;
}

void helper_idivq_EAX_T0(void)
{
    uint64_t r0, r1;
    if (T0 == 0) {
        raise_exception(EXCP00_DIVZ);
    }
    r0 = EAX;
    r1 = EDX;
    if (idiv64(&r0, &r1, T0))
        raise_exception(EXCP00_DIVZ);
    EAX = r0;
    EDX = r1;
}

void helper_bswapq_T0(void)
{
    T0 = bswap64(T0);
}
#endif

void helper_hlt(void)
{
    env->hflags &= ~HF_INHIBIT_IRQ_MASK; /* needed if sti is just before */
    env->hflags |= HF_HALTED_MASK;
    env->exception_index = EXCP_HLT;
    MDBG("Calling cpu_loop_exit()%c\n", '.');
    cpu_loop_exit();
}

void helper_monitor(void)
{
    if ((uint32_t)ECX != 0) {
        MDBG("Calling raise_exception(0x%x).\n", EXCP0D_GPF);
        raise_exception(EXCP0D_GPF);
    }
    /* XXX: store address ? */
}

void helper_mwait(void)
{
    if ((uint32_t)ECX != 0) {
        MDBG("Calling raise_exception(0x%x).\n", EXCP0D_GPF);
        raise_exception(EXCP0D_GPF);
    }
    /* XXX: not complete but not completely erroneous */
    if (env->cpu_index != 0 || env->next_cpu != NULL) {
        /* more than one CPU: do not sleep because another CPU may
           wake this one */
    } else {
        helper_hlt();
    }
}

float approx_rsqrt(float a)
{
    return 1.0 / sqrt(a);
}

float approx_rcp(float a)
{
    return 1.0 / a;
}

void update_fp_status(void)
{
    int rnd_type;

    /* set rounding mode */
    switch(env->fpuc & RC_MASK) {
    default:
    case RC_NEAR:
        rnd_type = float_round_nearest_even;
        break;
    case RC_DOWN:
        rnd_type = float_round_down;
        break;
    case RC_UP:
        rnd_type = float_round_up;
        break;
    case RC_CHOP:
        rnd_type = float_round_to_zero;
        break;
    }
    set_float_rounding_mode(rnd_type, &env->fp_status);
#ifdef FLOATX80
    switch((env->fpuc >> 8) & 3) {
    case 0:
        rnd_type = 32;
        break;
    case 2:
        rnd_type = 64;
        break;
    case 3:
    default:
        rnd_type = 80;
        break;
    }
    set_floatx80_rounding_precision(rnd_type, &env->fp_status);
#endif
}

#if !defined(CONFIG_USER_ONLY) 

#define MMUSUFFIX _mmu
#define GETPC() (__builtin_return_address(0))

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* sorav */

static inline uint32_t
ld_kernel_no_exception(target_ulong ptr, int *exception, size_t data_size)
{
  int index;
  uint32_t res;
  target_ulong addr;
  unsigned long physaddr;
  int is_user;

  addr = ptr;
  index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
  is_user = 0;
  *exception = 0;
  if (__builtin_expect(env->tlb_table[is_user][index].addr_read != 
        (addr & TARGET_PAGE_MASK), 0)) {
    printf("%s(): calling __ldb_mmu(is_user = 0)\n", __func__);
    switch (data_size) {
      case 1: res = __ldb_mmu(addr, 0, exception); break;
      case 2: res = __ldw_mmu(addr, 0, exception); break;
      case 4: res = __ldl_mmu(addr, 0, exception); break;
      default: ASSERT(0);
    }
  } else {
    physaddr = addr + env->tlb_table[is_user][index].addend;
    //res = ldub_raw((uint8_t *)physaddr);
    switch (data_size) {
      case 1: res = ldub_raw((uint8_t *)physaddr); break;
      case 2: res = lduw_raw((uint8_t *)physaddr); break;
      case 4: res = ldl_raw((uint8_t *)physaddr); break;
      default: ASSERT(0);
    }
  }
  return res;
}
/* sorav */

#endif




/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(target_ulong addr, int is_write, int is_user, void *retaddr,
    int *exception)
{
    TranslationBlock *tb;
    int ret;
    unsigned long pc;
    CPUX86State *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;

    ret = cpu_x86_handle_mmu_fault(env, addr, is_write, is_user, 1, exception);
    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }
        if (retaddr) {
          MDBG("Calling raise_exception_err(0x%x). addr=0x%x, "
              "is_write=%d, is_user=%d\n", env->exception_index, addr,
              is_write, is_user);
          raise_exception_err(env->exception_index, env->error_code);
        } else {
          MDBG("Calling raise_exception_err_norestore(0x%x). addr=0x%x, "
              "is_write=%d, is_user=%d\n", env->exception_index, addr,
              is_write, is_user);
          raise_exception_err_norestore(env->exception_index, env->error_code);
        }
    }
    env = saved_env;
}

void
rr_log_helper_load_seg(CPUState *env1, int seg_reg, SegmentCache const *sc,
    uint32_t cr0)
{
  int dpl, rpl;

  if (!(cr0 & CR0_PE_MASK)) {
    return;
  } 

  ASSERT(cr0 & CR0_PE_MASK);      /* protected mode. */
  if ((sc->selector & 0xfffc) == 0) {
    ASSERT(seg_reg != R_SS);    /* ss cannot be loaded with NULL segment. */
    goto load_seg_cache;
  }

  rpl = sc->selector & 3;
  dpl = (sc->flags >> DESC_DPL_SHIFT) & 3;
  if (seg_reg == R_SS) {
    /* must be a writable segment. */
    ASSERT(!(sc->flags & DESC_CS_MASK) && (sc->flags & DESC_W_MASK));
    ASSERT(rpl == dpl);
  } else {
    /* must be a readable segment. */
    ASSERT((sc->flags & (DESC_CS_MASK | DESC_R_MASK)) != DESC_CS_MASK);
    if (!(sc->flags & DESC_CS_MASK) || !(sc->flags & DESC_C_MASK)) {
      ASSERT(dpl >= rpl);
    }
  }

  ASSERT(sc->flags & DESC_P_MASK);
load_seg_cache:
  cpu_x86_load_seg_cache(env1, seg_reg, sc->selector, sc->base, sc->limit,
      sc->flags);
}

uint32_t
helper_compute_eflags(CPUState *env1)
{
  uint32_t env_eflags;
  save_caller_regs();
  env = env1;
  env_eflags = cc_table[env1->cc_op].compute_all();
  env_eflags |= (DF & DF_MASK);
  env_eflags |= env1->eflags & ~(VM_MASK | RF_MASK);
  restore_caller_regs();
  return env_eflags;
}

void
helper_set_cc_op(long val)
{
  printf("%s(%#x) called.\n", __func__, val);
}

void
helper_panic(void)
{
  int i;
  printf("qemu: Panic tag seen.\n");
  for (i = 0; i < 0x50000000; i++);
  exit(PANIC_EXITCODE);
}

void
helper_exit(void)
{
  int i;
  printf("qemu: Exit tag seen.\n");
  for (i = 0; i < 0x50000000; i++);
  exit(0);
}

void
helper_profile_reset(void)
{
  profile_log_reset();
}

extern int romwrite;
extern long romwrite_dst;
extern long romwrite_src;
extern long romwrite_size;

void
helper_romwrite_set_dst(long val)
{
  //printf("%s(%#lx) called.\n", __func__, val);
  romwrite_dst = val;
}

void
helper_romwrite_set_src(long val)
{
  //printf("%s(%#lx) called.\n", __func__, val);
  romwrite_src = val;
}

void
helper_romwrite_set_size(long val)
{
  //printf("%s(%#lx) called.\n", __func__, val);
  romwrite_size = val;
}

void
helper_romwrite_transfer(void)
{
  //printf("%s() called. romwrite_size=%#x\n", __func__, romwrite_size);
  ASSERT(romwrite_size > 0);
  uint8_t buf[romwrite_size];
  romwrite = 1;
  cpu_physical_memory_rw(romwrite_src, buf, romwrite_size, 0);
  cpu_physical_memory_rw(romwrite_dst, buf, romwrite_size, 1);
  romwrite = 0;
  romwrite_size = 0;
}

void
helper_exit_tb(void)
{
#if 0
  if (env->n_exec >= 0x10000) {
    printf("%s() %d: *0x4695e5=%hhx\n", __func__, __LINE__,
        ldub_raw(0x4695e5));
  }
#endif
}
