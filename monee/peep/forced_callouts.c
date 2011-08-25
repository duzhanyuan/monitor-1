#include "peep/forced_callouts.h"
#include <stdlib.h>
#include <stdio.h>
#include <types.h>
#include <string.h>
#include "peep/insntypes.h"
#include "peep/tb.h"
#include "peep/tb_trace.h"
#include "sys/vcpu.h"
#include "sys/monitor.h"
#include "sys/interrupt.h"

typedef struct fcallout_patch_t {
  uint8_t *ptr1, *ptr2;
  uint8_t b0, b1, b2, b3;
} fcallout_patch_t;

typedef struct fcallout_patch_pending_t {
  target_ulong eip_phys, eip_virt;
  bool use_next_insn;
} fcallout_patch_pending_t;

#define MAX_CALLOUT_PATCHES 1
fcallout_patch_t fcallout_patches[MAX_CALLOUT_PATCHES];
size_t num_fcallout_patches = 0;
fcallout_patch_pending_t fcallout_patches_pending[MAX_CALLOUT_PATCHES];
size_t num_fcallout_patches_pending = 0;


static void fcallout_patches_tb_free(tb_t *tb);

void
clear_fcallout_patches(void)
{
  unsigned i;
  ASSERT(num_fcallout_patches <= 1);
  for (i = 0; i < num_fcallout_patches; i++) {
    tb_t *tb;
    ASSERT(fcallout_patches[i].ptr1);
    *(fcallout_patches[i].ptr1) = fcallout_patches[i].b0;
    *(fcallout_patches[i].ptr1 + 1) = fcallout_patches[i].b1;
    if (fcallout_patches[i].ptr2) {
      *(fcallout_patches[i].ptr2) = fcallout_patches[i].b2;
      *(fcallout_patches[i].ptr2 + 1) = fcallout_patches[i].b3;
    }
    tb = tb_find(fcallout_patches[i].ptr1);
    ASSERT(tb);
    tb_trace_free_remove(tb, fcallout_patches_tb_free);
    if (fcallout_patches[i].ptr2) {
      tb = tb_find(fcallout_patches[i].ptr2);
      ASSERT(tb);
      tb_trace_free_remove(tb, fcallout_patches_tb_free);
    }
  }
  num_fcallout_patches = 0;
  num_fcallout_patches_pending = 0;
}

static bool
insn_is_forced_callout(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
  if (!strcmp(opc, "int")) {
    ASSERT(insn->op[0].type == op_imm);
    if (insn->op[0].val.imm == FORCED_CALLOUT) {
      return true;
    }
  }
  return false;
}

void
scan_next_insn(uint8_t const *tc_ptr, uint8_t const **ptr1,
    uint8_t const **ptr2)
{
  size_t len;
  insn_t insn;
  uint8_t const *tc_next;
  tb_t *tb;

  tb = tb_find(tc_ptr);
	if (!tb) {
		printf("tc_ptr=%p\n", tc_ptr);
	}
  ASSERT(tb);
  tc_next = tb_get_tc_next(tb, tc_ptr);
  ASSERT(tc_next <= tb->tc_ptr + tb->tc_boundaries[tb->num_insns]);
  while (tc_ptr < tc_next) {
    len = disas_insn(tc_ptr, tc_ptr, &insn, 4, false);
    if (insn_is_forced_callout(&insn)) {
      /* There is already a forced callout coming up. do nothing. */
      *ptr1 = *ptr2 = NULL;
      return;
    }
    if (insn_is_indirect_jump(&insn)) {
      ASSERT(insn.op[0].type == op_mem);
      if (   insn.op[0].val.mem.base  == -1
          && insn.op[0].val.mem.index == -1
          && insn.op[0].val.mem.segtype == segtype_sel
          && insn.op[0].val.mem.seg.sel == R_GS
          && insn.op[0].val.mem.disp == (uint32_t)&monitor.eip) {
        /* this is a callout, do nothing. */
        *ptr1 = *ptr2 = NULL;
        return;
      } else {
        /* Patch indirect jump, so that later the target can be determined
         * and appropriately patched. */
        /*
        printf("Patching indirect jump at %p. op[0].type=%d, op[0].val."
				    "mem.base=%d, op[0].val.mem.index=%d, "
            "op[0].val.mem.segtype=%d, op[0].val.mem.seg.sel=%d, "
						"op[0].val.mem.disp=%llx, &monitor.eip=%p\n", tc_ptr,
            insn.op[0].type, insn.op[0].val.mem.base, insn.op[0].val.mem.index,
						insn.op[0].val.mem.segtype, insn.op[0].val.mem.seg.sel,
						insn.op[0].val.mem.disp, &monitor.eip);
            */
        *ptr1 = tc_ptr;
        *ptr2 = NULL; 
        return;
      }
    }
    if (insn_is_direct_jump(&insn)) {
      uint8_t *target;
      ASSERT(insn.op[0].type == op_imm);
      target = (uint8_t *)(uint32_t)insn.op[0].val.imm;
      *ptr1 = (void *)target;
      if (insn_is_conditional_jump(&insn)) {
        *ptr2 = tc_ptr + len;
      } else {
        *ptr2 = NULL;
      }
      return;
    }
    tc_ptr += len;
  }
  ASSERT(tc_next < tb->tc_ptr + tb->tc_boundaries[tb->num_insns]);
  *ptr1 = tc_next;
  *ptr2 = NULL;
  return;
}

