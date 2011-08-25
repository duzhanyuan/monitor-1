#ifndef LIB_BXDEBUG_H
#define LIB_BXDEBUG_H

#include "lib/macros.h"

/*
#define __MDEBUG_PAD       jmp 1f ; 1: .byte 0x90, 0x90, 0x90, 0x90, 0x90, 0x90

#define __DUMP_STATE             .byte 0x0f, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00
#define __INSPECT_MEMORY         .byte 0x0f, 0x04, 0x01
#define __MONITOR_MEMORY_START   .byte 0x0f, 0x04, 0x02
#define __MONITOR_MEMORY_STOP    .byte 0x0f, 0x04, 0x03
#define __PRINT_ERRORMSG         .byte 0x0f, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00
#define __PROFILE_RESET          .byte 0x0f, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00
#define __MDEBUG_SET             .byte 0x0f, 0x04, 0x10

#define DUMP_STATE        __DUMP_STATE     ; __MDEBUG_PAD
#define INSPECT_MEMORY(x) __INSPECT_MEMORY ; .long (x) ; __MDEBUG_PAD
#define MONITOR_MEMORY_START(x) __MONITOR_MEMORY_START ; .long (x); __MDEBUG_PAD
#define MONITOR_MEMORY_STOP(x)  __MONITOR_MEMORY_STOP  ; .long (x); __MDEBUG_PAD
#define PRINT_ERRORMSG    __PRINT_ERRORMSG ; __MDEBUG_PAD
#define PROFILE_RESET     __PROFILE_RESET ; __MDEBUG_PAD
*/
#define BXDEBUG_START  \
  movl %eax, %gs:(vcpu + VCPU_SCRATCH_OFF(0)); \
  movl %edx, %gs:(vcpu + VCPU_SCRATCH_OFF(1)); \
  movl $0x8ae3, %eax; \
  movl $0x8a00, %edx; \
  outl %eax, %dx; \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(0)), %eax; \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(1)), %edx

#define BXDEBUG_STOP  \
  movl %eax, %gs:(vcpu + VCPU_SCRATCH_OFF(0)); \
  movl %edx, %gs:(vcpu + VCPU_SCRATCH_OFF(1)); \
  movl $0x8ae2, %eax; \
  movl $0x8a00, %edx; \
  outl %eax, %dx; \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(0)), %eax; \
  movl %gs:(vcpu + VCPU_SCRATCH_OFF(1)), %edx

/*
#define dump_state()            asm(xstr(DUMP_STATE))
#define inspect_memory(x)       asm(xstr(INSPECT_MEMORY(x)))
#define monitor_memory_start(x) asm(xstr(MONITOR_MEMORY_START(x)))
#define monitor_memory_stop(x)  asm(xstr(MONITOR_MEMORY_STOP(x)))
#define print_errormsg()        asm(xstr(PRINT_ERRORMSG))
#define profile_reset()         asm(xstr(PROFILE_RESET))
*/
#define bxdebug_start()         asm(xstr(BXDEBUG_START))
#define bxdebug_stop()          asm(xstr(BXDEBUG_STOP))


/* Mdebug_levels:
 * 8 : print state ignoring rep instructions
 * 9 : print only memory accesses
 * 10: print state
 */


#endif /* lib/bxdebug.h */
