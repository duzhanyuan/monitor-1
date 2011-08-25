#ifndef __LIB_RANDOM_H
#define __LIB_RANDOM_H

#include <stddef.h>
#include <stdint.h>

void random_init (unsigned seed);
void random_bytes (void *, size_t);
unsigned long random_ulong (void);
uint64_t random_u64 (void);

#endif /* lib/random.h */
