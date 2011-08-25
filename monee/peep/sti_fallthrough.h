#ifndef PEEP_STI_FALLTHROUGH_H
#define PEEP_STI_FALLTHROUGH_H

#include <stdbool.h>

void add_sti_fallthrough_addr(void *ptr);
bool remove_sti_fallthrough_addr(void *ptr);

#endif  /* peep/sti_fallthrough.h */
