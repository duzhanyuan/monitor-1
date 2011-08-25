#ifndef __SYS_MODE_H
#define __SYS_MODE_H

#include <stdint.h>

typedef enum mode_t {
  MODE_USER = 0,
  MODE_KERNEL,
  MODE_INVALID
} mode_t;

struct intr_frame;

enum mode_t switch_to_kernel(void);
enum mode_t switch_to_user(void);
void switch_mode(enum mode_t mode);
uint16_t read_cpl(void);

#endif /* sys/mode.h */
