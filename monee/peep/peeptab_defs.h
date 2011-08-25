#ifndef PEEP_PEEPTAB_DEFS_H
#define PEEP_PEEPTAB_DEFS_H

#include "lib/macros.h"
#include "lib/mdebug.h"
#include "sys/loader.h"
#include "sys/vcpu_consts.h"

#define SAVE_SS_ESP()                                         \
  movw %ss, %gs:(vcpu + VCPU_SCRATCH_OFF(0));                 \
  movl %esp, %gs:(vcpu + VCPU_SCRATCH_OFF(1));                \
  /* use temporary ss. */                                     \
  movl $(SEL_TMPSEG), %gs:(vcpu + VCPU_SCRATCH_OFF(2));       \
  movw %gs:(vcpu + VCPU_SCRATCH_OFF(2)), %ss;                 \

#define RESTORE_SS_ESP()                                      \
  movw %gs:(vcpu + VCPU_SCRATCH_OFF(0)), %ss;                 \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(1)), %esp

#define SAVE_FLAGS(addr)                                      \
  SAVE_SS_ESP();                                              \
  /* save flags to addr. */                                   \
  movl $((addr) + 4), %esp;                                   \
  pushfl;                                                     \
  RESTORE_SS_ESP()

/* restore flags, without modifying IF. */
#define RESTORE_FLAGS(addr)                                   \
  SAVE_SS_ESP();                                              \
  /* save flags to scratch. */                                \
  movl $((vcpu + VCPU_SCRATCH_OFF(3)) + 4), %esp;             \
  pushfl;                                                     \
  /* use temporary. */                                        \
  movl %eax, %gs:(vcpu + VCPU_SCRATCH_OFF(4));                \
	/* mov (addr) to scratch. */																\
  movl %gs:(addr), %eax;																			\
  movl %eax, %gs:(vcpu + VCPU_SCRATCH_OFF(5));								\
  /* do not change IF (use existing one). */                  \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(3)), %eax;                \
  andl $(IF_MASK), %eax;                                      \
  andl $(~IF_MASK), %gs:(vcpu + VCPU_SCRATCH_OFF(5));         \
  orl  %eax, %gs:(vcpu + VCPU_SCRATCH_OFF(5));                \
  /* restore eax from scratch. */                             \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(4)), %eax;                \
  /* read eflags from scratch. */                             \
  movl $(vcpu + VCPU_SCRATCH_OFF(5)), %esp;                   \
  popfl;                                                      \
  RESTORE_SS_ESP()

#define SAVE_GUEST_FLAGS SAVE_FLAGS(vcpu + VCPU_EFLAGS_OFF)
#define RESTORE_GUEST_FLAGS RESTORE_FLAGS(vcpu + VCPU_EFLAGS_OFF)

#define SAVE_MONITOR_FLAGS_TO(mstruct) SAVE_FLAGS(mstruct + MONITOR_EFLAGS_OFF)
#define RESTORE_MONITOR_FLAGS_FROM(mstruct) RESTORE_FLAGS(mstruct+MONITOR_EFLAGS_OFF)

#define SAVE_MONITOR_FLAGS 		SAVE_MONITOR_FLAGS_TO(monitor)
#define RESTORE_MONITOR_FLAGS RESTORE_MONITOR_FLAGS_FROM(monitor)

#define SAVE_GUEST                                \
  movl %eax, %gs:(vcpu + VCPU_EAX_OFF);           \
  movl %ecx, %gs:(vcpu + VCPU_ECX_OFF);           \
  movl %edx, %gs:(vcpu + VCPU_EDX_OFF);           \
  movl %ebx, %gs:(vcpu + VCPU_EBX_OFF);           \
  movl %esp, %gs:(vcpu + VCPU_ESP_OFF);           \
  movl %ebp, %gs:(vcpu + VCPU_EBP_OFF);           \
  movl %esi, %gs:(vcpu + VCPU_ESI_OFF);           \
  movl %edi, %gs:(vcpu + VCPU_EDI_OFF);           \
  movw %es, %gs:(vcpu + VCPU_ES_OFF);             \
  movw %fs, %gs:(vcpu + VCPU_FS_OFF);             \
  movw %ds, %gs:(vcpu + VCPU_DS_OFF);             \
  movw %ss, %gs:(vcpu + VCPU_SS_OFF);             \
  fxsave %gs:(vcpu + VCPU_FXSTATE_OFF);

