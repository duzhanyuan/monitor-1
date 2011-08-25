#ifndef PEEP_DEBUG_H
#define PEEP_DEBUG_H

#include <debug.h>
#include <stdint.h>

#define INSN 2
#define MATCH_ALL 2

#define cur_peep_entry_is_debug() ((cur_peep_entry                       \
    && strstr(cur_peep_entry->name, "debug"))?1:2)

//#define cur_peep_entry_is_debug() 2

#define MATCH min(MATCH_ALL, cur_peep_entry_is_debug())

#endif
