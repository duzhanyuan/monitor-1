#ifndef PEEP_JUMPTABLE1_H
#define PEEP_JUMPTABLE1_h
#include <stdint.h>

#define JUMPTABLE1_SIZE 4096
#define JUMPTABLE1_MASK ((JUMPTABLE1_SIZE*8 - 1) & ~0x7)

typedef struct jumptable1_entry_t {
  uint32_t eip;
  uint32_t tc_ptr;
} jumptable1_entry_t;

void jumptable1_init(void);
void jumptable1_add(uint32_t eip, uint32_t tc_ptr);
void jumptable1_remove(uint32_t eip);
void jumptable1_clear(void);

#endif