/* 1. In RESTORE_GUEST, the segment registers must be restored BEFORE restoring
 *    esp. esp should not be modified between restoration of segment registers
 *    and restoration of guest.esp. At this point, ASSERT(monitor.esp == %esp)
 *
 *    This is because, the interrupt handler uses the segment register to
 *    determine if the execution was inside guest-context or inside
 *    monitor-context. Hence, if there is an interrupt between the modification
 *    of segment register and the modification of esp, things should remain
 *    coherent.
 *
 * 2. Similarly, SAVE_MONITOR_FLAGS and RESTORE_GUEST_FLAGS should always be
 *    AFTER restoration of
 *    segment registers. This is because RESTORE_GUEST_FLAGS temporarily
 *    sets the stack pointer, and this should happen only in guest context.
 */
#define RESTORE_GUEST                             \
  movw %gs:(vcpu + VCPU_ES_OFF), %es;             \
  movw %gs:(vcpu + VCPU_FS_OFF), %fs;             \
  movw %gs:(vcpu + VCPU_DS_OFF), %ds;             \
  movw %gs:(vcpu + VCPU_SS_OFF), %ss;             \
  movl %gs:(vcpu + VCPU_EAX_OFF), %eax;           \
  movl %gs:(vcpu + VCPU_ECX_OFF), %ecx;           \
  movl %gs:(vcpu + VCPU_EDX_OFF), %edx;           \
  movl %gs:(vcpu + VCPU_EBX_OFF), %ebx;           \
  movl %gs:(vcpu + VCPU_ESP_OFF), %esp;           \
  movl %gs:(vcpu + VCPU_EBP_OFF), %ebp;           \
  movl %gs:(vcpu + VCPU_ESI_OFF), %esi;           \
  movl %gs:(vcpu + VCPU_EDI_OFF), %edi;           \
  fxrstor %gs:(vcpu + VCPU_FXSTATE_OFF);


#define SAVE_MONITOR_TO(mstruct)                  \
  movl %eax, %gs:(mstruct + MONITOR_EAX_OFF);     \
  movl %ecx, %gs:(mstruct + MONITOR_ECX_OFF);     \
  movl %edx, %gs:(mstruct + MONITOR_EDX_OFF);     \
  movl %ebx, %gs:(mstruct + MONITOR_EBX_OFF);     \
  movl %esp, %gs:(mstruct + MONITOR_ESP_OFF);     \
  movl %ebp, %gs:(mstruct + MONITOR_EBP_OFF);     \
  movl %esi, %gs:(mstruct + MONITOR_ESI_OFF);     \
  movl %edi, %gs:(mstruct + MONITOR_EDI_OFF);     \
  /*fxsave  %gs:(mstruct + MONITOR_FXSTATE_OFF);*/\
  movw %es,  %gs:(mstruct + MONITOR_ES_OFF);      \
  movw %fs,  %gs:(mstruct + MONITOR_FS_OFF);      \
  movw %ds,  %gs:(mstruct + MONITOR_DS_OFF);      \
  movw %ss,  %gs:(mstruct + MONITOR_SS_OFF)

/* 1. In RESTORE_MONITOR, the segment registers should be restored AFTER
 *    restoring esp but before any modification to esp. This ensures that
 *    monitor.esp == %esp between the restoration of %esp and the restoration
 *    of segment registers. The reasons are similar to the comment made for
 *    RESTORE_GUEST
 * 
 * 2. Simiarly, RESTORE_MONITOR_FLAGS and SAVE_GUEST_FLAGS should happen BEFORE
 *    the segment registers are restored. This ensures that all stack
 *    modifications happen in guest-context.
 */
#define RESTORE_MONITOR_FROM(mstruct)             \
  movl %gs:(mstruct + MONITOR_EAX_OFF), %eax;     \
  movl %gs:(mstruct + MONITOR_ECX_OFF), %ecx;     \
  movl %gs:(mstruct + MONITOR_EDX_OFF), %edx;     \
  movl %gs:(mstruct + MONITOR_EBX_OFF), %ebx;     \
  movl %gs:(mstruct + MONITOR_ESP_OFF), %esp;     \
  movl %gs:(mstruct + MONITOR_EBP_OFF), %ebp;     \
  movl %gs:(mstruct + MONITOR_ESI_OFF), %esi;     \
  movl %gs:(mstruct + MONITOR_EDI_OFF), %edi;     \
  movw %gs:(mstruct + MONITOR_ES_OFF), %es;       \
  movw %gs:(mstruct + MONITOR_FS_OFF), %fs;       \
  movw %gs:(mstruct + MONITOR_DS_OFF), %ds;       \
  movw %gs:(mstruct + MONITOR_SS_OFF), %ss


