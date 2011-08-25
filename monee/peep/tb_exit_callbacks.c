#include "peep/tb_exit_callbacks.h"
#include <stdlib.h>
#include <stdio.h>
#include <list.h>
#include "mem/malloc.h"
#include "mem/malloc_cb.h"

struct list tb_exit_callbacks;

struct tb_exit_callback_entry
{
	void (*func)(void *opaque);
	void *opaque;
	struct list_elem l_elem;
};

void
tb_exit_callbacks_init(void)
{
	list_init(&tb_exit_callbacks);
}

void
register_tb_exit_callback(void (*func)(void *opaque), void *opaque,
		struct malloc_cb *malloc_cb)
{
	struct tb_exit_callback_entry *entry;
	struct list_elem *e;
	for (e = list_begin(&tb_exit_callbacks); e != list_end(&tb_exit_callbacks);
			e = list_next(e)) {
		entry = list_entry(e, struct tb_exit_callback_entry, l_elem);
		if (entry->func == func && entry->opaque == opaque) {
			/* callback already exists. */
			return;
		}
	}
	(*malloc_cb->lock)(opaque);
	entry = (*malloc_cb->malloc)(sizeof (struct tb_exit_callback_entry));
	(*malloc_cb->unlock)(opaque);
	ASSERT(entry);
	entry->func = func;
	entry->opaque = opaque;
	list_push_front(&tb_exit_callbacks, &entry->l_elem);
}

void process_tb_exit_callbacks(void)
{
	while (!list_empty(&tb_exit_callbacks)) {
		struct list_elem *e = list_pop_front(&tb_exit_callbacks);
		struct tb_exit_callback_entry *tb_exit_callback;
		tb_exit_callback = list_entry(e, struct tb_exit_callback_entry, l_elem);
		/*
		printf("%s() %d: calling callback function %p\n", __func__, __LINE__,
			tb_exit_callback->func);
			*/
		(*tb_exit_callback->func)(tb_exit_callback->opaque);
		free(tb_exit_callback);
	}
}
