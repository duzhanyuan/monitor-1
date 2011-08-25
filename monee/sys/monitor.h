#ifndef SYS_MONITOR_H
#define SYS_MONITOR_H

#include "peep/insntypes.h"

typedef struct monitor_t {
  //uint32_t regs[NUM_REGS];      /* eax, ecx, edx, ebx, ebp, esi, edi */
	uint32_t eax, ecx, edx, ebx, ebp, esi, edi;
	void *esp;
	uint16_t es, ss, ds, fs;
  uint32_t eflags;
  //uint16_t segs[NUM_SEGS];      /* es, fs, gs, cs, ss, ds */
  void (*eip)(void);
} monitor_t;

extern monitor_t monitor;
extern monitor_t *last_monitor_context;

void monitor_init(void);

#endif
