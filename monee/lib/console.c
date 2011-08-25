#include <console.h>
#include <outdev.h>
#include <stdarg.h>
#include <stdio.h>
#include "devices/serial.h"
#include "devices/vga.h"
#include "sys/init.h"
#include "sys/interrupt.h"
#include "threads/synch.h"

static void vprintf_helper (char, void *);
static void log_vprintf_helper (char, void *);
static void putchar_have_lock (uint8_t c);
static void log_putchar_have_lock (uint8_t c);

static struct outdev_t logdev;
static struct outdev_t console;

/* Number of characters written to outdev. */
static int64_t write_cnt;


void
console_init (void) 
{
#ifdef __MONITOR__
  outdev_init(&logdev, "Log-console", &log_vprintf_helper, &log_putchar_have_lock);
#else
  outdev_init(&console, "Console", &vprintf_helper, &putchar_have_lock);
#endif
}

/* Notifies the console that a kernel panic is underway,
   which warns it to avoid trying to take the console lock from
   now on. */
void
console_panic (void) 
{
#ifdef __MONITOR__
  outdev_panic(&logdev);
#else
  outdev_panic(&console);
#endif
}

/* Prints console statistics. */
void
console_print_stats (void) 
{
  printf ("Console: %lld characters output\n", write_cnt);
}

/* The standard vprintf() function,
   which is like printf() but uses a va_list.
   Writes its output to both vga display and serial port. */
int
vprintf (const char *format, va_list args) 
{
#ifdef __MONITOR__
  return outdev_vprintf(&logdev, format, args);
#else
  return outdev_vprintf(&console, format, args);
#endif
}

/* Writes string S to the console, followed by a new-line
   character. */
int
puts (const char *s) 
{
#ifdef __MONITOR__
  return outdev_puts(&logdev, s);
#else
  return outdev_puts(&console, s);
#endif
}

/* Writes the N characters in BUFFER to the console. */
void
putbuf (const char *buffer, size_t n) 
{
#ifdef __MONITOR__
  return outdev_putbuf(&logdev, buffer, n);
#else
  return outdev_putbuf(&console, buffer, n);
#endif

}

/* Writes C to the vga display and serial port. */
int
putchar (int c) 
{
#ifdef __MONITOR__
  return outdev_putchar(&logdev, c);
#else
  return outdev_putchar(&console, c);
#endif
}

/* Helper function for vprintf(). */
static void
vprintf_helper (char c, void *char_cnt_) 
{
  int *char_cnt = char_cnt_;
  (*char_cnt)++;
  putchar_have_lock (c);
}

/* Writes C to the vga display and serial port.
   The caller has already acquired the console lock if
   appropriate. */
static void
putchar_have_lock (uint8_t c) 
{
  ASSERT (outdev_locked_by_current_thread (&console));
  write_cnt++;
  serial_putc (c);
  //vga_putc (c);
}

/* Helper function for vprintf() for logdev. */
static void
log_vprintf_helper (char c, void *char_cnt_) 
{
  int *char_cnt = char_cnt_;
  (*char_cnt)++;
  log_putchar_have_lock (c);
}

/* Writes C to the log console (serial port).
   The caller has already acquired the logdev lock if
   appropriate. */
static void
log_putchar_have_lock (uint8_t c) 
{
  ASSERT (outdev_locked_by_current_thread (&logdev));
  serial_putc (c);
}
