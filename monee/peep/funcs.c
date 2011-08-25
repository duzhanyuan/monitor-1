#include "peep/funcs.h"
#include <stdlib.h>
#include <string.h>
#include "peepgen_offsets.h"
#include "peep/callouts.h"
#include "peep/peeptab_defs.h"
#include "sys/vcpu.h"
#include "sys/monitor.h"
#include "sys/interrupt.h"
#include "threads/synch.h"

static bool ftrue(void);

void
funcs_init(void)
{
  static uint32_t retaddr;
	static struct func_struct *func_struct;
	static void *func_tc_ptr, *func_tc_done;
	static int cur_depth;
  vcpu.func_label = &&func_label;
  vcpu.tc_label = &&tc_label;
  vcpu.func_monitor_eip = &&func_monitor_eip;

  if (ftrue()) {
    return;
  }

tc_label:
	cur_depth = vcpu.func_depth++;
	if (vcpu.func_depth > MAX_FUNC_DEPTH) {
		PANIC("func_depth too large!\n");
	}
	func_struct = &vcpu.func_struct[cur_depth];
	intr_frame_2_monitor(&vcpu.func_intr_frame_monitor_state,
			&func_struct->func_intr_frame);

	func_tc_ptr = func_struct->func_tc_buf;
	memcpy(&vcpu.cur_func_struct, func_struct, sizeof (struct func_struct));
	last_monitor_context = &vcpu.cur_func_struct.func_monitor_state;
	barrier();
  save_monitor_to_func();
  restore_monitor_from_intr_frame_state();
  asm volatile ("jmp *%%gs:(%0)":: "m"(func_tc_ptr));
func_monitor_eip:
  save_monitor_to_intr_frame_state();
  restore_monitor_from_func();
	barrier();
	cur_depth = --vcpu.func_depth;
	func_struct = &vcpu.func_struct[cur_depth];
	memcpy(func_struct, &vcpu.cur_func_struct, sizeof (struct func_struct));
	vcpu.func_intr_frame_monitor_state.eflags &= ~IF_MASK;
	vcpu.func_intr_frame_monitor_state.eflags |=
		(func_struct->func_intr_frame.eflags & IF_MASK);

	monitor_2_intr_frame(&func_struct->func_intr_frame,
			&vcpu.func_intr_frame_monitor_state);

	func_tc_done = func_struct->func_tc_done;
  asm volatile ("jmp *%%gs:(%0)" :: "m"(func_tc_done));

func_label:
  save_monitor_to_intr_frame_state();
  restore_monitor_from_func();
	barrier();
	cur_depth = vcpu.func_depth - 1;
	func_struct = &vcpu.func_struct[cur_depth];
	memcpy(func_struct, &vcpu.cur_func_struct, sizeof (struct func_struct));
	monitor_2_vcpu(&vcpu.func_intr_frame_monitor_state);
  /* restore ss, esp. */
  vcpu.segs[R_SS].selector = func_struct->func_stack[1];
  vcpu.regs[R_ESP] = func_struct->func_stack[2];
  ASSERT(func_struct->func);
  switch (func_struct->func_n_args) {
    case 0:
      ((void (*)(void))func_struct->func)();
      break;
    case 1:
      ((void (*)(long))func_struct->func)(func_struct->func_args[0]);
      break;
    case 2:
      ((void (*)(long,long))func_struct->func)(
				func_struct->func_args[0], func_struct->func_args[1]);
      break;
    case 3:
      ((void (*)(long,long,long))func_struct->func)(
				func_struct->func_args[0],
				func_struct->func_args[1],
				func_struct->func_args[2]);
      break;
    default:
      ASSERT(0);
      break;
  }
	cur_depth = vcpu.func_depth - 1;
	func_struct = &vcpu.func_struct[cur_depth];
  retaddr = func_struct->func_stack[0];
	vcpu_2_monitor(&vcpu.func_intr_frame_monitor_state);
	memcpy(&vcpu.cur_func_struct, func_struct, sizeof (struct func_struct));
	barrier();
  save_monitor_to_func();
  restore_monitor_from_intr_frame_state();
  asm volatile ("jmp *%%gs:(%0)" : : "m"(retaddr));
}

static bool
ftrue(void)
{
  /* to fool the compiler. */
  return true;
}
