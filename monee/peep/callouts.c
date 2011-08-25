#include "peep/callouts.h"
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include "mem/vaddr.h"
#include "mem/palloc.h"
#include "mem/paging.h"
#include "mem/pt_mode.h"
#include "peep/peep.h"
#include "peep/tb.h"
#include "peep/jumptable2.h"
#include "peep/cpu_constraints.h"
#include "peep/callouts.h"
#include "peep/insn.h"
#include "peepgen_offsets.h"
#include "peep/peeptab_defs.h"
#include "sys/gdt.h"
#include "sys/interrupt.h"
#include "sys/exception.h"
#include "sys/monitor.h"
#include "sys/vcpu.h"
#include "sys/rr_log.h"
#include "sys/flags.h"
#include "sys/mode.h"
#include "threads/synch.h"

#define PCALL 2

/* Callout functions. They are called by assembly code in peep.tab, hence
 * need global linkage.
 */
void callout_mov_to_cr3(unsigned long paddr, unsigned long fallthrough_addr);
void callout_in(unsigned long port, size_t data_size);
void callout_ins(size_t data_size, unsigned long prefix);
void callout_out(unsigned long port, size_t data_size);
void callout_outs(size_t data_size, unsigned long prefix);
void callout_int(unsigned long intno, unsigned long fallthrough_addr);
void callout_int3(unsigned long intno);
void callout_mov_to_cr0(unsigned long val);
void callout_ltr_val(unsigned long val);
void callout_restore_tr0_and_ltr_mem(long tr0, long segno);
void callout_restore_tr0_and_lgdt(long tr0, long segno);
void callout_restore_tr0_and_lidt(long tr0, long segno);
void callout_lcall(unsigned long new_cs, unsigned long new_eip, long size,
    long fallthrough_addr);
void callout_lret(unsigned long size, unsigned long popsize);
void callout_ljmp(unsigned long new_cs, unsigned long new_eip);
void callout_restore_tr0_and_ljmp_indir(long tr0, long segno,
    int dsize);
void callout_restore_tr0_and_lcall_indir(long tr0, long segno,
    int dsize, unsigned long fallthrough_addr);
void callout_real_mov_to_seg(unsigned long val, long segno);
void callout_real_mov_to_seg_restore_tr0(long tr0, long segno);
void callout_real_lxs_restore_tr0(long tr0, long segno, long regno);
void callout_real_pop_seg(long segno);
void callout_real_push_seg(long segno);
void callout_real_mov_seg_to_reg(long segno, long regno);
void callout_real_mov_seg_to_mem_restore_tr0(long src_segno,
    long target_segno, long tr0);
void callout_iret(void);
void callout_hlt(void);
//void callout_sti(long fallthrough_addr);
void callout_nop(void);
void callout_invd(void);
void callout_real_movs(size_t data_size, unsigned long prefix);
void callout_real_stos(size_t data_size, unsigned long prefix);
void callout_real_lods(size_t data_size, unsigned long prefix);
void callout_real_scas(size_t data_size, unsigned long prefix);
void callout_real_cmps(size_t data_size, unsigned long prefix);

/* Exception funcs. */
void gpf_load_seg(long segno, long descno_val);

/* Stats. */
static long long stats_num_callouts = 0;
static long long stats_num_forced_callouts = 0;
static long long stats_num_sti_callouts = 0;

#define CALLOUT_INC_STATS() do {																						\
	stats_num_callouts++;																											\
	if (!strcmp(__func__, "callout_sti")) {																		\
		stats_num_sti_callouts++;																								\
	}																																					\
} while(0)


void
callout_mov_to_cr3(unsigned long paddr, unsigned long fallthrough_addr)
{
	CALLOUT_INC_STATS();
  LOG(PCALL, "%s(0x%lx) called.\n", __func__, paddr);

  if (vcpu.cr[3] != paddr) {
    vcpu.cr[3] = paddr;
    shadow_pagedir_sync();
    //tb_unchain_all();       //XXX: improve this.
    jumptable1_clear();
    jumptable2_clear();
  }
  vcpu.eip = (void *)fallthrough_addr;
}

void
callout_in(unsigned long port, size_t data_size)
{
  uint32_t tmp = 0;

	CALLOUT_INC_STATS();
  LOG(PCALL, "%s(0x%lx,%d) called.\n", __func__, port, data_size);
  switch (data_size) {
    case 1:
      vcpu.regs[0] = (vcpu.regs[0] & 0xffffff00) | rr_inb(port);
      break;
    case 2:
      vcpu.regs[0] = (vcpu.regs[0] & 0xffff0000) | rr_inw(port);
      break;
    case 4:
      vcpu.regs[0] = rr_inl(port);
      break;
  }
}

void
callout_ins(size_t data_size, unsigned long prefix)
{
  size_t count = 1;
  bool prefixed_rep;

	CALLOUT_INC_STATS();
  prefixed_rep = (prefix & PREFIX_REPZ) || (prefix & PREFIX_REPNZ);
  LOG(PCALL, "%s(%d, 0x%lx) called.\n", __func__, data_size, prefix);
  if (prefixed_rep) {
    count = vcpu.regs[R_ECX];
  }
  if ((vcpu.cr[0] & CR0_PE_MASK) == 0) {
    count &= 0xffff;
  }
  segcache_sync(R_ES);
  rr_ins(vcpu.regs[R_EDX], (void *)(vcpu.segs[R_ES].base + vcpu.regs[R_EDI]),
      count, data_size);
  vcpu.regs[R_EDI] += count*data_size;
  if (prefixed_rep) {
    vcpu.regs[R_ECX] -= count;
  }
}

void
callout_out(unsigned long port, size_t data_size)
{
  target_ulong mask;
	CALLOUT_INC_STATS();
  LOG(PCALL, "%s(0x%lx,%d) called.\n", __func__, port, data_size);

  mask = (1 << (data_size*8)) - 1;
  rr_out(port, vcpu.regs[0] & mask, data_size);
}

