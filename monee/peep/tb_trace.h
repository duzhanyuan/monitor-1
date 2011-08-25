#ifndef PEEP_TB_TRACE_H
#define PEEP_TB_TRACE_H

#include <types.h>
#include <stdlib.h>

struct tb_t;

void tb_trace_malloc_add(target_phys_addr_t start, size_t len,
    void (*callback)(struct tb_t *tb));

void tb_trace_malloc_remove(target_phys_addr_t start, size_t len,
    void (*callback)(struct tb_t *tb));

void tb_trace_free_add(struct tb_t *tb, void (*callback)(struct tb_t *tb));
void tb_trace_free_remove(struct tb_t *tb, void (*callback)(struct tb_t *tb));

void tb_trace_malloced(struct tb_t *tb);
void tb_trace_freed(struct tb_t *tb);

#endif /* peep/tb_trace.h */
