/*
 * Block driver for FIFO files
 * 
 * Copyright (c) 2006 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <aio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <execinfo.h>
#include "vl.h"
#include "block_int.h"
#include "mdbg.h"

#define ASSERT assert

#ifndef QEMU_TOOL
#include "exec-all.h"
#endif


static uint8_t cur_buffer[512];
static int64_t cur_offset = 0;
static BlockDriverState *cur_write_state = NULL;

typedef struct BDRVFifoState {
    int fd;
    int flags;
} BDRVFifoState;

static void fifo_flush(BlockDriverState *bs);

static void hup_handler(int sig)
{
  fifo_flush(cur_write_state);
  exit(0);
}

static int fifo_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVFifoState *s = bs->opaque;
    int fd, open_flags, ret;

    open_flags = flags | O_BINARY;

    //mkfifo(filename, 0644);

    while (((fd = open(filename, open_flags, 0644)) < 0) && errno == EINTR) {
    }

    if (fd < 0) {
        ret = -errno;
        if (ret == -EROFS)
            ret = -EACCES;
        return ret;
    }
    s->fd = fd;
    s->flags = flags;
    signal(SIGHUP, hup_handler);
    return 0;
}

static int fifo_read(BlockDriverState *bs, int64_t offset, 
                      uint8_t *buf, int nb_sectors)
{
  BDRVFifoState *s = bs->opaque;
  int ret, count, remaining;
  uint8_t *end;

  count = nb_sectors * 512;
  remaining = count;
  end = buf + count;

  if (s->flags == BDRV_O_APPEND) {
    int nptrs, j;
#define SIZE 100
    void *buffer[SIZE];

    nptrs = backtrace(buffer, SIZE);
    printf("read() called on append-only log. backtrace() returned "
        "%d addresses\n", nptrs);
    printf("Call stack:");

    for (j = 0; j < nptrs; j++) {
      printf(" %p", buffer[j]);
    }
    printf("\n");

    return -1;
  }

  do {
    ret = read(s->fd, end - remaining, remaining);
    if (ret != -1) {
      remaining -= ret;
    }
  } while (remaining);

  return count;
}

static void
fifo_write_sector(BDRVFifoState *s, uint8_t const *buf)
{
  int remaining;
  uint8_t const *end;

  remaining = 512;
  end = buf + 512;

  do {
    int ret;

    ret = write(s->fd, end - remaining, remaining);
    if (ret != -1) {
      remaining -= ret;
    }
  } while (remaining);
}

static int
fifo_write(BlockDriverState *bs, int64_t offset, 
                      const uint8_t *buf, int nb_sectors)
{
  BDRVFifoState *s = bs->opaque;
  int count;
  uint8_t const *ptr;
  int i;

  if (!cur_write_state) {
    cur_write_state = bs;
  }
  count = nb_sectors * 512;
  //printf("%s(): offset=%llx, cur_offset=%llx\n", __func__, offset, cur_offset);
  ASSERT(offset == cur_offset || offset == (cur_offset + 1));
  ASSERT(bs == cur_write_state);

#if 0
  printf("fifo_write(%llx, %d): ", offset, count);
  for (i = 0; i < count; i++) {
    printf("%c", buf[i]);
  }
  printf("\n");
#endif

  if (s->flags == BDRV_O_RDONLY) {
   int nptrs, j;
#define SIZE 100
    void *buffer[SIZE];

    nptrs = backtrace(buffer, SIZE);
    printf("write() called on read-only log. backtrace() returned "
        "%d addresses\n", nptrs);
    printf("Call stack:");

    for (j = 0; j < nptrs; j++) {
      printf(" %p", buffer[j]);
    }
    printf("\n");
  } while(0);

  if (offset == cur_offset + 1) {
    fifo_write_sector(s, cur_buffer);
    cur_offset++;
  }
  ASSERT(offset == cur_offset);
  ptr = buf;
  for (i = 0; i < nb_sectors - 1; i++) {
    fifo_write_sector(s, ptr);
    ptr += 512;
    cur_offset++;
  }
  memcpy(cur_buffer, ptr, 512);

	return 0;
  //return count;
}

static void fifo_close(BlockDriverState *bs)
{
    BDRVFifoState *s = bs->opaque;
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
}

static int fifo_truncate(BlockDriverState *bs, int64_t offset)
{
  ASSERT(0);
}


static int fifo_create(const char *filename, int64_t total_size,
                       const char *backing_file, int flags)
{
    int fd;

    fd = mkfifo(filename, 0644);
    if (fd < 0)
        return -EIO;
    close(fd);
    return 0;
}

static void fifo_flush(BlockDriverState *bs)
{
  BDRVFifoState *s = bs->opaque;
  fifo_write_sector(s, cur_buffer);
}

static int64_t fifo_getlength(BlockDriverState *bs)
{
  /* Return a large number. 4G. */
  return 0xffffffff;
}

BlockDriver bdrv_fifo = {
    "fifo",
    sizeof(BDRVFifoState),
    NULL, /* no probe for protocols */
    fifo_open,
    fifo_read,
    fifo_write,
    fifo_close,
    fifo_create,
    fifo_flush,
    
    .bdrv_aio_read = NULL,
    .bdrv_aio_write = NULL,
    .bdrv_aio_cancel = NULL,
    .aiocb_size = 0,
    .protocol_name = "fifo",
    .bdrv_pread = NULL,
    .bdrv_pwrite = NULL,
    .bdrv_truncate = fifo_truncate,
    .bdrv_getlength = fifo_getlength,
};
