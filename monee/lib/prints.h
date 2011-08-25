#ifndef PRINTS_H
#define PRINTS_H

#include <stdio.h>
#include "sys/io.h"

static int
prints(char const *fmt, ...)
{
  va_list args;
  char buf[256], *ptr;
  int i;

  va_start(args, fmt);
  i = vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);

  buf[255] = '\0';

  for (ptr = buf; *ptr; ptr++) {
    outb(0x1234, *ptr);
  }
  return i;
}

#endif
