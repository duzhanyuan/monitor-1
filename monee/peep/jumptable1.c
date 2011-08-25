#include "peep/jumptable1.h"
#include <debug.h>
#include <string.h>

jumptable1_entry_t jumptable1[JUMPTABLE1_SIZE];

void
jumptable1_init(void)
{
  jumptable1_clear();
}

void
jumptable1_add(uint32_t eip, uint32_t tc_ptr)
{
#ifndef NO_JUMPTABLE1
  /*
  if (*(uint32_t *)((uint8_t *)jumptable1 + (eip & JUMPTABLE1_MASK)) == eip) {
    log_printf("%s(): Adding duplicate %#x at %#x.\n", __func__, eip,
        eip & JUMPTABLE1_MASK);
  }
  */
	ASSERT(tc_ptr);		/* We rely on tc_ptr being non-zero. */
  *(uint32_t *)((uint8_t *)jumptable1 + (eip & JUMPTABLE1_MASK)) = eip;
  *(uint32_t *)((uint8_t *)jumptable1 + (eip & JUMPTABLE1_MASK) + 4) = tc_ptr;
#endif
}

void
jumptable1_remove(uint32_t eip)
{
  *(uint32_t *)((uint8_t *)jumptable1 + (eip & JUMPTABLE1_MASK)) = 0;
  *(uint32_t *)((uint8_t *)jumptable1 + (eip & JUMPTABLE1_MASK) + 4) = 0;
}

void
jumptable1_clear(void)
{
  memset(jumptable1, 0x0, sizeof jumptable1);
}
