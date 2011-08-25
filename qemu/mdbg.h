#ifndef __MDBG_H
#define __MDBG_H
#include <stdint.h>

#define MAX_MEMORY_MONITORS 256

extern int mdbg_level;
extern uint32_t inspect_memory_addr;
extern FILE *mdbg_fp;

#define MDBGn(n,x,args...) do {                                               \
  if (mdbg_level >= n)  {                                                     \
    if (!mdbg_fp) {                                                           \
      mdbg_fp = fopen("/tmp/log.mdbg", "w");                                  \
    }                                                                         \
    fprintf(mdbg_fp, "%s() %d:" x, __func__, __LINE__, ##args);               \
    fflush(mdbg_fp);                                                          \
  }                                                                           \
} while(0)

#define MDBG MDBG2
#define MDBG2(...) MDBGn(2,__VA_ARGS__)
#define MDBG3(...) MDBGn(3,__VA_ARGS__)

#define xstr(x) _xstr(x)
#define _xstr(...) #__VA_ARGS__
#ifndef ASSERT
#define ASSERT(CONDITION) do {if (CONDITION) { } else { *(char *)0 = 0; }}while(0)
#endif

extern int num_memory_monitors;
extern uint64_t memory_monitors[MAX_MEMORY_MONITORS];
extern uint64_t prev_memory_monitor_value[MAX_MEMORY_MONITORS];

extern uint64_t n_exec;
extern uint64_t next_breakpoint;

#define LOAD_REPLAY_LOG 0x1234

#define PANIC_EXITCODE 13
#define MISMATCH_EXITCODE 13
#define INSN_COUNT_ERROR_EXITCODE 14

#endif
