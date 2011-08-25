#include "sys/io.h"
#include <types.h>
#include <stdlib.h>
#include <stdio.h>
#include "devices/disk.h"
#include "devices/serial.h"
#include "mem/vaddr.h"
#include "sys/bootsector.h"
#include "sys/vcpu.h"

/* XXX: use a two level table to reduce memory usage. */
#define MAX_IOPORTS 65536
static IOPortReadFunc *ioport_read_table[3][MAX_IOPORTS];
static IOPortWriteFunc *ioport_write_table[3][MAX_IOPORTS];
void *ioport_opaque[MAX_IOPORTS];
bool needs_log[MAX_IOPORTS];

static void init_ioports(void);
static int get_bsize(int size);

void
io_out(uint16_t port, target_ulong data, size_t data_size)
{
  int bsize;
	LOG(IOPORT, "%s(%#hx, %#x, %#x) called.\n", __func__, port, data, data_size);

  bsize = get_bsize(data_size);
  ASSERT(bsize != -1);

  if (ioport_write_table[bsize][port]) {
		LOG(IOPORT, "%s(): ioport_write_table[%d][%x]=%p.\n", __func__, bsize,
				port, ioport_write_table[bsize][port]);
    (*ioport_write_table[bsize][port])(ioport_opaque[port], port, data);
    return;
  }

  if (vcpu.replay_log) {
    return;
  }

  switch (data_size) {
    case 1: outb(port, data); break;
    case 2: outw(port, data); break;
    case 4: outl(port, data); break;
    default: ASSERT(0);
  }
}

void
io_outs(uint16_t port, const void *ptr, size_t cnt, size_t data_size)
{
	static uint32_t (*ld[3])(target_ulong ptr) = {ldub, lduw, ldl};
	static void (*out[3])(uint16_t port, uint32_t data) = {outb, outw, outl};
	target_ulong addr;
  int bsize;
	unsigned i;

	bsize = get_bsize(data_size);
	ASSERT(bsize != -1);
	addr = (target_ulong)ptr;

	LOG(IOPORT, "%s(%#hx,%x,%zx,%zx) called.\n", __func__, port, addr,
			cnt, data_size);

	for (i = 0; i < cnt; i ++) {
		uint32_t data;

		data = (*ld[bsize])(addr + i*data_size);
		if (ioport_write_table[bsize][port]) {
			(*ioport_write_table[bsize][port])(ioport_opaque[port], port, data);
		} else {
			(*out[bsize])(port, data);
		}
	}
}

void
io_ins(uint16_t port, void *ptr, size_t cnt, size_t data_size)
{
  int bsize, bnum;
	unsigned i;
	target_ulong addr;
	static void (*st[3])(target_ulong ptr, uint32_t val) = {stub, stuw, stl};
	static uint32_t (*in[3])(uint16_t port) = {inb, inw, inl};

	addr = (target_ulong)ptr;
	bsize = get_bsize(data_size);
	ASSERT(bsize != -1);
	LOG(IOPORT, "%s(%#hx,%x,%zx,%zx) called.\n", __func__, port, addr,
			cnt, data_size);

	for (i = 0; i < cnt; i ++) {
		uint32_t data;
		if (ioport_read_table[bsize][port]) {
			data = (*ioport_read_table[bsize][port])(ioport_opaque[port], port);
		} else {
			data = (*in[bsize])(port);
		}
		(*st[bsize])(addr + i*data_size, data);
	}
}

#define io_in(suffix, type, data_size)                                      \
  type io_in##suffix(uint16_t port) {                                       \
    int bsize;                                                              \
    type ret;                                                               \
    bsize = get_bsize(data_size);                                           \
    ASSERT(bsize != -1);                                                    \
                                                                            \
    LOG(IOPORT, "%s(%#hx) called.\n", __func__, port);                      \
    if (ioport_read_table[bsize][port]) {                                   \
      ret=(*ioport_read_table[bsize][port])(ioport_opaque[port],port);      \
      return ret;                                                           \
    }                                                                       \
    ret = in##suffix(port);                                                 \
    return ret;                                                             \
  }

io_in(b, uint8_t, 1);
io_in(w, uint16_t, 2);
io_in(l, uint32_t, 4);

void
io_init(void)
{
  init_ioports();
}

/* size is the word size in byte */
int
register_ioport_read(int start, int length, int size,
    IOPortReadFunc *func, void *opaque, bool log)
{
  int i, bsize;

  bsize = get_bsize(size);
  if (bsize == -1) {
    return -1;
  }
  for(i = start; i < start + length; i += size) {
    ioport_read_table[bsize][i] = func;
    if (ioport_opaque[i] != NULL && ioport_opaque[i] != opaque) {
      LOG(HW, "register_ioport_read: invalid opaque");
    }
    ioport_opaque[i] = opaque;
		needs_log[i] = log;
  }
  return 0;
}

/* size is the word size in byte */
int
register_ioport_write(int start, int length, int size,
    IOPortWriteFunc *func, void *opaque, bool log)
{
  int i, bsize;

  bsize = get_bsize(size);
  if (bsize == -1) {
    return -1;
  }
  for(i = start; i < start + length; i += size) {
    ioport_write_table[bsize][i] = func;
    if (ioport_opaque[i] != NULL && ioport_opaque[i] != opaque) {
      LOG(HW, "register_ioport_write: invalid opaque");
    }
    ioport_opaque[i] = opaque;
		needs_log[i] = log;
  }
  return 0;
}

static void
init_ioports(void)
{
  int i;
  for (i = 0; i < MAX_IOPORTS; i++) {
    int j;
    for (j = 0; j < 3; j++) {
      ioport_read_table[j][i] = NULL;
      ioport_write_table[j][i] = NULL;
    }
		needs_log[i] = true;
  }
}

static int
get_bsize(int size)
{
  if (size == 1) {
    return 0;
  } else if (size == 2) {
    return 1;
  } else if (size == 4) {
    return 2;
  } else {
    LOG(HW, "register_ioport_read: invalid size");
    return -1;
  }
}

bool
ioport_needs_log(uint16_t port)
{
	return needs_log[port];
}