bool
fcallout_already_patched(uint8_t const *tc_ptr)
{
  unsigned i;
  for (i = 0; i < num_fcallout_patches; i++) {
    if (   tc_ptr == fcallout_patches[i].ptr1
        || tc_ptr == fcallout_patches[i].ptr2) {
      return true;
    }
  }
  return false;
}

static bool
fcallout_already_pending(target_ulong eip_phys, target_ulong eip_virt,
    bool use_next_insn)
{
  unsigned i;
  for (i = 0; i < num_fcallout_patches_pending; i++) {
    if (   eip_phys == fcallout_patches_pending[i].eip_phys
        && eip_virt == fcallout_patches_pending[i].eip_virt
        && use_next_insn == fcallout_patches_pending[i].use_next_insn) {
      return true;
    }
  }
  return false;
}

static bool
pc_belongs_to_tb(target_ulong eip_phys, target_ulong eip_virt, tb_t const *tb)
{
  /*
  printf("eip_virt=%#x, tb->eip_virt=%#x, tb->tb_len=%#x\n", eip_virt,
      tb->eip_virt, tb->tb_len);
      */
  return (eip_phys >= tb->eip_phys && eip_phys < tb->eip_phys + tb->tb_len
      && eip_virt >= tb->eip_virt && eip_virt < tb->eip_virt + tb->tb_len);
}

static bool
tc_belongs_to_tb(uint8_t *tcptr, tb_t const *tb)
{
  return (   tcptr >= tb->tc_ptr
          && tcptr < tb->tc_ptr + tb->tc_boundaries[tb->num_insns]);
}

