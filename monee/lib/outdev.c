#include <outdev.h>
#include <stdarg.h>
#include <stdio.h>
#include "devices/serial.h"
#include "devices/vga.h"
#include "sys/init.h"
#include "sys/interrupt.h"
#include "threads/synch.h"

/* Enable outdev locking. */
void
outdev_init(outdev_t *outdev, char const *name,
    void (*vprintf_helper)(char, void *), void (*putchar_have_lock)(uint8_t))
{
  lock_init (&outdev->lock);
  outdev->use_lock = true;
  outdev->name = name;
  outdev->vprintf_helper = vprintf_helper;
  outdev->putchar_have_lock = putchar_have_lock;
  outdev->lock_depth = 0;
}

/* Notifies the outdev that a kernel panic is underway,
   which warns it to avoid trying to take the outdev lock from
   now on. */
void
outdev_panic(outdev_t *outdev)
{
  outdev->use_lock = false;
  //switch_to_kernel();
}

/* Acquires the outdev lock. */
static void
acquire_outdev(outdev_t *outdev)
{
  if (!intr_context () && outdev->use_lock)  {
    if (lock_held_by_current_thread (&outdev->lock)) {
      outdev->lock_depth++; 
    } else {
      lock_acquire (&outdev->lock); 
    }
  }
}

/* Releases the outdev lock. */
static void
release_outdev(outdev_t *outdev)
{
  if (!intr_context () && outdev->use_lock) {
    if (outdev->lock_depth > 0) {
      outdev->lock_depth--;
    } else {
      lock_release (&outdev->lock); 
    }
  }
}

/* Returns true if the current thread has the outdev lock,
   false otherwise. */
bool
outdev_locked_by_current_thread (outdev_t *outdev)
{
  return (intr_context ()
          || !outdev->use_lock
          || lock_held_by_current_thread (&outdev->lock));
}

/* The standard vprintf() function,
   which is like printf() but uses a va_list. */
int
outdev_vprintf (outdev_t *outdev, char const *format, va_list args) 
{
  int char_cnt = 0;

  acquire_outdev(outdev);
  __vprintf (format, args, outdev->vprintf_helper, &char_cnt);
  release_outdev(outdev);

  return char_cnt;
}

/* Writes string S to the outdev, followed by a new-line
   character. */
int
outdev_puts (outdev_t *outdev, char const *s) 
{
  acquire_outdev(outdev);
  while (*s != '\0') {
    (*outdev->putchar_have_lock)(*s++);
  }
  (*outdev->putchar_have_lock)('\n');
  release_outdev(outdev);

  return 0;
}

/* Writes the N characters in BUFFER to the outdev. */
void
outdev_putbuf(outdev_t *outdev, char const *buffer, size_t n) 
{
  acquire_outdev(outdev);
  while (n-- > 0) {
    (*outdev->putchar_have_lock)(*buffer++);
  }
  release_outdev(outdev);
}

/* Writes C to outdev. */
int
outdev_putchar (outdev_t *outdev, int c) 
{
  acquire_outdev(outdev);
  (*outdev->putchar_have_lock)(c);
  release_outdev(outdev);
  
  return c;
}
