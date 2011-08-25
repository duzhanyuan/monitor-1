#ifndef PEEP_INSN_H
#define PEEP_INSN_H
#include <stdio.h>
#include "peep/insntypes.h"

struct insn_t;
struct regset_t;
struct assignments_t;
struct intr_frame;

int insn2str(struct insn_t const *insn, char *buf, size_t size);
int insns2str(struct insn_t const *insns, int n_insns, char *buf, size_t size);
int operand2str(operand_t const *op, char *buf, size_t size);
bool str2operand(operand_t *op, char const *str);
void insn_init(struct insn_t *insn);
void insn_get_usedef(struct insn_t const *insn, struct regset_t *use,
    struct regset_t *def);

bool insn_is_terminating(struct insn_t const *insn);
bool insn_accesses_mem16(struct insn_t const *insn, struct operand_t const **op1,
		struct operand_t const **op2);
bool insn_accesses_mem(struct insn_t const *insn, struct operand_t const **op1,
		struct operand_t const **op2);
bool insn_accesses_stack(insn_t const *insn);
struct operand_t const *insn_has_prefix(insn_t const *insn);
size_t insn_get_operand_size(insn_t const *insn);
size_t insn_get_addr_size(insn_t const *insn);
bool insn_is_conditional_jump(insn_t const *insn);
bool insn_is_direct_jump(insn_t const *insn);
bool insn_is_indirect_jump(insn_t const *insn);
bool insn_is_string_op(insn_t const *insn);
bool insn_is_movs_or_cmps(insn_t const *insn);
bool insn_is_cmps_or_scas(insn_t const *insn);
bool insn_is_push(insn_t const *insn);
bool insn_is_pop(insn_t const *insn);
bool insn_is_sti(insn_t const *insn);
void insn_rename_constants(insn_t *insn,
		struct assignments_t const *assignments);

long operand_get_value(operand_t const *op);

opertype_t str2tag(char const *tagstr);


void print_insn(insn_t const *insn);

#endif /* peep/insn.h */
