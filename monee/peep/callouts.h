#ifndef PEEP_CALLOUTS_H
#define PEEP_CALLOUTS_H
#include <stdbool.h>
#include <types.h>
#include "peep/cpu_constraints.h"

struct intr_frame;
struct monitor_t;
struct operand_t;
struct insn_t;

void forced_callout (struct intr_frame *f);
bool guest_intr_handler(struct intr_frame *frame);
bool guest_handle_exception(struct intr_frame *f,
    cpu_constraints_t cpu_constraints);
void execute_code_in_intr_frame_context(struct intr_frame *f,
		uint8_t *tpage, size_t tlen);
void intr_frame_2_vcpu(struct intr_frame *f);
void vcpu_2_intr_frame(struct intr_frame *f);

void monitor_2_vcpu(struct monitor_t *m);
void vcpu_2_monitor(struct monitor_t *m);

void intr_frame_2_monitor(struct monitor_t *m, struct intr_frame *f);
void monitor_2_intr_frame(struct intr_frame *f, struct monitor_t *m);

void intr_guest_init(void);
void handle_pending_interrupts(uint8_t *tc_ptr);

void callout_print_stats(void);

#endif
