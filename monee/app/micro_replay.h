#ifndef APP_MICRO_REPLAY_H
#define APP_MICRO_REPLAY_H

#include <stdbool.h>
#include <types.h>

void micro_replay_init(void);
void check_micro_replay(void);
void micro_replay_switch_mode(void);
bool interrupts_black_listed_eip(target_ulong eip);
void micro_replay_print_stats(void);

#endif /* app/micro_replay.h */
