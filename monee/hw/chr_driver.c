#include "hw/chr_driver.h"
#include <debug.h>
#include <stdlib.h>
#include <stdio.h>
#include "mem/malloc.h"

CharDriverState *
chr_driver_open(char const *filename)
{
  CharDriverState *ret;
  ret = malloc(sizeof(CharDriverState));
  ASSERT(ret);
  return ret;
}

void
chr_driver_printf(CharDriverState *s, char const *fmt, ...)
{
  NOT_IMPLEMENTED();
}

int
chr_driver_write(CharDriverState *s, uint8_t const *buf, int len)
{
  int i;
  for (i = 0; i < len; i++) {
    printf("%c", buf[i]);
  }
	return len;
}

void
chr_driver_send_event(CharDriverState *s, int event)
{
  NOT_IMPLEMENTED();
}

void
chr_driver_add_handlers(CharDriverState *s,
    IOCanRWHandler *fd_can_read, IOReadHandler *fd_read,
    IOEventHandler *fd_event, void *opaque)
{
  MSG("%s()\n", __func__);
  //NOT_IMPLEMENTED();
}

int
chr_driver_ioctl(CharDriverState *s, int cmd, void *arg)
{
  //MSG("%s()\n", __func__);
  //NOT_IMPLEMENTED();
	return 0;
}

void
chr_driver_reset(CharDriverState *s)
{
  NOT_IMPLEMENTED();
}

int
chr_driver_can_read(CharDriverState *s)
{
  NOT_IMPLEMENTED();
}

void
chr_driver_read(CharDriverState *s, uint8_t *buf, int len)
{
  NOT_IMPLEMENTED();
}