#define SAVE_MONITOR              						SAVE_MONITOR_TO(monitor)
#define RESTORE_MONITOR           						RESTORE_MONITOR_FROM(monitor)

#define SAVE_MONITOR_TO_FUNC      				\
	SAVE_MONITOR_TO(vcpu + VCPU_FUNC_MONITOR_STATE_OFF);			 	\
  SAVE_MONITOR_FLAGS_TO(vcpu + VCPU_FUNC_MONITOR_STATE_OFF)
#define RESTORE_MONITOR_FROM_FUNC 				\
	RESTORE_MONITOR_FROM(vcpu + VCPU_FUNC_MONITOR_STATE_OFF);		\
  RESTORE_MONITOR_FLAGS_FROM(vcpu + VCPU_FUNC_MONITOR_STATE_OFF)

#define SAVE_MONITOR_TO_INTR_FRAME_STATE \
	SAVE_MONITOR_TO(vcpu + VCPU_FUNC_INTR_FRAME_STATE_OFF); \
  SAVE_MONITOR_FLAGS_TO(vcpu + VCPU_FUNC_INTR_FRAME_STATE_OFF)
#define RESTORE_MONITOR_FROM_INTR_FRAME_STATE \
	RESTORE_MONITOR_FROM(vcpu + VCPU_FUNC_INTR_FRAME_STATE_OFF);  \
  RESTORE_MONITOR_FLAGS_FROM(vcpu + VCPU_FUNC_INTR_FRAME_STATE_OFF)

#define save_flags_use_eax_temp(loc, temp)              \
  lahf;                                                 \
  movw %temp##w, loc;                                   \
  seto loc + 2

#define restore_flags_use_eax_temp(loc, temp)           \
  cmpb $0, loc + 2;                                     \
  je 9f;                                                \
  /* create an overflow to set overflow flag. */        \
  movb $1, %temp##b;                                    \
  addb $0x7f, %temp##b;                                 \
9:movw loc, %temp##w;                                   \
  sahf;                                                 \

