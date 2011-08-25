#ifndef __MEM_PT_MODE_H
#define __MEM_PT_MODE_H

#include <lib/types.h>

typedef target_ulong pt_mode_t;

struct intr_frame;

#ifdef __MONITOR__
pt_mode_t switch_to_phys(void);
pt_mode_t switch_to_shadow(int);
void switch_pt(pt_mode_t pt_mode);
pt_mode_t read_cr3(void);
void pt_reload(void);
#else
#define switch_to_phys() (0)
#define switch_to_shadow(x) (0)
#define switch_pt(x)
#endif

#endif /* mem/pt_mode.h */
