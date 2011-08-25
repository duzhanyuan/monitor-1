#include "peep/tb_trace.h"
#include <stdlib.h>
#include <stdio.h>
#include <types.h>
#include "peep/tb.h"

void
tb_trace_malloc_add(target_phys_addr_t start, size_t len,
    void (*callback)(tb_t *tb))
{
}

void
tb_trace_malloc_remove(target_phys_addr_t start, size_t len,
    void (*callback)(tb_t *tb))
{
}

void
tb_trace_free_add(tb_t *tb, void (*callback)(tb_t *tb))
{
}

void
tb_trace_free_remove(tb_t *tb, void (*callback)(tb_t *tb))
{
}

void
tb_trace_malloced(tb_t *tb)
{
}

void
tb_trace_freed(tb_t *tb)
{
}
