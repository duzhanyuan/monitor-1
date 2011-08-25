#ifndef MEM_MALLOC_CB_H
#define MEM_MALLOC_CB_H

struct malloc_cb
{
	void *(*malloc)(size_t size);
	void (*lock)(void *opaque);
	void (*unlock)(void *opaque);
};

#endif 	/* mem/malloc_cb.h */