void
callout_outs(size_t data_size, unsigned long prefix)
{
	CALLOUT_INC_STATS();
  LOG(PCALL, "%s(%d, 0x%lx) called.\n", __func__, data_size, prefix);
  size_t count = 1;
  bool prefixed_rep;

  prefixed_rep = (prefix & PREFIX_REPZ) || (prefix & PREFIX_REPNZ);
  LOG(PCALL, "%s(%d, 0x%lx) called.\n", __func__, data_size, prefix);
  if (prefixed_rep) {
    count = vcpu.regs[R_ECX];
  }
  if ((vcpu.cr[0] & CR0_PE_MASK) == 0) {
    count &= 0xffff;
  }
  segcache_sync(R_ES);
  rr_outs(vcpu.regs[R_EDX], (void *)(vcpu.segs[R_ES].base + vcpu.regs[R_ESI]),
      count, data_size);
  vcpu.regs[R_ESI] += count*data_size;
  if (prefixed_rep) {
    vcpu.regs[R_ECX] -= count;
  }
}

void
callout_int(unsigned long intno, unsigned long fallthrough_addr)
{
  int dpl;

	CALLOUT_INC_STATS();
  LOG(PCALL, "%s(0x%lx,0x%lx) called.\n", __func__, intno, fallthrough_addr);
  LOG(PCALL, "eax=0x%x, ecx=0x%x, edx=0x%x, ebx=0x%x, es=0x%hx\n",
      vcpu.regs[R_EAX], vcpu.regs[R_ECX], vcpu.regs[R_EDX], vcpu.regs[R_EBX],
      vcpu.orig_segs[R_ES]);
  vcpu.eip = (void *)fallthrough_addr;
  raise_interrupt(intno, 1, -1, (target_ulong)vcpu.eip);
}

void
callout_int3(unsigned long intno)
{
	CALLOUT_INC_STATS();
  LOG(PCALL, "%s() called.\n", __func__);
}

void
callout_mov_to_cr0(unsigned long val)
{
	CALLOUT_INC_STATS();
  LOG(PCALL, "%s(0x%lx) called.\n", __func__, val);
  vcpu.cr[0] = val;
  shadow_pagedir_sync();
  /*
  if (using_cr3_page_table) {
    load_shadow_pagedir();
    orig_pagedir_real_cleanup();
  }
  */
}

