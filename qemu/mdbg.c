#include <stdlib.h>
#include <stdio.h>
#include "mdbg.h"

int mdbg_level = 0;
uint32_t inspect_memory_addr = 0;
FILE *mdbg_fp = NULL;

int num_memory_monitors = 0;
uint64_t memory_monitors[MAX_MEMORY_MONITORS];
uint64_t prev_memory_monitor_value[MAX_MEMORY_MONITORS];
