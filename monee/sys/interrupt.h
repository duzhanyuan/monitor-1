#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>
#include <lib/types.h>

#define FORCED_CALLOUT 255

struct tb_t;
/* Interrupts on or off? */
enum intr_level 
  {
    INTR_OFF,             /* Interrupts disabled. */
    INTR_ON               /* Interrupts enabled. */
  };

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* Interrupt stack frame. */
struct intr_frame
  {
    /* Pushed by intr_entry in intr-stubs.S.
       These are the interrupted task's saved registers. */
    uint32_t edi;               /* Saved EDI. */
    uint32_t esi;               /* Saved ESI. */
    uint32_t ebp;               /* Saved EBP. */
    uint32_t esp_dummy;         /* Not used. */
    uint32_t ebx;               /* Saved EBX. */
    uint32_t edx;               /* Saved EDX. */
    uint32_t ecx;               /* Saved ECX. */
    uint32_t eax;               /* Saved EAX. */
    uint16_t gs, :16;           /* Saved GS segment register. */
    uint16_t fs, :16;           /* Saved FS segment register. */
    uint16_t es, :16;           /* Saved ES segment register. */
    uint16_t ds, :16;           /* Saved DS segment register. */

    /* Pushed by intrNN_stub in intr-stubs.S. */
    uint32_t vec_no;            /* Interrupt vector number. */

    /* Sometimes pushed by the CPU,
       otherwise for consistency pushed as 0 by intrNN_stub.
       The CPU puts it just under `eip', but we move it here. */
    uint32_t error_code;        /* Error code. */

    /* Pushed by intrNN_stub in intr-stubs.S.
       This frame pointer eases interpretation of backtraces. */
    void *frame_pointer;        /* Saved EBP (frame pointer). */

    /* Pushed by the CPU.
       These are the interrupted task's saved registers. */
    void (*eip) (void);         /* Next instruction to execute. */
    uint16_t cs, :16;           /* Code segment for eip. */
    uint32_t eflags;            /* Saved CPU flags. */
    void *esp;                  /* Saved stack pointer. */
    uint16_t ss, :16;           /* Data segment for esp. */
  };

/* The stack frame that is formed in a call to intr_handler
 * (see sys/intr-stubs.S).
 */
struct intr_handler_stack_frame {
  struct intr_frame if_;        /* instr-stubs.S */
  void *intr_frame_pointer;     /* pushed by intr-stubs.S */
  void (*eip)(void);            /* call */
  void (*ebp)(void);            /* pushed by intr_handler. */
  char padding[128];            /* compiler may add some extra frames on esp. */
  //char padding[32];            /* compiler may add some extra frames on esp. */
};

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);

void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
void unregister_handler (uint8_t vec_no);
bool intr_is_registered(uint8_t vec_no);
void intr_irq_mask(int irq);
void intr_irq_unmask(int irq);

bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);


void read_orig_intr_gate (int intno, void (**function)(void), int *dpl);

void raise_interrupt(int intno,int is_int_insn, int error_code,
    int next_eip_addend);
void raise_exception_err(int exception_index, int error_code);

void iret_real(void);
void iret_protected(void);


void do_interrupt(unsigned intno, int is_int, int error_code, uint32_t next_eip,
    int is_hw);

#endif /* threads/interrupt.h */