void
callout_ltr_val(unsigned long val)
{
  int selector;
  desc_table_t *dt;
  uint32_t e1, e2;
  unsigned index, type, entry_limit;
  target_ulong ptr;

	CALLOUT_INC_STATS();
  selector = val & 0xffff;
  if ((selector & 0xfffc) == 0) {
    /* NULL selector case: invalid TR */
    vcpu.tr.base = 0;
    vcpu.tr.limit = 0;
    vcpu.tr.flags = 0;
  } else {
    if (selector & 0x4) {
      LOG(PCALL, "Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
      raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    dt = &vcpu.gdt;
    index = selector & ~7;
    entry_limit = 7;
    if ((index + entry_limit) > dt->limit) {
      LOG(PCALL, "Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
      raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    ptr = dt->base + index;
    e1 = ldl_kernel(ptr);
    e2 = ldl_kernel(ptr + 4);
    type = (e2 >> DESC_TYPE_SHIFT) & 0xf;
    if ((e2 & DESC_S_MASK) ||
        (type != 1 && type != 9)) {
      LOG(PCALL, "Calling raise_exception_err(0x%x).\n", EXCP0D_GPF);
      raise_exception_err(EXCP0D_GPF, selector & 0xfffc);
    }
    if (!(e2 & DESC_P_MASK)) {
      LOG(PCALL, "Calling raise_exception_err(0x%x).\n", EXCP0B_NOSEG);
      raise_exception_err(EXCP0B_NOSEG, selector & 0xfffc);
    }
    vcpu.tr.base = get_seg_base(e1, e2);
    vcpu.tr.limit = get_seg_limit(e1, e2);
    vcpu.tr.flags = e2;
    e2 |= DESC_TSS_BUSY_MASK;
    stl_kernel(ptr + 4, e2);
  }
  vcpu.tr.selector = selector;
}

void
callout_restore_tr0_and_ltr_mem(long tr0, long segno)
{
  unsigned long val, memaddr;

	CALLOUT_INC_STATS();
  ASSERT(tr0 >= 0 && tr0 < NUM_REGS);
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  segcache_sync(segno);
  memaddr = vcpu.segs[segno].base + vcpu.regs[tr0];
  LOG(PCALL, "%s(%lu,%lu[0x%lx]) called.\n", __func__, tr0, segno, memaddr);
  vcpu.regs[tr0] = vcpu.temporaries[0];
  val = *((uint16_t *)memaddr);
  callout_ltr_val(val);
}

void
callout_restore_tr0_and_lgdt(long tr0, long segno)
{
  unsigned long memaddr;

	CALLOUT_INC_STATS();
	LOG(PCALL, "%s(): entry\n", __func__);
  ASSERT(tr0 >= 0 && tr0 < NUM_REGS);
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  segcache_sync(segno);
  memaddr = vcpu.segs[segno].base + vcpu.regs[tr0];
  LOG(PCALL, "%s(%lu,%lu[0x%lx]) called.\n", __func__, tr0, segno, memaddr);
  vcpu.regs[tr0] = vcpu.temporaries[0];
  vcpu.gdt.limit = *((uint16_t *)memaddr);
  vcpu.gdt.base = *((uint32_t *)(memaddr + 2));

  gdt_load(vcpu.gdt.base, vcpu.gdt.limit);
}

void
callout_restore_tr0_and_lidt(long tr0, long segno)
{
  unsigned long memaddr;

	CALLOUT_INC_STATS();
  ASSERT(tr0 >= 0 && tr0 < NUM_REGS);
  ASSERT(segno >= 0 && segno < NUM_SEGS);

  segcache_sync(segno);

  memaddr = vcpu.segs[segno].base + vcpu.regs[tr0];
  DBGn(PCALL, "%s(%lu,%lu[0x%lx]) called.\n", __func__, tr0, segno, memaddr);
  vcpu.regs[tr0] = vcpu.temporaries[0];

  vcpu.idt.limit = *((uint16_t *)memaddr);
  vcpu.idt.base = *((uint32_t *)(memaddr + 2));
}

static void
lcall_real(unsigned long new_cs, unsigned long new_eip, long size,
    unsigned long fallthrough_addr)
{
  uint32_t esp, esp_mask;
  target_ulong ssp;

  DBGn(PCALL, "%s(0x%lx,0x%lx,0x%lx,0x%lx) called.\n", __func__, new_cs,
      new_eip, size, fallthrough_addr);

  segcache_sync(R_SS);
  esp = vcpu.regs[R_ESP];
  esp_mask = get_sp_mask(vcpu.segs[R_SS].flags);
  ssp = vcpu.segs[R_SS].base;
  if (size == 4) {
    PUSHL(ssp, esp, esp_mask, vcpu.orig_segs[R_CS]);
    PUSHL(ssp, esp, esp_mask, fallthrough_addr);
  } else {
    ASSERT(size == 2);
    DBGn(PCALL, "%s(): pushing cs %#x to %#x.\n", __func__,
        vcpu.orig_segs[R_CS], esp);
    PUSHW(ssp, esp, esp_mask, vcpu.orig_segs[R_CS]);
    DBGn(PCALL, "%s(): pushing eip 0x%lx to 0x%x.\n", __func__,
        fallthrough_addr, esp);
    PUSHW(ssp, esp, esp_mask, fallthrough_addr);
  }
  SET_ESP(esp, esp_mask);
  vcpu.eip = (void *)new_eip;
  load_seg_cache(R_CS, new_cs, new_cs << 4, 0xffff, 0);
}

void
callout_lcall(unsigned long new_cs, unsigned long new_eip, long size,
    long fallthrough_addr)
{
	CALLOUT_INC_STATS();
  if (vcpu.cr[0] & CR0_PE_MASK) {
    NOT_IMPLEMENTED();
  } else {
    lcall_real(new_cs, new_eip, size, fallthrough_addr);
  }
}

static void
lret_real(long size, long popsize)
{
  uint32_t sp, sp_mask, new_eip, new_cs, ssp;

  segcache_sync(R_SS);
  sp = vcpu.regs[R_ESP];
  sp_mask = get_sp_mask(vcpu.segs[R_SS].flags);
  ssp = vcpu.segs[R_SS].base;
  if (size == 4) {
    POPL(ssp, sp, sp_mask, new_eip);
    POPL(ssp, sp, sp_mask, new_cs);
    new_cs &= 0xffff;
  } else {
    ASSERT(size == 2);
    POPW(ssp, sp, sp_mask, new_eip);
    DBGn(PCALL, "%s(): popped eip %#x from  %#x\n", __func__, new_eip, sp);
    POPW(ssp, sp, sp_mask, new_cs);
    DBGn(PCALL, "%s(): popped cs %#x from  %#x\n", __func__, new_cs, sp);
  }
  sp += popsize;
  DBGn(PCALL, "%s(): size=%ld, new_cs=%#x, new_eip=%#x\n", __func__, size,
      new_cs, new_eip);
  vcpu.regs[R_ESP] = (vcpu.regs[R_ESP] & ~sp_mask) | (sp & sp_mask);
  new_cs &= 0xffff;
  load_seg_cache(R_CS, new_cs, new_cs << 4, 0xffff, 0);
  vcpu.eip = (void *)new_eip;
}

void
callout_lret(unsigned long size, unsigned long popsize)
{
	CALLOUT_INC_STATS();
  if (vcpu.cr[0] & CR0_PE_MASK) {
    NOT_IMPLEMENTED();
  } else {
    lret_real(size, popsize);
  }
}

void
callout_ljmp(unsigned long new_cs, unsigned long new_eip)
{
  uint32_t e1, e2, cpl, dpl, rpl, limit;
	CALLOUT_INC_STATS();
  LOG(PCALL, "%s(0x%lx, 0x%lx) called.\n", __func__, new_cs, new_eip);
  if (vcpu.cr[0] & CR0_PE_MASK) {
    if ((new_cs & 0xfffc) == 0) {
      LOG(INT, "%s() %d: Raising 0D_GPF. new_cs[0x%lx]&0xfffc != 0.\n",
          __func__, __LINE__, new_cs);
      raise_exception_err(EXCP0D_GPF, 0);
    }
    if (!read_segment(&e1, &e2, new_cs, false, true)) {
      LOG(INT, "%s() %d: Raising 0D_GPF. read_segment(%lu) failed.\n",
          __func__, __LINE__, new_cs);
      raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
    }
    cpl = vcpu.orig_segs[R_CS] & 3;
    if (e2 & DESC_S_MASK) {
      if (!(e2 & DESC_CS_MASK)) {
        LOG(INT, "%s() %d: Raising 0D_GPF. !(e2[0x%x]&DESC_CS_MASK[%#x]).\n",
            __func__, __LINE__, e2, DESC_CS_MASK);
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
      }
      dpl = (e2 >> DESC_DPL_SHIFT) & 3;
      if (e2 & DESC_C_MASK) {
        /* conforming code segment */
        if (dpl > cpl) {
          LOG(INT, "%s() %d: Raising 0D_GPF. Conforming code seg. "
              "dpl[%d]>cpl[%d].\n", __func__, __LINE__, dpl, cpl);
          raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
      } else {
        /* non conforming code segment */
        rpl = new_cs & 3;
        if (rpl > cpl) {
          LOG(INT, "%s() %d: Raising 0D_GPF. Non-conforming code seg. "
              "rpl[%d]>cpl[%d].\n", __func__, __LINE__, rpl, cpl);
          raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
        if (dpl != cpl) {
          LOG(INT, "%s() %d: Raising 0D_GPF. Non-conforming code seg. "
              "dpl[%d] != cpl[%d].\n", __func__, __LINE__, dpl, cpl);
          raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
        }
      }
      if (!(e2 & DESC_P_MASK)) {
        LOG(INT, "%s() %d: Raising 0B_NOSEG. !(e2[%#x] & DESC_P_MASK[%#x]).\n",
            __func__, __LINE__, e2, DESC_P_MASK);
        raise_exception_err(EXCP0B_NOSEG, new_cs & 0xfffc);
      }
      limit = get_seg_limit(e1, e2);
      if (   new_eip > limit
          /* && !(env->hflags & HF_LMA_MASK)*/
          && !(e2 & DESC_L_MASK)) {
        LOG(INT, "%s() %d: Raising 0D_GPF. new_eip[0x%lx] > limit[0x%x] && "
            "!(e2[%#x] & DESC_L_MASK[%#x]).\n", __func__, __LINE__, new_eip,
            limit, e2, DESC_L_MASK);
        raise_exception_err(EXCP0D_GPF, new_cs & 0xfffc);
      }
      load_seg_cache(R_CS, (new_cs & 0xfffc) | cpl, get_seg_base(e1, e2),
          limit, e2);
      vcpu.eip = (void *)new_eip;
    } else {
      NOT_IMPLEMENTED();
    }
  } else {
    load_seg_cache(R_CS, new_cs, new_cs << 4, 0xffff, 0);
    vcpu.eip = (void *)new_eip;
  }
}

void
callout_restore_tr0_and_ljmp_indir(long tr0, long segno,
    int dsize)
{
  unsigned long memaddr;
  uint16_t new_cs = 0;
  uint32_t new_eip = 0;

	CALLOUT_INC_STATS();
  ASSERT(tr0 >= 0 && tr0 < NUM_REGS);
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  segcache_sync(segno);
  memaddr = vcpu.segs[segno].base + vcpu.regs[tr0];
  LOG(PCALL, "%s(%ld,%ld[0x%lx]) called.\n", __func__, tr0, segno, memaddr);
  vcpu.regs[tr0] = vcpu.temporaries[0];

  if (dsize == 2) {
    new_eip = *((uint16_t *)memaddr);
    new_cs = *((uint16_t *)(memaddr + 2));
  } else if (dsize == 4) {
    new_eip = *((uint32_t *)memaddr);
    new_cs = *((uint16_t *)(memaddr + 4));
  }
  callout_ljmp(new_cs, new_eip);
}

void
callout_restore_tr0_and_lcall_indir(long tr0, long segno,
    int dsize, unsigned long fallthrough_addr)
{
  unsigned long memaddr;
  uint16_t new_cs = 0;
  uint32_t new_eip = 0;

	CALLOUT_INC_STATS();
  ASSERT(tr0 >= 0 && tr0 < NUM_REGS);
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  segcache_sync(segno);
  memaddr = vcpu.segs[segno].base + vcpu.regs[tr0];
  LOG(PCALL, "%s(%lu,%lu[0x%lx]) called.\n", __func__, tr0, segno, memaddr);
  vcpu.regs[tr0] = vcpu.temporaries[0];

  if (dsize == 2) {
    new_eip = *((uint16_t *)memaddr);
    new_cs = *((uint16_t *)(memaddr + 2));
  } else if (dsize == 4) {
    new_eip = *((uint32_t *)memaddr);
    new_cs = *((uint16_t *)(memaddr + 4));
  }
  callout_lcall(new_cs, new_eip, 2, fallthrough_addr);
}

void
callout_real_mov_to_seg(unsigned long val, long segno)
{
  uint16_t selector;
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  
	CALLOUT_INC_STATS();
  selector = val & 0xffff;
  load_seg_cache(segno, selector, selector << 4, 0xffff, 0);
}

void
callout_real_mov_to_seg_restore_tr0(long tr0, long segno)
{
  uint16_t selector;

	CALLOUT_INC_STATS();
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  DBGn(PCALL, "%s(0x%lx,0x%lx) called: vcpu.regs[0x%lx]=%#x\n", __func__, tr0,
      segno, tr0, vcpu.regs[tr0]);
  
  selector = vcpu.regs[tr0] & 0xffff;
  vcpu.regs[tr0] = vcpu.temporaries[0];
  load_seg_cache(segno, selector, selector << 4, 0xffff, 0);
}

void
callout_real_lxs_restore_tr0(long tr0, long segno, long regno)
{
	CALLOUT_INC_STATS();
  ASSERT(regno >= 0 && regno < NUM_REGS);
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  ASSERT(tr0 >= 0 && tr0 < NUM_REGS);
  uint16_t selector;
  int regval;
  long memaddr = vcpu.regs[tr0];
  vcpu.regs[tr0] = vcpu.temporaries[0];
  regval = *((uint16_t *)memaddr);
  selector = *((uint16_t *)(memaddr + 2));
  load_seg_cache(segno, selector, selector << 4, 0xffff, 0);
  vcpu.regs[regno] = regval;
}

void
callout_real_pop_seg(long segno)
{
  target_ulong ssp;
  uint16_t sp, selector;

	CALLOUT_INC_STATS();
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  LOG(PCALL, "%s() called.\n", __func__);

  segcache_sync(R_SS);
  ssp = vcpu.segs[R_SS].base;
  sp = vcpu.regs[R_ESP];
  selector = *((uint16_t *)(ssp + sp));
  sp += 2;
  vcpu.regs[R_ESP] = (vcpu.regs[R_ESP] & 0xffff0000) | sp;

  load_seg_cache(segno, selector, selector << 4, 0xffff, 0);
}

void
callout_real_push_seg(long segno)
{
  target_ulong ssp;
  uint16_t sp;

	CALLOUT_INC_STATS();
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  LOG(PCALL, "%s(%ld) called. pushing %#x [esp=%#x]\n", __func__, segno,
      (uint32_t)vcpu.segs[segno].base >> 4, vcpu.regs[R_ESP]);

  segcache_sync(R_SS);
  ssp = vcpu.segs[R_SS].base;
  sp = vcpu.regs[R_ESP];
  sp -= 2;
  vcpu.regs[R_ESP] = (vcpu.regs[R_ESP] & 0xffff0000) | sp;
  *((uint16_t *)(ssp + sp)) = (uint16_t)(vcpu.segs[segno].base >> 4);
}

void
callout_real_mov_seg_to_reg(long segno, long regno)
{
  target_ulong ssp;
  uint16_t sp;

	CALLOUT_INC_STATS();
  ASSERT(segno >= 0 && segno < NUM_SEGS);
  ASSERT(regno >= 0 && regno < NUM_REGS);
  LOG(PCALL, "%s(%ld(%#x),%ld) called. \n", __func__, segno,
      (uint32_t)vcpu.segs[segno].base >> 4, regno);

  vcpu.regs[regno] = (vcpu.regs[regno] & 0xffff0000) |
    (uint16_t)(vcpu.segs[segno].base >> 4);
}

void
callout_real_mov_seg_to_mem_restore_tr0(long src_segno,
    long target_segno, long tr0)
{
  long memaddr;

	CALLOUT_INC_STATS();
  ASSERT(src_segno >= 0 && src_segno < NUM_SEGS);
  ASSERT(target_segno >= 0 && target_segno < NUM_SEGS);
  ASSERT(tr0 >= 0 && tr0 < NUM_REGS);
  LOG(PCALL, "%s(%ld(%#x),%ld,%ld) called. \n", __func__, src_segno,
      (uint32_t)vcpu.segs[src_segno].base >> 4, target_segno, tr0);

  segcache_sync(src_segno);
  segcache_sync(target_segno);
  memaddr = vcpu.segs[target_segno].base + vcpu.regs[tr0];
  vcpu.regs[tr0] = vcpu.temporaries[0];
  *((uint16_t *)memaddr) = (uint16_t)(vcpu.segs[src_segno].base >> 4);
}


void
callout_iret(void)
{
	CALLOUT_INC_STATS();
  if ((vcpu.cr[0] & CR0_PE_MASK) == 0) {
    iret_real();
    /*
    char *addr = (char *)(vcpu.segs[R_DS].base + (vcpu.regs[R_ESI] & 0xffff));
    printf("%s() called: vcpu.eflags=0x%x, eax=0x%x, ds=%hx,si=%x: "
        "cylinders=%x, heads=%x, spt=%x, "
        "nb_sectors=%llx\n", __func__, vcpu.eflags, vcpu.regs[R_EAX],
        vcpu.segs[R_DS].base, vcpu.regs[R_ESI],
        *(uint32_t*)(addr + 4), *(uint32_t*)(addr+8), *(uint32_t*)(addr+12),
        *(uint64_t*)(addr+16));
        */
  } else {
    iret_protected();
  }
}

void
callout_hlt(void)
{
	CALLOUT_INC_STATS();
	if (vcpu.replay_log) {
		return;
	}
	if (vcpu.IF == 0) {
		shutdown_final_rites();
	}
	/* XXX */
	if (vcpu.IF == 2) {
		vcpu.IF = 1;
	}
	/* XXX */
  vcpu.halted = 1;
  while (vcpu.halted) {
    mode_t mode;
    mode = switch_to_kernel();
    asm("hlt");
    switch_mode(mode);
  }
}

/*
void
callout_sti(long fallthrough_addr)
{
	CALLOUT_INC_STATS();
	if (!vcpu.IF) {
		uint8_t *ptr1, *ptr2;
		//vcpu.sti_fallthrough = fallthrough_addr;
		if (vcpu.callout_next) {
			scan_next_insn((char *)vcpu.callout_next + 1, &ptr1, &ptr2);
			apply_fcallout_patch(ptr1, ptr2);
		}
		vcpu.IF = 1;
	}
	ASSERT(vcpu.IF == 1);
}
*/
void
callout_nop(void)
{
	CALLOUT_INC_STATS();
}

void
callout_invd(void)
{
  //flush translation cache.
	CALLOUT_INC_STATS();
  NOT_IMPLEMENTED();
}

void
callout_real_movs(size_t data_size, unsigned long prefix)
{
	CALLOUT_INC_STATS();
  NOT_IMPLEMENTED();
}

void
callout_real_stos(size_t data_size, unsigned long prefix)
{
	CALLOUT_INC_STATS();
  NOT_IMPLEMENTED();
}

void
callout_real_lods(size_t data_size, unsigned long prefix)
{
	CALLOUT_INC_STATS();
  NOT_IMPLEMENTED();
}

void
callout_real_scas(size_t data_size, unsigned long prefix)
{
	CALLOUT_INC_STATS();
  NOT_IMPLEMENTED();
}

void
callout_real_cmps(size_t data_size, unsigned long prefix)
{
	CALLOUT_INC_STATS();
  NOT_IMPLEMENTED();
}

void
forced_callout (struct intr_frame *f)
{
  ASSERT(read_cpl() != 3);

	stats_num_forced_callouts++;
  clear_fcallout_patches();
  f->eip = (void *)((uint8_t *)f->eip - 2);

  //printf("%s() %d:\n", __func__, __LINE__);
  if (tb_is_tc_boundary((void *)f->eip)) {
    intr_frame_2_vcpu(f);
    vcpu.callout_next = (void *)f->eip;

    /* do not access any stack local variable beyond this point. */
    reset_stack();
		intr_enable();
    switch_to_user();
		restore_monitor_flags();
    restore_monitor();
    longjmp(vcpu.jmp_env, 2);
  } else {
    uint8_t const *tc_ptr = (void *)f->eip;
    insn_t insn;
    size_t len;

    /* always use size 4 for translated code. */
    len = disas_insn(tc_ptr, tc_ptr, &insn, 4, false);
    if (insn_is_indirect_jump(&insn)) {
      bool ret;
      DBGn(PCALL, "forced callout at a jump indirect insn.\n");
      ret = guest_handle_exception(f, CPU_CONSTRAINT_FORCED_CALLOUT);
      if (!ret) {
        printf("PANIC: Guest failed to handle exception for instruction: ");
        print_insn(&insn);
        printf("\n");
        PANIC("");
      }
    } else {
      uint8_t *ptr1, *ptr2;
      DBGn(PCALL, "forced callout in the middle of an instruction. scanning "
          "again at %p.\n", tc_ptr);
      scan_next_insn(tc_ptr, &ptr1, &ptr2);
      DBGn(PCALL, "ptr1=%p,ptr2=%p\n", ptr1, ptr2);
      apply_fcallout_patch(ptr1, ptr2);
    }
  }
}

void
gpf_load_seg(long segno, long descno_val)
{
  uint16_t descno = descno_val;
  uint32_t e1, e2;
  bool rdseg;

  LOG(PCALL, "%s(%#lx,%#hx) called.\n", __func__, segno, descno);

  /* XXX: check for permissions. */
  if (descno >= SEL_BASE) {
    NOT_IMPLEMENTED();
  } else {
    vcpu.segs[segno].selector = descno;
  }
  rdseg = read_segment(&e1, &e2, descno, false, true);
  if (!rdseg) {
    NOT_IMPLEMENTED();
  }
  //printf("%s(): e1=%08x, e2=%08x\n", __func__, e1, e2);
  vcpu.segs[segno].base = get_seg_base(e1, e2);
  vcpu.segs[segno].limit = get_seg_limit(e1, e2);
  vcpu.segs[segno].flags = e2;
  gdt_make_shadow_segdesc(segno);
  /* printf("%s(): base=%08x, limit=%08x\n", __func__, vcpu.segs[segno].base,
      vcpu.segs[segno].limit); */
}

/*
void
gpf_real_read_memw_to_regw(long addr, long regno)
{
  long memaddr;
  uint32_t val, e1, e2, gs = 0, base;
   //This can be the only reason for this #GPF
  ASSERT((unsigned long)addr >= LOADER_MONITOR_VIRT_BASE);
  ASSERT(vcpu.intr_frame);
  gs = vcpu.intr_frame->gs;
  read_segment(&e1, &e2, gs, true, false);
  base = get_seg_base(e1, e2);
  memaddr = base + addr;
  printf("gs = 0x%x, e1 = 0x%x, e2 = 0x%x, base = 0x%x, addr=0x%x, "
      "memaddr=0x%x. vcpu.default_user_gs=0x%x, monitor.eip=0x%x\n", gs, e1,
      e2, base, addr, memaddr, vcpu.default_user_gs, monitor.eip);
  ASSERT(memaddr >= 0);
  val = vcpu.regs[regno];
  vcpu.regs[regno] = (val & 0xffff0000) | *((uint16_t *)memaddr);
}
*/


static bool guest_handles_interrupt = false;
void
intr_guest_init(void)
{
  guest_handles_interrupt = true;
  unregister_handler(0x20);     /* timer */
  unregister_handler(0x21);     /* keyboard */
}

static void *
vcpu_exception_rollback(struct intr_frame *f)
{
  unsigned i;
	int cur_pos;
	void (*eip)(void);
  tb_t *tb;

	eip = f->eip;
  if (!(tb = tb_find(eip))) {
		return NULL;
    ERR("tb_find(%p) returned NULL.\n", eip);
    NOT_REACHED();
  }
  cur_pos = -1;
  for (i = 0; i < tb->num_insns; i++) {
    if (tb->tc_ptr + tb->tc_boundaries[i + 1] > (uint8_t *)eip) {
      if (tb->rollbacks[i].buf) {
        int j;
        for (j = 0; j < tb->rollbacks[i].nb_rollbacks; j++) {
          if (tb->tc_ptr + tb->tc_boundaries[i] +
              tb->rollbacks[i].code_offset[j] >= (uint8_t *)eip) {
						size_t rb_off = tb->rollbacks[i].rb_offset[j];
            LOG(INT, "Rolling back 0x%x[%p].\n",
                tb->eip_virt + ((i == 0)?0:tb->eip_boundaries[i-1]), eip);
						execute_code_in_intr_frame_context(f, tb->rollbacks[i].buf + rb_off,
								tb->rollbacks[i].buf_size - rb_off);
							/*
            mode_t mode;
            vcpu.func_tc_ptr = tb->rollbacks[i].buf
              + tb->rollbacks[i].rb_offset[j];
            vcpu.func_tc_done = &&done_rollback;
            mode = switch_to_user();
            asm("jmp *%%gs:%0" : : "m"(vcpu.tc_label));
done_rollback:
            switch_mode(mode);
						*/
            break;
          }
        }
      }
      ASSERT(tb->tc_ptr + tb->tc_boundaries[i] <= (uint8_t *)eip);
      LOG(INT, "%d: eip=%p, i=%d, num_insns=%dtb->tc_ptr=%p,%p,%p\n", __LINE__,
          eip, i, tb->num_insns, tb->tc_ptr, tb->tc_ptr + tb->tc_boundaries[i],
          tb->tc_ptr + tb->tc_boundaries[tb->num_insns]);
      return tb->tc_ptr + tb->tc_boundaries[i];
    }
  }
  LOG(INT, "eip=%p, tb->tc_ptr=%p,%p\n", eip, tb->tc_ptr,
      tb->tc_ptr + tb->tc_boundaries[tb->num_insns - 1]);
  NOT_REACHED();
}

bool
guest_intr_handler(struct intr_frame *frame)
{
  bool external;
  //uint32_t *esp;
  static volatile int vec_no, error_code;

  //mdebug_stop();
  ASSERT(thread_current() == running_thread());
  external = frame->vec_no >= 0x20 && frame->vec_no < 0x30;

  if (!external) {
    ASSERT(frame->vec_no != FORCED_CALLOUT);
		if (frame->gs != SEL_GDSEG && !tb_find(frame->eip)) {
			printf("frame->vec_no = %x, frame->eip=%p\n", frame->vec_no, frame->eip);
		}
    ASSERT(frame->gs == SEL_GDSEG || tb_find(frame->eip));
    vec_no = frame->vec_no;
    error_code = frame->error_code;
    if (vec_no == EXCP0E_PAGE && (error_code & PF_U)) {
      int cpl;
      cpl = vcpu.orig_segs[R_CS] & 3;
			if (cpl != 3) {
				error_code &= ~PF_U;
			}
    }
		if (frame->gs != SEL_GDSEG) {
			ASSERT(tb_find(frame->eip));
			vcpu.callout_next = vcpu_exception_rollback(frame);
			intr_frame_2_vcpu(frame);
		} else {
			vcpu.eip = vcpu.eip_executing;
			vcpu.callout_next = vcpu.callout_cur;
		}
		if (vcpu.record_log || vcpu.replay_log) {
			vcpu.n_exec = get_n_exec(vcpu.callout_next);
		}
		/*
		MSG ("%s(): raising exception at eip %p, n_exec %llx\n", __func__,
				vcpu.eip, vcpu.n_exec);
				*/

    /* after reset_stack(), local variables are inaccessible. */
    reset_stack();
		intr_enable();
    switch_to_user();
		restore_monitor_flags();
    restore_monitor();
    raise_exception_err(vec_no, error_code);
    NOT_REACHED();
  }
  if (!guest_handles_interrupt) {
    return false;
  }
  ASSERT(intr_get_level() == INTR_OFF);

	if (frame->vec_no == 0x20) {
		guest_tick();
	}
  if (vcpu.halted) {
    vcpu.halted = 0;
  }
	if (vcpu.replay_log) {
		return true;
	}

	if (!(vcpu.interrupt_request & CPU_INTERRUPT_HARD)) {
		cpu_interrupt(CPU_INTERRUPT_HARD);
		//printf("registering interrupt 0x%x at %p.\n", frame->vec_no, frame->eip);
		if (vcpu.IF == 1) {
			struct tb_t *tb;
			if (!fcallout_patch_exists() && (tb = tb_find(frame->eip))) {
				uint8_t *ptr1, *ptr2;
				scan_next_insn((void *)frame->eip, &ptr1, &ptr2);
				if (ptr1 && !fcallout_already_patched(ptr1)) {
					apply_fcallout_patch(ptr1, ptr2);
				}
			}
		}
	}

	pic_set_irq(&vcpu.isa_pic, frame->vec_no - 0x20, 1);

  return true;
}

void
execute_code_in_intr_frame_context(struct intr_frame *f, uint8_t *code,
		size_t len)
{
	uint8_t *tpage = vcpu.func_struct[vcpu.func_depth].func_tc_buf;
	enum intr_level intr_level;
	size_t tlen = len;

	ASSERT(tlen < MAX_FUNC_CODESIZE);
	memcpy(tpage, code, tlen);

	tlen += emit_jump_indir_insn((uint8_t *)tpage + tlen,
			(target_ulong)&vcpu.func_monitor_eip);
	ASSERT(tlen < MAX_FUNC_CODESIZE);

	vcpu.func_struct[vcpu.func_depth].func_tc_done = &&done_tc;
	memcpy(&vcpu.func_struct[vcpu.func_depth].func_intr_frame,
			f, sizeof (struct intr_frame));
	vcpu.func_struct[vcpu.func_depth].last_monitor_context = last_monitor_context;
	/* Need to disable interrupts, because func_stack is used. */
	intr_level = intr_disable();
	vcpu.func_struct[vcpu.func_depth].mode = switch_to_user();
	barrier();
	asm("jmp *%%gs:%0" : : "m"(vcpu.tc_label));
done_tc:
	barrier();
	switch_mode(vcpu.func_struct[vcpu.func_depth].mode);
	intr_set_level(intr_level);
	last_monitor_context = vcpu.func_struct[vcpu.func_depth].last_monitor_context;
	memcpy(f, &vcpu.func_struct[vcpu.func_depth].func_intr_frame,
			sizeof (struct intr_frame));
	return;
}

bool
guest_handle_exception(struct intr_frame *f, cpu_constraints_t cpu_constraints)
{
  static uint8_t tpage[PGSIZE];
  size_t tpage_size = PGSIZE;
  uint8_t *ptr, *ptr_next;
  static insn_t insn;
  size_t tlen, size;

  if (vcpu.cr[0] & CR0_PE_MASK) {
    cpu_constraints |= CPU_CONSTRAINT_PROTECTED;
  } else {
    cpu_constraints |= CPU_CONSTRAINT_REAL;
  }
  size = 4;   /* size is always 4 because the translated code is always
                 running in 32-bit mode. */

  ptr = (void *)f->eip;
  ptr_next = ptr + disas_insn(ptr, f->eip, &insn, size, false);

	/*
  if (loglevel & VCPU_LOG_EXCP) {
		LOG(EXCP, "#EXCP IN:\n");
		print_asm(ptr, ptr_next - ptr, 4);
	}
	*/

  if (tlen = peep_translate(tpage, tpage_size, &insn, 1, NULL, NULL, NULL,
        NULL, NULL, NULL, (target_ulong)ptr, (target_ulong)ptr_next,
				false, &cpu_constraints, NULL)) {
		/*
		if (loglevel & VCPU_LOG_EXCP) {
			LOG(EXCP, "#EXCP OUT:\n");
			print_asm(tpage, tlen, 4);
		}
		*/
		execute_code_in_intr_frame_context(f, tpage, tlen);
		return true;
  }
  return false;
}

void
handle_pending_interrupts(uint8_t *tc_ptr)
{
  int intno;
  enum intr_level level;

  if (vcpu.IF != 1) {
		//printf("%s() %d:\n", __func__, __LINE__);
    return;
  }
	/*
  if ((target_ulong)vcpu.eip == vcpu.sti_fallthrough) {
		//printf("%s(): sti fallthrough return.\n", __func__);
    return;
  } else {
    vcpu.sti_fallthrough = 0;
  }
	*/

  level = intr_disable();
  if (vcpu.interrupt_request & CPU_INTERRUPT_HARD) {
    intno = pic_read_irq(&vcpu.isa_pic);
    if (!pic_is_spurious_interrupt(&vcpu.isa_pic, intno)) {
      //pic_set_irq(intno, 0);
      intr_set_level(level);
      //printf("%p: raising interrupt %#x\n", vcpu.eip, intno);
      if (vcpu.record_log || vcpu.replay_log) {
        vcpu.n_exec = get_n_exec(vcpu.callout_next);
      }
      raise_interrupt(intno, 0, -1, 0);
      NOT_REACHED();
    } else {
			//printf("resetting interrupt.\n");
      vcpu.interrupt_request &= ~CPU_INTERRUPT_HARD;
    }
  }
	//printf("%s() %d:\n", __func__, __LINE__);
  intr_set_level(level);
}

#define vcpu_2_fm(fm) do {																										 \
  fm->eax = vcpu.regs[R_EAX];																									 \
  fm->ecx = vcpu.regs[R_ECX];                                                  \
  fm->edx = vcpu.regs[R_EDX];                                                  \
  fm->ebx = vcpu.regs[R_EBX];                                                  \
  fm->esp = (void *)vcpu.regs[R_ESP];                                          \
  fm->ebp = vcpu.regs[R_EBP];                                                  \
  fm->esi = vcpu.regs[R_ESI];                                                  \
  fm->edi = vcpu.regs[R_EDI];                                                  \
  fm->es = vcpu.segs[R_ES].selector;                                           \
  fm->ss = vcpu.segs[R_SS].selector;                                           \
  fm->ds = vcpu.segs[R_DS].selector;                                           \
  fm->fs = vcpu.segs[R_FS].selector;                                           \
  fm->eflags = vcpu.eflags | IF_MASK;                                          \
} while (0)

#define fm_2_vcpu(fm) do {																										 \
  vcpu.regs[R_EAX] = fm->eax;																									 \
  vcpu.regs[R_ECX] = fm->ecx;											 														 \
  vcpu.regs[R_EDX] = fm->edx;                                                  \
  vcpu.regs[R_EBX] = fm->ebx;                                                  \
  vcpu.regs[R_ESP] = (target_ulong)fm->esp;                                    \
  vcpu.regs[R_EBP] = fm->ebp;                                                  \
  vcpu.regs[R_ESI] = fm->esi;                                                  \
  vcpu.regs[R_EDI] = fm->edi;                                                  \
  vcpu.segs[R_ES].selector  = fm->es;                                          \
  /*vcpu.segs[R_CS].selector = fm->cs;*/                                       \
  vcpu.segs[R_SS].selector  = fm->ss;                                          \
  vcpu.segs[R_DS].selector  = fm->ds;                                          \
  vcpu.segs[R_FS].selector  = fm->fs;                                          \
  /*vcpu.segs[R_GS].selector  = fm->gs;*/                                      \
  vcpu.eflags = (fm->eflags & ~IF_MASK) | (vcpu.eflags & IF_MASK);             \
} while (0)

void
vcpu_2_intr_frame(struct intr_frame *f)
{
	vcpu_2_fm(f);
}

void
vcpu_2_monitor(struct monitor_t *m)
{
	vcpu_2_fm(m);
}

void
intr_frame_2_vcpu(struct intr_frame *f)
{
  struct tb_t *tb;
  tb = tb_find(f->eip);
	ASSERT(tb);
	vcpu.eip = (void *)tb_tc_ptr_to_eip_virt(f->eip);
	ASSERT(vcpu.eip);
	fm_2_vcpu(f);
}

void
monitor_2_vcpu(struct monitor_t *m)
{
	fm_2_vcpu(m);
}

#define copy_field(field, dst, src) dst->field = src->field
#define copy_seg_m2i(field, dst, src) do {	\
	if (dst->field != SEL_KDSEG) {						\
		dst->field = src->field;								\
	}																					\
} while (0)
void
monitor_2_intr_frame(struct intr_frame *f, struct monitor_t *m)
{
	copy_field(eax, f, m);
	copy_field(ecx, f, m);
	copy_field(edx, f, m);
	copy_field(ebx, f, m);
	copy_field(ebp, f, m);
	copy_field(esi, f, m);
	copy_field(edi, f, m);
	copy_field(eflags, f, m);
	copy_seg_m2i(es, f, m);
	copy_seg_m2i(fs, f, m);
	copy_seg_m2i(ds, f, m);
	if ((f->cs & 0x3) > 0) {
		copy_seg_m2i(ss, f, m);
		copy_field(esp, f, m);
	}
}
#define copy_seg_i2m(field, dst, src) do {	\
	if (src->field == SEL_KDSEG) {						\
		NOT_REACHED();													\
		dst->field = SEL_UDSEG;									\
	} else {																	\
		dst->field = src->field;								\
	}																					\
} while (0)
void
intr_frame_2_monitor(struct monitor_t *m, struct intr_frame *f)
{
	copy_field(eax, m, f);
	copy_field(ecx, m, f);
	copy_field(edx, m, f);
	copy_field(ebx, m, f);
	copy_field(ebp, m, f);
	copy_field(esi, m, f);
	copy_field(edi, m, f);
	copy_field(eflags, m, f);
	copy_seg_i2m(es, m, f);
	copy_seg_i2m(fs, m, f);
	copy_seg_i2m(ds, m, f);
	if ((f->cs & 0x3) > 0) {
		copy_seg_i2m(ss, m, f);
		copy_field(esp, m, f);
	} else {
		NOT_REACHED();
		m->ss = SEL_UDSEG;
		m->esp = 0;			/* esp should not be used. */
	}
}

void
callout_print_stats(void)
{
	printf("MON-STATS: callouts: %lld all, %lld forced, %lld sti\n",
			stats_num_callouts, stats_num_forced_callouts, stats_num_sti_callouts);
}