#define __JUMP_INDIRECT_PART1(target, temp)                   \
  save_flags_use_eax_temp(%gs:vcpu + VCPU_SCRATCH_OFF(1), temp);              \
  movl target, %temp##d;                                                      \
  addl %gs:(vcpu + VCPU_SEGS_BASE_OFF(R_CS)), %temp##d;                       \
  movl %temp##d, %gs:(vcpu + VCPU_SCRATCH_OFF(2));                            \
  /*XXX: also check against cs limit. */                                      \
  andl $JUMPTABLE1_MASK, %temp##d;                                            \
  addl $jumptable1, %temp##d;                                                 \
  movl %temp##d, %gs:(vcpu + VCPU_SCRATCH_OFF(3));                            \
	/* Check if the target is NULL, in which case this entry does not exist. */ \
  cmpl $0x0, %gs:0x4(%temp##d,%eiz,1);     		                                \
	je	 1f;																																		\
	/* Check if the table's eip matches ours. */																\
  movl %gs:(%temp##d,%eiz,1), %temp##d;                                       \
  cmpl %temp##d, %gs:(vcpu + VCPU_SCRATCH_OFF(2));                            \
  jne  1f;                                                                    \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(3)), %temp##d;                            \
  movl %gs:4(%temp##d,%eiz,1), %temp##d;                                      \
  movl %temp##d, %gs:(vcpu + VCPU_JTARGET_OFF);                               \
  restore_flags_use_eax_temp(%gs:vcpu + VCPU_SCRATCH_OFF(1), temp);

#define __JUMP_INDIRECT_PART2(target, temp)                                   \
1:set_eip(target);                                                            \
  restore_flags_use_eax_temp(%gs:vcpu + VCPU_SCRATCH_OFF(1), temp);

#define JUMP_INDIRECT_AFTER_TR1_RESTORE_USE_EAX_TEMP(target, temp)            \
  __JUMP_INDIRECT_PART1(target, temp);                                        \
  RESTORE_TEMPORARY(1);                                                       \
  RESTORE_TEMPORARY(0);                                                       \
  jmpl *%gs:(vcpu + VCPU_JTARGET_OFF);                                        \
  __JUMP_INDIRECT_PART2(target, temp);                                        \
  RESTORE_TEMPORARY(1);                                                       \
  RESTORE_TEMPORARY(0);                                                       \
  EXIT_TB

#define JUMP_INDIRECT_AFTER_TR2_RESTORE_USE_EAX_TEMP(target, temp)            \
  __JUMP_INDIRECT_PART1(target, temp);                                        \
  RESTORE_TEMPORARY(2);                                                       \
  RESTORE_TEMPORARY(1);                                                       \
  RESTORE_TEMPORARY(0);                                                       \
  jmpl *%gs:(vcpu + VCPU_JTARGET_OFF);                                        \
  __JUMP_INDIRECT_PART2(target, temp);                                        \
  RESTORE_TEMPORARY(2);                                                       \
  RESTORE_TEMPORARY(1);                                                       \
  RESTORE_TEMPORARY(0);                                                       \
  EXIT_TB


#define JUMP_INDIRECT_USE_EAX_TEMP(target, tmpno)       \
  __JUMP_INDIRECT_PART1(target, tr##tmpno);             \
  RESTORE_TEMPORARY(tmpno);                             \
  jmpl *%gs:(vcpu + VCPU_JTARGET_OFF);                  \
  __JUMP_INDIRECT_PART2(target, tr##tmpno);             \
  RESTORE_TEMPORARY(tmpno);                             \
  EXIT_TB

#define JUMP_TO_TC_IF_NO_PENDING_INTERRUPTS                                   \
  cli;                                                                        \
  SAVE_GUEST_FLAGS;                                                           \
  cmpl $0, %gs:(vcpu + VCPU_REPLAY_LOG_OFF);                       						\
  jne 1f;                                                                     \
  cmpb $0, %gs:(vcpu + VCPU_INTERRUPT_REQUEST_OFF);                           \
  je 1f;                                                                      \
  cmpw $1, %gs:(vcpu + VCPU_IF_OFF);                                          \
  jne 1f;                                                                     \
  /*cmpl $0, %gs:(vcpu + VCPU_STI_FALLTHROUGH_OFF);                           \
  jne 1f;                                                              */     \
  RESTORE_GUEST_FLAGS;                                                        \
  sti;                                                                        \
  JUMP_TO_MONITOR;                                                            \
  1: RESTORE_GUEST_FLAGS;                                                     \
  sti;                                                                        \
  jmp *%gs:(vcpu + VCPU_TC_PTR_OFF)

#define CHECK_IF2_AND_SET     																								\
  SAVE_GUEST_FLAGS;																														\
  cmpw $2, %gs:(vcpu + VCPU_IF_OFF);																					\
  jne 1f;																																			\
  movw $1, %gs:(vcpu + VCPU_IF_OFF);																					\
  1: RESTORE_GUEST_FLAGS

#define CALLOUT_NOP_IF_PENDING_IRQ																						\
  SAVE_GUEST_FLAGS;																														\
  cmpb $0, %gs:(vcpu + VCPU_INTERRUPT_REQUEST_OFF);														\
  je 1f;																																			\
  RESTORE_GUEST_FLAGS;																												\
  CALLOUT0(callout_nop);																											\
  1: RESTORE_GUEST_FLAGS

#define READ_MEM16                                                          \
  save_flags_use_eax_temp(%gs:vcpu + VCPU_SCRATCH_OFF(1), tr0);             \
  movl $C0, %tr1d;                                                          \
  movl $vr0d, %gs:vcpu + VCPU_SCRATCH_OFF(2);                               \
  cmpl $-1, %gs:vcpu + VCPU_SCRATCH_OFF(2);                                 \
  je 1f; /* index not present. */                                           \
  movzwl %vr0w, %tr0d;                                                      \
  leal 0x0(%tr0d, %tr1d, 1), %tr1d;                                         \
 1: movl $vr1d, %gs:vcpu + VCPU_SCRATCH_OFF(2);                             \
  cmpl $-1, %gs:vcpu + VCPU_SCRATCH_OFF(2);                                 \
  je 1f; /* index not present. */                                           \
  movzwl %vr1w, %tr0d;                                                      \
  leal 0x0(%tr0d, %tr1d, 1), %tr1d;                                         \
 1: movzwl %tr1w, %tr1d;                                                    \
  movl $vseg0, %tr0d;                                                       \
  imul $VCPU_SEGS_STRUCT_SIZE, %tr0d;                                       \
  addl $(vcpu + VCPU_SEGS_BASE_OFF(0)), %tr0d;                              \
  addl %gs:(%tr0d,%eiz,1), %tr1d;                                           \
  restore_flags_use_eax_temp(%gs:vcpu + VCPU_SCRATCH_OFF(1), tr0);          \
  movl %tr1d, %gs:C1
  
#define save_monitor()    				  asm(xstr(SAVE_MONITOR))
#define restore_monitor() 				  asm(xstr(RESTORE_MONITOR))
#define save_monitor_to_func()      asm(xstr(SAVE_MONITOR_TO_FUNC))
#define restore_monitor_from_func() asm(xstr(RESTORE_MONITOR_FROM_FUNC))
#define save_monitor_to_intr_frame_state() asm(xstr(SAVE_MONITOR_TO_INTR_FRAME_STATE))
#define restore_monitor_from_intr_frame_state() asm(xstr(RESTORE_MONITOR_FROM_INTR_FRAME_STATE))
#define save_guest()     					  asm(xstr(SAVE_GUEST))
#define restore_guest()   					asm(xstr(RESTORE_GUEST))

#define save_guest_flags()  			  asm(xstr(SAVE_GUEST_FLAGS))
#define restore_guest_flags()  		  asm(xstr(RESTORE_GUEST_FLAGS))
#define save_monitor_flags()  		  asm(xstr(SAVE_MONITOR_FLAGS))
#define restore_monitor_flags()  	  asm(xstr(RESTORE_MONITOR_FLAGS))

#define jump_to_tc_if_no_pending_interrupts()         \
  asm(xstr(JUMP_TO_TC_IF_NO_PENDING_INTERRUPTS))

/* Snippets. */
#define SAVE_REG movl %vr0d, %gs:C0
#define LOAD_REG movl %gs:C0, %vr0d

/* Rules for peep.tab. */

#define CALLOUT0(func)                                  \
  movl $func, %gs:(vcpu+VCPU_CALLOUT_OFF);              \
  movl $0, %gs:(vcpu+VCPU_CALLOUT_N_ARGS_OFF);          \
  movl $tc_end, %gs:(vcpu+VCPU_CALLOUT_NEXT_OFF);       \
  movl $gen_code_ptr, %gs:(vcpu+VCPU_CALLOUT_CUR_OFF);	\
  set_eip_executing($cur_addr);                   			\
  set_eip($fallthrough_addr);                      			\
	JUMP_TO_MONITOR

#define CALLOUT1(func, arg1)                            \
  movl $func, %gs:(vcpu+VCPU_CALLOUT_OFF);              \
  movl $1, %gs:(vcpu+VCPU_CALLOUT_N_ARGS_OFF);          \
  movl arg1, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF);          \
  movl $tc_end, %gs:(vcpu+VCPU_CALLOUT_NEXT_OFF);       \
  movl $gen_code_ptr, %gs:(vcpu+VCPU_CALLOUT_CUR_OFF);	\
  set_eip_executing($cur_addr);                   			\
  set_eip($fallthrough_addr);                      			\
	JUMP_TO_MONITOR

#define CALLOUT2(func, arg1, arg2)                      \
  movl $func, %gs:(vcpu+VCPU_CALLOUT_OFF);              \
  movl $2, %gs:(vcpu+VCPU_CALLOUT_N_ARGS_OFF);          \
  movl arg1, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF);          \
  movl arg2, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF+4);        \
  movl $tc_end, %gs:(vcpu+VCPU_CALLOUT_NEXT_OFF);       \
  movl $gen_code_ptr, %gs:(vcpu+VCPU_CALLOUT_CUR_OFF);	\
  set_eip_executing($cur_addr);                   			\
  set_eip($fallthrough_addr);                      			\
	JUMP_TO_MONITOR

#define CALLOUT3(func, arg1, arg2, arg3)                \
  movl $func, %gs:(vcpu+VCPU_CALLOUT_OFF);              \
  movl $3, %gs:(vcpu+VCPU_CALLOUT_N_ARGS_OFF);          \
  movl arg1, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF);          \
  movl arg2, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF+4);        \
  movl arg3, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF+8);        \
  movl $tc_end, %gs:(vcpu+VCPU_CALLOUT_NEXT_OFF);       \
  movl $gen_code_ptr, %gs:(vcpu+VCPU_CALLOUT_CUR_OFF);	\
  set_eip_executing($cur_addr);                   			\
  set_eip($fallthrough_addr);                      			\
	JUMP_TO_MONITOR

