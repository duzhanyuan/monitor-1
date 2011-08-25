#ifndef PEEP_JUMPTABLE_H
#define PEEP_JUMPTABLE_H
#include <types.h>

struct tb_t;
void jumptable2_init(void);
void jumptable2_add(struct tb_t *tb);
void *jumptable2_find(target_ulong eip_virt, target_ulong eip);
void jumptable2_remove(struct tb_t *tb);
void jumptable2_clear(void);

#endif