static void
fcallout_patches_tb_add(tb_t *tb)
{
  uint8_t const *ptr1 = NULL, *ptr2 = NULL;
  int deleted = -1;
  unsigned i;

  for (i = 0; i < num_fcallout_patches_pending; i++) {
    if (pc_belongs_to_tb(fcallout_patches_pending[i].eip_phys,
          fcallout_patches_pending[i].eip_virt, tb)) {
      unsigned inum;
      for (inum = 0; inum < tb->num_insns; inum++) {
        if (fcallout_patches_pending[i].eip_virt ==
            tb->eip_virt + ((inum==0)?0:tb->eip_boundaries[inum-1])) {
          ASSERT(fcallout_patches_pending[i].eip_phys == tb->eip_phys
              + ((inum==0)?0:tb->eip_boundaries[inum-1]));
          if (fcallout_patches_pending[i].use_next_insn) {
            //XXX
            printf("Warning: not tested.\n");
            scan_next_insn(tb->tc_ptr + tb->tc_boundaries[inum], &ptr1, &ptr2);
          } else {
            ptr1 = tb->tc_ptr + tb->tc_boundaries[inum];
            ptr2 = NULL;
          }
          ASSERT(!fcallout_already_patched(ptr1));
          ASSERT(!ptr2 || !fcallout_already_patched(ptr2));
          break;
        }
      }
      ASSERT(inum < tb->num_insns);
      deleted = i;
      break;
    }
  }
  if (deleted == -1) {
    return;
  }
  tb_trace_malloc_remove(fcallout_patches_pending[deleted].eip_phys, 1,
      fcallout_patches_tb_add);
  tb_trace_free_add(tb, fcallout_patches_tb_free);
  for (i = deleted; i < num_fcallout_patches_pending - 1; i++) {
    memcpy(&fcallout_patches_pending[i], &fcallout_patches_pending[i+1],
        sizeof fcallout_patches_pending[i]);
  }
  num_fcallout_patches_pending--;

  //printf("%s(): calling apply_fcallout_patch()\n", __func__);
  apply_fcallout_patch((uint8_t *)ptr1, (uint8_t *)ptr2);
  ASSERT(num_fcallout_patches + num_fcallout_patches_pending
      <= MAX_CALLOUT_PATCHES);
}


static void
fcallout_patches_tb_free(tb_t *tb)
{
  int deleted = -1;
  unsigned i;

  for (i =0; i < num_fcallout_patches; i++) {
    bool b1, b2;
    uint8_t const *ptr;
    target_ulong eip_virt, eip_phys;
    bool use_next_insn;

    b1 = tc_belongs_to_tb(fcallout_patches[i].ptr1, tb);
    b2 = tc_belongs_to_tb(fcallout_patches[i].ptr2, tb);
    if (!b1 && !b2) {
      continue;
    }
		NOT_REACHED();/* It looks like it should never happen that a tb being
										 freed contains a callout patch. */
    ASSERT(b1 || b2);
    /* only one of ptr1 and ptr2 can be in tb. */
    ASSERT((!b1 && b2) || (b1 && !b2));
    ptr = b1?fcallout_patches[i].ptr1:fcallout_patches[i].ptr2;
    eip_virt = tb_tc_ptr_to_eip_virt(ptr);
    eip_phys = (eip_virt - tb->eip_virt) + tb->eip_phys;
    if (tb_is_tc_boundary(ptr)) {
      use_next_insn = false;
    } else {
      use_next_insn = true;
    }
    fcallout_patches_pending[num_fcallout_patches_pending].eip_phys = eip_phys;
    fcallout_patches_pending[num_fcallout_patches_pending].eip_virt = eip_virt;
    fcallout_patches_pending[num_fcallout_patches_pending].use_next_insn =
      use_next_insn;
    num_fcallout_patches_pending++;
    tb_trace_malloc_add(eip_phys, 1, fcallout_patches_tb_add);
    deleted = i;
    break;
  }

  if (deleted == -1) {
    return;
  }
	NOT_REACHED();
  tb_trace_free_remove(tb, fcallout_patches_tb_free);
  ASSERT(deleted < (int)num_fcallout_patches);
  ASSERT(num_fcallout_patches > 0);
  for (i = deleted; i < num_fcallout_patches - 1; i++) {
    memcpy(&fcallout_patches[i], &fcallout_patches[i+1],
        sizeof fcallout_patches[i]);
  }
  num_fcallout_patches--;
  ASSERT(num_fcallout_patches + num_fcallout_patches_pending
      <= MAX_CALLOUT_PATCHES);
}

