#ifndef __PROFILE_LOG_H
#define __PROFILE_LOG_H
#include <stdint.h>

extern FILE *profile_log;
void profile_log_init(char const *filename);
void profile_log_increment_count(uint32_t eip);
void profile_log_dump(void);
void profile_log_reset(void);

#endif
