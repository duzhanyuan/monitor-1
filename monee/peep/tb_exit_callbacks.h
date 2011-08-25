#ifndef PEEP_TB_EXIT_CALLBACKS_H
#define PEEP_TB_EXIT_CALLBACKS_H
#include <stddef.h>

struct malloc_cb;
void tb_exit_callbacks_init(void);
void register_tb_exit_callback(void (*func)(void *opaque), void *opaque,
		struct malloc_cb *malloc_cb);
void process_tb_exit_callbacks(void);

#endif