/*
void
fcallout_patch_pc(target_ulong eip_phys, target_ulong eip_virt)
{
  int inum;
  tb_t *tb;
  uint8_t *ptr;
  tb = tb_find_pc(eip_phys, eip_virt, &inum);
  if (!tb) {
    if (!fcallout_already_pending(eip_phys, eip_virt, false)) {
      fcallout_patches_pending[num_fcallout_patches_pending].eip_phys = eip_phys;
      fcallout_patches_pending[num_fcallout_patches_pending].eip_virt = eip_virt;
      fcallout_patches_pending[num_fcallout_patches_pending].use_next_insn =
        false;
      num_fcallout_patches_pending++;
      tb_trace_malloc_add(eip_phys, 1, fcallout_patches_tb_add);
      ASSERT(num_fcallout_patches_pending <= MAX_CALLOUT_PATCHES);
    }
    return;
  }
  ASSERT(inum < tb->num_insns);
  ASSERT(tb->tc_boundaries[inum] + 1 <= tb->tc_boundaries[tb->num_insns]);
  ptr = tb->tc_ptr+tb->tc_boundaries[inum];
  ASSERT(tb_find(tb->tc_ptr));
  ASSERT(tb_find(ptr));
  if (!fcallout_already_patched((uint8_t const *)ptr)) {
    //printf("%s(): calling apply_fcallout_patch(%p)\n", __func__, ptr);
    apply_fcallout_patch(ptr, NULL);
  }
  ASSERT(num_fcallout_patches + num_fcallout_patches_pending
      <= MAX_CALLOUT_PATCHES);
}
*/

void
apply_fcallout_patch(uint8_t *ptr1, uint8_t *ptr2)
{
  tb_t *tb;
  
  ASSERT(!ptr1 || !fcallout_already_patched(ptr1));
  ASSERT(!ptr2 || !fcallout_already_patched(ptr2));
  //printf("%p: Callout-patching %p,%p\n", vcpu.eip, ptr1, ptr2);
  if (!ptr1) {
    ASSERT(!ptr2);
    return;
  }
  fcallout_patches[num_fcallout_patches].ptr1 = ptr1;
  fcallout_patches[num_fcallout_patches].ptr2 = ptr2;

  ASSERT(ptr1);
  tb = tb_find(ptr1);
  if (!tb) { printf("ptr1=%p\n", ptr1); }
  ASSERT(tb);
  fcallout_patches[num_fcallout_patches].b0 = *ptr1;
  fcallout_patches[num_fcallout_patches].b1 = *(ptr1 + 1);
  *ptr1 = 0xcd;
  *(ptr1 + 1) = 0xff;
  tb_trace_free_add(tb, fcallout_patches_tb_free);

  if (ptr2) {
    tb = tb_find(ptr2);
    ASSERT(tb);
    fcallout_patches[num_fcallout_patches].b2 = *ptr2;
    fcallout_patches[num_fcallout_patches].b3 = *(ptr2 + 1);
    *ptr2 = 0xcd;
    *(ptr2 + 1) = 0xff;
    tb_trace_free_add(tb, fcallout_patches_tb_free);
  }
  num_fcallout_patches++;
  ASSERT(num_fcallout_patches + num_fcallout_patches_pending
      <= MAX_CALLOUT_PATCHES);
}

bool
fcallout_patch_exists(void)
{
  if (num_fcallout_patches + num_fcallout_patches_pending > 0) {
    return true;
  }
  return false;
}

void
fcallouts_tc_write(uint8_t *tc_ptr, uint8_t val)
{
	unsigned i;
	ASSERT(tc_ptr);
	for (i = 0; i < num_fcallout_patches; i++) {
		if (fcallout_patches[i].ptr1 == tc_ptr) {
			fcallout_patches[i].b0 = val;
			return;
		} else if (fcallout_patches[i].ptr1 + 1 == tc_ptr) {
			fcallout_patches[i].b1 = val;
			return;
		} else if (fcallout_patches[i].ptr2 == tc_ptr) {
			fcallout_patches[i].b2 = val;
			return;
		} else if (fcallout_patches[i].ptr2 + 1 == tc_ptr) {
			fcallout_patches[i].b3 = val;
			return;
		}
	}
}

bool
fcallouts_tb_active(tb_t const *tb)
{
	unsigned i;
	ASSERT(tb);
	for (i = 0; i < num_fcallout_patches; i++) {
		ASSERT(fcallout_patches[i].ptr1);
		if (tb == tb_find(fcallout_patches[i].ptr1)) {
			return true;
		}
		if (fcallout_patches[i].ptr2 && tb == tb_find(fcallout_patches[i].ptr2)) {
			return true;
		}
	}
	return false;
}

