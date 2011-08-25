#include "sys/syscall.h"
#include <stdio.h>
#include <mdebug.h>
#include "sys/interrupt.h"

static void syscall_handler(struct intr_frame *);

void
syscall_init(void)
{
  intr_register_int(0x80, 3, INTR_OFF, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f)
{
  /* Only the initial thread can call this function, so no races. */
  static uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi, eip, eflags;
  struct thread *running_thread(void);
  ASSERT2(thread_current() == thread_initial());

  eax = f->eax; ecx = f->ecx; edx = f->edx; ebx = f->ebx;
  eflags = f->eflags;
  esp = (uint32_t)f->esp; ebp = f->ebp; esi = f->esi; edi = f->edi;
  eip = (uint32_t)f->eip;
  asm ("pushl %0 ; popfl" : : "m"(eflags));
  asm ("movl %0, %%esp ; movl %1, %%ebp" : :  "m"(esp), "m"(ebp));
  asm ("jmp *%0" : : "m"(eip), "a"(eax), "c"(ecx), "d"(edx),
      "b"(ebx), "S"(esi), "D"(edi));
}
