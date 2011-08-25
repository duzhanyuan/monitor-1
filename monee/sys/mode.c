#include "sys/mode.h"
#include <debug.h>
#include <mdebug.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sys/gdt.h"
#include <sys/interrupt.h>
#include <sys/flags.h>

#define SWITCH_MAGIC 0x71847214

uint16_t
read_cpl(void)
{
  uint16_t cs, cpl;
  asm("mov %%cs, %w0" : "=g"(cs));
  cpl = cs & 0x3;
  //ASSERT(cpl == 0 || cpl == 3); /* discount for virtualbox bugs. */
  return cpl;
}

enum mode_t
switch_to_kernel(void)
{
  enum mode_t mode = MODE_KERNEL;

  /* There is only one thread with CPL=3, so there is no race condition here. */
  if (read_cpl() == 3) {
    asm volatile ("int $0x80");
    ASSERT(read_cpl() != 3);
    mode = MODE_USER;
  }
  return mode;
}

enum mode_t
switch_to_user(void)
{
  enum mode_t mode = MODE_USER;
  enum intr_level old_level;

  ASSERT2(thread_current() == thread_initial());
  if (read_cpl() != 3) {
    struct intr_frame if_;

    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eip = &&done_switch_to_user;
    asm("pushfl ; popl %0" : "=g"(if_.eflags));
    asm("movl %%eax, %0" : "=g"(if_.eax));
    asm("movl %%ecx, %0" : "=g"(if_.ecx));
    asm("movl %%edx, %0" : "=g"(if_.edx));
    asm("movl %%ebx, %0" : "=g"(if_.ebx));
    asm("movl %%esp, %0" : "=g"(if_.esp));
    asm("movl %%ebp, %0" : "=g"(if_.ebp));
    asm("movl %%esi, %0" : "=g"(if_.esi));
    asm("movl %%edi, %0" : "=g"(if_.edi));
    asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
done_switch_to_user:
    ASSERT2(read_cpl() == 3);
    mode = MODE_KERNEL;
  }
  return mode;
}

void
switch_mode(enum mode_t mode)
{
  ASSERT(mode == MODE_USER || mode == MODE_KERNEL);
  if (mode == MODE_USER) {
    switch_to_user();
  } else {
    switch_to_kernel();
  }
}
