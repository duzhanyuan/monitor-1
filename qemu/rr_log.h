#ifndef RR_LOG_H
#define RR_LOG_H

typedef enum rr_log_tag_t {
  RR_LOG_END = 0,
  RR_LOG_TAG_MS = 1,
  RR_LOG_TAG_IN = 2,
  RR_LOG_TAG_INS = 3,
  RR_LOG_TAG_INTR = 4,
  RR_LOG_TAG_PANIC = 5,
  RR_LOG_TAG_EXIT = 6,
} rr_log_tag_t;
extern FILE *rr_log;
enum rr_log_tag_t rr_log_read_next(void);

#endif
