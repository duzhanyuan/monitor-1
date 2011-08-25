#ifndef SYS_HW_H
#define SYS_HW_H

#include <stdint.h>

typedef void IOEventHandler(void *opaque, int event);
typedef void IOReadHandler(void *opaque, const uint8_t *buf, int size);
typedef int IOCanRWHandler(void *opaque);
typedef void IOHandler(void *opaque);

//typedef void SetIRQFunc(void *opaque, int irq_num, int level);

#endif
