#ifndef PEEP_I386_DIS_H
#define PEEP_I386_DIS_H
#include <lib/stdlib.h>
#include <lib/types.h>
#include <lib/stdbool.h>

struct insn_t;
void opc_init(void);
int sprint_insn(unsigned long pc, char *buf, size_t buf_size, unsigned size,
		bool guest);
int disas_insn(unsigned char const *code, target_ulong eip,
    struct insn_t *insn, unsigned size, bool guest);

#endif