#define CALLOUT4(func, arg1, arg2, arg3, arg4)          \
  movl $func, %gs:(vcpu+VCPU_CALLOUT_OFF);              \
  movl $4, %gs:(vcpu+VCPU_CALLOUT_N_ARGS_OFF);          \
  movl arg1, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF);          \
  movl arg2, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF+4);        \
  movl arg3, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF+8);        \
  movl arg4, %gs:(vcpu+VCPU_CALLOUT_ARGS_OFF+12);       \
  movl $tc_end, %gs:(vcpu+VCPU_CALLOUT_NEXT_OFF);       \
  movl $gen_code_ptr, %gs:(vcpu+VCPU_CALLOUT_CUR_OFF);	\
  set_eip_executing($cur_addr);                   			\
  set_eip($fallthrough_addr);                      			\
	JUMP_TO_MONITOR

#define CALL_FUNC                                   \
  movl %esp, %gs:(vcpu + VCPU_FUNC_STACK_OFF + 8);  \
  movl %ss, %esp;                                   \
  movl %esp, %gs:(vcpu + VCPU_FUNC_STACK_OFF + 4);  \
  movl $SEL_TMPSEG, %esp;                           \
  movl %esp, %ss;                                   \
  leal (vcpu + VCPU_FUNC_STACK_OFF + 4), %esp;      \
  call *%gs:(vcpu + VCPU_FUNC_LABEL_OFF)

