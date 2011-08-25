#ifndef SYS_LOADER_H
#define SYS_LOADER_H

#include "sys/bootsector.h"

/* Monitor constants. */
/* LOADER_MONITOR_BASE must be >= 1MB (high-mem) and must be <= 60 MB (to
 * prevent it from overflowing the 64 MB of memory mapped by the loader.
 * Additionally, if you wish to use 4MB pages for the monitor, this address
 * should be aligned to 4MB boundary. */

#define LOADER_MONITOR_BASE 0x400000
#define VMM_SIZE      0x400000
#define VMX_SIZE      0x400000
#define MONITOR_SIZE  (VMM_SIZE + VMX_SIZE)
#define LOADER_MONITOR_END  (LOADER_MONITOR_BASE+MONITOR_SIZE)
            /* The physical address space LOADER_MONITOR_BASE..END is always 
             * reserved for the monitor. */
#define LOADER_VMM_VIRT_BASE 0xffc00000
                                     /* Virtual address of vmm's base. */
#define LOADER_VMX_VIRT_BASE (LOADER_VMM_VIRT_BASE - VMX_SIZE)
#define LOADER_MONITOR_VIRT_BASE LOADER_VMX_VIRT_BASE

#define LOADER_PAGEDIR_BASE 0x10000

#endif /* sys/loader.h */
