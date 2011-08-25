#include <stdio.h>
#include <setjmp.h>
#include "devices/disk.h"
#include "sys/interrupt.h"
#include "sys/io.h"
#include "hw/bdrv.h"
#include "sys/flags.h"
#include "mem/vaddr.h"
#include "mem/paging.h"
#include "mem/pt_mode.h"
#include "peep/tb.h"
#include "sys/bootsector.h"
#include "sys/monitor.h"
#include "sys/rr_log.h"
#include "sys/vcpu.h"

monitor_t monitor, *last_monitor_context;
vcpu_t vcpu;
uint32_t *phys_map;
struct BlockDriverState *hda_bdrv, *hdb_bdrv, swap_bdrv, rr_log_init_dump_bdrv;

target_ulong
vcpu_get_eip(void)
{
  return (target_ulong)(vcpu.segs[R_CS].base + (uint32_t)vcpu.eip);
}

bool
vcpu_equal(vcpu_t const *cpu1, vcpu_t const *cpu2)
{
#define _compare_field(f, fmt, args...) do {                      \
  if (cpu1->f != cpu2->f) {                                       \
    printf("%s(): mismatch on " fmt ": %#x<->%#x\n", __func__,\
      ##args, (uint32_t)cpu1->f, (uint32_t)cpu2->f);              \
    return false;                                                 \
  }                                                               \
} while(0)

#define compare_field(f) do {       \
  _compare_field(f, #f);            \
} while(0)

#define compare_seg(s) do {             \
  compare_field(orig_##s);              \
  compare_field(s.base);                \
  compare_field(s.limit);               \
  compare_field(s.flags);               \
} while(0)

  //compare_field(n_exec);
  compare_field(eip);
  compare_field(regs[0]);
  compare_field(regs[1]);
  compare_field(regs[2]);
  compare_field(regs[3]);
  compare_field(regs[4]);
  compare_field(regs[5]);
  compare_field(regs[6]);
  compare_field(regs[7]);
  compare_seg(segs[0]);
  compare_seg(segs[1]);
  compare_seg(segs[2]);
  compare_seg(segs[3]);
  compare_seg(segs[4]);
  compare_seg(segs[5]);

  return true;
}

uint64_t
get_n_exec(const void *tc_ptr)
{
  tb_t const *tb;
  int cur_pos;
	unsigned i;
  ASSERT(vcpu.record_log || vcpu.replay_log);

  if (!tc_ptr || !(tb = tb_find(tc_ptr))) {
    /* this can only happen, if we are at the end of a tb. */
    return vcpu.n_exec;
  }
  if (tc_ptr == tb->tc_ptr) {
    return vcpu.n_exec;
  }
  cur_pos = -1;
  for (i = 0; i < tb->num_insns; i++) {
    if (tb->tc_ptr + tb->tc_boundaries[i] >= (uint8_t *)tc_ptr) {
      cur_pos = i;
      break;
    }
  }
  if (cur_pos == -1) {
    printf("tc_ptr=%p\n", tc_ptr);
    for (i = 0; i <= tb->num_insns; i++) {
      printf("tb->tc_ptr[%d]=%p\n", i, tb->tc_ptr + tb->tc_boundaries[i]);
    }
  }
  ASSERT(cur_pos != -1);
  ASSERT(vcpu.n_exec >= tb->num_insns);
  return (vcpu.n_exec - tb->num_insns + cur_pos);
}

void
cpu_interrupt(int mask)
{
  //printf("%s(0x%x) called.\n", __func__, __LINE__);
	ASSERT(!vcpu.replay_log);
	vcpu.interrupt_request |= mask;
}

void
cpu_reset_interrupt(int mask)
{
	ASSERT(!vcpu.replay_log);
  vcpu.interrupt_request &= ~mask;
}

uint32_t
ldl_kernel_dont_set_access_bit(target_ulong vaddr)
{
  target_phys_addr_t paddr;
	/*
  pt_mode_t pt_mode;
  uint32_t ret;
	*/

  paddr = pt_walk((void *)vcpu.cr[3], vaddr, NULL, NULL, 0);
  if (paddr == PDE_ERR || paddr == PTE_ERR) {
    NOT_IMPLEMENTED();
    //raise_exception();
  }
	/*
  pt_mode = switch_to_phys();
  ret = *(uint32_t *)paddr;
  switch_pt(pt_mode);
  return ret;
	*/
	return ldl_phys(paddr);
}


int
vcpu_get_privilege_level(void)
{
	return ((vcpu.cr[0] & CR0_PE_MASK) && (vcpu.orig_segs[R_CS] & 3)==3)?1:0;
}