#define FUNC0(func)                                 \
  movl $func, %gs:(vcpu+VCPU_FUNC_OFF);             \
  movl $0, %gs:(vcpu+VCPU_FUNC_N_ARGS_OFF);         \
  CALL_FUNC

#define FUNC1(func, arg1)                           \
  movl $func, %gs:(vcpu+VCPU_FUNC_OFF);             \
  movl $1, %gs:(vcpu+VCPU_FUNC_N_ARGS_OFF);         \
  movl arg1, %gs:(vcpu+VCPU_FUNC_ARGS_OFF);         \
  CALL_FUNC

#define FUNC2(func, arg1, arg2)                     \
  movl $func, %gs:(vcpu+VCPU_FUNC_OFF);             \
  movl $2, %gs:(vcpu+VCPU_FUNC_N_ARGS_OFF);         \
  movl arg1, %gs:(vcpu+VCPU_FUNC_ARGS_OFF);         \
  movl arg2, %gs:(vcpu+VCPU_FUNC_ARGS_OFF+4);       \
  CALL_FUNC

#define FUNC3(func, arg1, arg2, arg3)               \
  movl $func, %gs:(vcpu+VCPU_FUNC_OFF);             \
  movl $3, %gs:(vcpu+VCPU_FUNC_N_ARGS_OFF);         \
  movl arg1, %gs:(vcpu+VCPU_FUNC_ARGS_OFF);         \
  movl arg2, %gs:(vcpu+VCPU_FUNC_ARGS_OFF+4);       \
  movl arg3, %gs:(vcpu+VCPU_FUNC_ARGS_OFF+8);       \
  CALL_FUNC

#define set_eip(new_eip) movl new_eip, %gs:(vcpu + VCPU_EIP_OFF); movl $1, %gs:(vcpu + VCPU_NEXT_EIP_IS_SET_OFF)
#define set_eip_executing(cur_eip) movl cur_eip, %gs:(vcpu + VCPU_EIP_EXECUTING_OFF)
/*
#define INCREMENT_VCPU_N_EXEC addl $C0, %gs:(vcpu + VCPU_N_EXEC_OFF); \
  adcl $0, %gs:(vcpu + VCPU_N_EXEC_OFF + 4)
*/
#define INCREMENT_VCPU_N_EXEC movl %eax, %gs:(vcpu + VCPU_SCRATCH_OFF(0)); movl %gs:(vcpu + VCPU_N_EXEC_OFF), %eax; leal C0(%eax), %eax; movl %eax, %gs:(vcpu + VCPU_N_EXEC_OFF); movl %gs:(vcpu + VCPU_SCRATCH_OFF(0)), %eax
#define CALLOUT_RR_LOG_VCPU_STATE SAVE_FLAGS(vcpu + VCPU_TEMPORARIES_OFF(0)); movl %eax, %gs:(vcpu + VCPU_TEMPORARIES_OFF(1)); movl %gs:(vcpu + VCPU_N_EXEC_OFF), %eax; cmpl %gs:(vcpu + VCPU_REPLAY_LAST_ENTRY_N_EXEC_OFF), %eax; jb 1f; RESTORE_FLAGS(vcpu + VCPU_TEMPORARIES_OFF(0)); movl %gs:(vcpu + VCPU_TEMPORARIES_OFF(1)), %eax; CALLOUT1(rr_log_vcpu_state, $C0); jmp 2f; 1: RESTORE_FLAGS(vcpu + VCPU_TEMPORARIES_OFF(0)); movl %gs:(vcpu + VCPU_TEMPORARIES_OFF(1)), %eax; 2:
//#define CALLOUT_RR_LOG_VCPU_STATE CALLOUT1(rr_log_vcpu_state, $C0)
#define _(x) 1f+(x); 1:

