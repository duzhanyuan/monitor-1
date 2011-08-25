#ifndef MEM_SIMULATE_INSN_H
#define MEM_SIMULATE_INSN_H
#include <types.h>
#include <stdbool.h>
#include <stddef.h>

struct intr_frame;

bool simulate_faulting_instruction(struct intr_frame *f,
		size_t *memaccess_size, target_ulong *fault_addr);

#endif /* mem/simulate_insn.h */
