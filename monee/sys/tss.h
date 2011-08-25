#ifndef SYS_TSS_H
#define SYS_TSS_H

#include <stdint.h>

struct tss;
void tss_init (void);
struct tss *tss_get (void);
void tss_update (void);

#endif /* sys/tss.h */
