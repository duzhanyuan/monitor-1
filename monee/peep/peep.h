#ifndef PEEP_PEEP_H
#define PEEP_PEEP_H
#include <stddef.h>
#include <stdbool.h>
#include <lib/types.h>
#include "peep/cpu_constraints.h"

#define PEEP_PREFIX peep_
#define ROLLBACK_PREFIX rb_
struct rollbacks_t;

void peep_init(void);
void set_max_tu_size(int size);
size_t translate(uint8_t *code, target_ulong eip_virt, void *buf, size_t buf_size,
		struct rollbacks_t *rollbacks, size_t *tb_len, uint16_t *edge_offsets,
		uint16_t *jmp_offsets, uint8_t *eip_boundaries, uint16_t *tc_boundaries,
		char **peep_string, size_t *num_insns,
		cpu_constraints_t const *cpu_constraints);

struct insn_t;
size_t
peep_translate(void *buf, size_t buf_size, struct insn_t *insns,
    int n_insns, uint16_t *edge_offsets, uint16_t *jmp_offsets,
    char **rb_buf, uint16_t *rb_code_offset, uint16_t *rb_rb_offset,
    size_t *nb_rbs, target_ulong cur_addr, target_ulong fallthrough_addr,
    int is_terminating, cpu_constraints_t const *cpu_constraints,
    char *peep_string);

size_t emit_jump_indir_insn(uint8_t *optr, target_ulong target);

void *hw_memcpy(void *dst, const void *src, size_t n);
size_t rename_mem_operands_to_disps(uint8_t *obuf, size_t obuf_size,
		uint8_t const *buf, target_ulong disp0, target_ulong disp1,
		bool addr16);

#endif