/* temp must be no_eax. seg must be cs_gs*/
#define MOV_SEG_TO_GS_USE_EAX_TEMP0_NO_EAX_TEMP1(seg, temp0, temp1) \
  /*movl %eax, %gs:(vcpu + VCPU_SCRATCH_OFF(0));            */      \
  save_flags_use_eax_temp(%gs:vcpu + VCPU_SCRATCH_OFF(1), temp0);   \
  movl $seg, %temp1##d;                                             \
  imul $VCPU_SEGS_STRUCT_SIZE, %temp1##d;                           \
  addl $(vcpu + VCPU_SEGS_OFF(0)), %temp1##d;                       \
  restore_flags_use_eax_temp(%gs:vcpu + VCPU_SCRATCH_OFF(1), temp0);\
  /* movl %gs:(vcpu + VCPU_SCRATCH_OFF(0)), %eax;       */          \
  movl %gs:(%temp1##d,%eiz,1), %temp1##d;                           \
  movw %temp1##w, %gs;

#if 0
/* reg and tmp must be no_eax. */
#define LEA_WITH_SEG_USE_TEMP(seg, mem, reg, tmp)                   \
  movl %eax, %gs:(vcpu + VCPU_SCRATCH_OFF(0));                      \
  save_flags_use_eax(%gs:vcpu + VCPU_SCRATCH_OFF(1));               \
  movl $seg, %tmp##d;                                               \
  imul $VCPU_SEGS_STRUCT_SIZE, %tmp##d;                             \
  movl (vcpu + VCPU_SEGS_BASE_OFF(0))(%tmp##d,%eiz,1), %tmp##d;     \
  leal mem, %reg##d;                                                \
  addl %tmp##d, %reg##d;                                            \
  /* XXX: check against limit. */                                   \
  restore_flags_use_eax(%gs:vcpu + VCPU_SCRATCH_OFF(1));            \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(0)), %eax;

#endif

/*
#define MOVW_CS_GS_MEM16_USE_TR0_TR1(target, seg)                   \
  __MOV_CS_GS_MEM16_USE_TR0_TR1_LOAD_GS(seg);                       \
  movw %gs:MEM16, target;                                           \
  movl $SEL_UDSEG, %tr1d;                                           \
  movw %tr1w, %gs

#define MOVL_CS_GS_MEM16_USE_TR0_TR1(target, seg)                           \
  __MOV_CS_GS_MEM16_USE_TR0_TR1_LOAD_GS(seg);                       \
  movl %gs:MEM16, target;                                           \
  movl $SEL_UDSEG, %tr1d;                                           \
  movw %tr1w, %gs
  */

#define RESTORE_GS                                                  \
  movw %cs:(vcpu + VCPU_DEFAULT_USER_GS_OFF), %gs

#define MOV_REG_TO_SEG_USE_NO_EAX_TEMP(regno, rsegno, segno, tmp)       \
  movl segno, %tmp##d;                                                  \
  leal (vcpu + VCPU_ORIG_SEGS_OFF(0))(,%tmp##d,4), %tmp##d;             \
  movw regno##w, %gs:(%tmp##d);                                         \
  movl regno##d, %tmp##d;                                               \
  movl %eax, %gs:(vcpu + VCPU_SCRATCH_OFF(0));                          \
  lahf;                                                                 \
  orl  $3, %tmp##d;                                                     \
  sahf;                                                                 \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(0)), %eax;                          \
  movl %tmp##d, rsegno

#define REAL_GET_MEM_ADDR_USE_NO_ESP_TEMP0_EAX_TEMP1(segno, memexpr, dstreg, temp0, temp1)  \
  save_flags_use_eax_temp(%gs:vcpu + VCPU_SCRATCH_OFF(1), temp1);   \
  movl $segno, %temp0##d;                                           \
  imul $VCPU_SEGS_STRUCT_SIZE, %temp0##d;                           \
  addl $(vcpu + VCPU_SEGS_BASE_OFF(0)), %temp0##d;                       \
  restore_flags_use_eax_temp(%gs:vcpu + VCPU_SCRATCH_OFF(1), temp1);\
  movl %gs:(%temp0##d,%eiz,1), %dstreg##d;                          \
  leal memexpr, %temp0##d;                                           \
  leal 0x0(%dstreg##d, %temp0##d, 1), %dstreg##d

#define HANDLE_LOOP(regname)                                  \
  lahf;                                                       \
  dec  %regname;                                              \
  cmp  $0x0, %regname;                                        \
  jz 1f;                                                      \
     sahf; RESTORE_TEMPORARY(0);                              \
     jmp target_C0;                                           \
  1: sahf; RESTORE_TEMPORARY(0);                              \
     jmp tc_next_eip;                                         \
  EDGE0: set_eip($C0); EXIT_TB;                               \
  EDGE1: set_eip($fallthrough_addr); EXIT_TB

#define HANDLE_LOOPX(jcond, regname)                        \
  lahf;                                                     \
  jcond 1f;                                                 \
  cmp  $0x1, %regname;                                      \
  jz 1f;                                                    \
     dec  %regname;                                         \
     sahf; RESTORE_TEMPORARY(0);                            \
     jmp target_C0;                                         \
  1: dec  %regname;                                         \
     sahf; RESTORE_TEMPORARY(0);                            \
     jmp tc_next_eip;                                       \
  EDGE0: set_eip($C0); EXIT_TB;                             \
  EDGE1: set_eip($fallthrough_addr); EXIT_TB


#define SAVE_PREV_TB movl $gen_code_ptr, %gs:(vcpu + VCPU_PREV_TB_OFF)
#define JUMP_TO_MONITOR jmp *%gs:(monitor + MONITOR_EIP_OFF)
#define JUMP_INDIR_INSN jmp *%gs:C0
#define EXIT_TB SAVE_PREV_TB; JUMP_TO_MONITOR
#define EDGE0 .edge0: movl $0, %gs:(vcpu + VCPU_EDGE_OFF); 1
#define EDGE1 .edge1: movl $1, %gs:(vcpu + VCPU_EDGE_OFF); 1
#define SAVE_TEMPORARY(tnum) \
  movl %tr##tnum##d, %gs:(vcpu + VCPU_TEMPORARIES_OFF(tnum))
#define RESTORE_TEMPORARY(tnum) \
  movl %gs:(vcpu + VCPU_TEMPORARIES_OFF(tnum)), %tr##tnum##d

#define MOV_MEM_TO_REG movl %gs:C0, %vr0d

#define MOVB_REGADDR_TO_AL  movb %gs:(%vr0d,%eiz,1), %al
#define MOVW_REGADDR_TO_AX  movw %gs:(%vr0d,%eiz,1), %ax
#define MOVL_REGADDR_TO_EAX movl %gs:(%vr0d,%eiz,1), %eax
#define MOVB_AL_TO_REGADDR  movb %al, %gs:(%vr0d,%eiz,1)
#define MOVW_AX_TO_REGADDR  movw %ax, %gs:(%vr0d,%eiz,1)
#define MOVL_EAX_TO_REGADDR movl %eax, %gs:(%vr0d,%eiz,1)

#define MOVB_DISPADDR_TO_REGADDR  		\
	movl %gs:C0, %vr1d;									\
  movb %gs:(%vr1d,%eiz,1), %vr1b;			\
  movb %vr1b, %gs:(%vr0d,%eiz,1)
#define MOVW_DISPADDR_TO_REGADDR  		\
	movl %gs:C0, %vr1d;									\
  movw %gs:(%vr1d,%eiz,1), %vr1w;			\
  movw %vr1w, %gs:(%vr0d,%eiz,1)
#define MOVL_DISPADDR_TO_REGADDR  		\
	movl %gs:C0, %vr1d;									\
  movl %gs:(%vr1d,%eiz,1), %vr1d;			\
  movl %vr1d, %gs:(%vr0d,%eiz,1)

#define EMIT_EDGE1 jmp tc_next_eip; EDGE1: set_eip($fallthrough_addr); EXIT_TB

#define RET ret
#define ROMWRITE_SET_DST    __ROMWRITE_SET_DST ; .long C0 ; __MDEBUG_PAD
#define ROMWRITE_SET_SRC    __ROMWRITE_SET_SRC ; .long C0 ; __MDEBUG_PAD
#define ROMWRITE_SET_SIZE   __ROMWRITE_SET_SIZE; .long C0 ; __MDEBUG_PAD
#define ROMWRITE_TRANSFER   __ROMWRITE_TRANSFER ; __MDEBUG_PAD

#endif /* sys/save_restore.h */
