#ifndef __LIB_OUTDEV_H
#define __LIB_OUTDEV_H
#include <stdio.h>
#include "threads/synch.h"

typedef struct outdev_t {
  char const *name;

  /* The outdev lock.
     Both the vga and serial layers do their own locking, so it's
     safe to call them at any time.
     But this lock is useful to prevent simultaneous printf() calls
     from mixing their output, which looks confusing. */
  struct lock lock;

  /* True in ordinary circumstances: we want to use the outdev
     lock to avoid mixing output between threads, as explained
     above.

     False in early boot before the point that locks are functional
     or the outdev lock has been initialized, or after a kernel
     panics.  In the former case, taking the lock would cause an
     assertion failure, which in turn would cause a panic, turning
     it into the latter case.  In the latter case, if it is a buggy
     lock_acquire() implementation that caused the panic, we'll
     likely just recurse. */
  bool use_lock;

  /* It's possible, if you add enough debug output to Pintos, to
     try to recursively grab outdev_lock from a single thread.  As
     a real example, I added a printf() call to palloc_free().
     Here's a real backtrace that resulted:

     lock_outdev()
     vprintf()
     printf()             - palloc() tries to grab the lock again
     palloc_free()        
     schedule_tail()      - another thread dying as we switch threads
     schedule()
     thread_yield()
     intr_handler()       - timer interrupt
     intr_set_level()
     serial_putc()
     putchar_have_lock()
     putbuf()
     sys_write()          - one process writing to the outdev
     syscall_handler()
     intr_handler()

     This kind of thing is very difficult to debug, so we avoid the
     problem by simulating a recursive lock with a depth
     counter. */
  int lock_depth;

  void (*vprintf_helper)(char, void *);
  void (*putchar_have_lock)(uint8_t);
} outdev_t;

void outdev_init(outdev_t *outdev, char const *name,
    void (*vprintf_helper)(char, void *), void (*putchar_have_lock)(uint8_t));
void outdev_panic(outdev_t *outdev);
void outdev_print_stats(outdev_t *outdev);
bool outdev_locked_by_current_thread (outdev_t *outdev);

int outdev_vprintf (outdev_t *outdev, char const *format, va_list args);
int outdev_puts(outdev_t *outdev, char const *s);
void outdev_putbuf(outdev_t *outdev, char const *s, size_t n);
int outdev_putchar(outdev_t *outdev, int c);

#endif /* lib/outdev.h */
