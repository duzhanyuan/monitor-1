/*
 * QEMU Monitor disk for direct communication between monitor and qemu
 * Author: Sorav Bansal
 */
#include <assert.h>
#include "vl.h"
#include "mdbg.h"
#include "block_int.h"

#define IOBASE 0x2345
#define SET_ADDR 0x123456
#define SET_LEN 0x234567
#define START_TRANSFER 0x345678
#define SET_BLOCK 0x456789
#define SET_OP 0x56789a

#define READ 0
#define WRITE 1

static int cur_io_base = IOBASE;
static int num_mdisk_devices = 0;

static uint32_t mdisk_control_read(void *opaque, uint32_t addr);
static void mdisk_control_write(void *opaque, uint32_t addr, uint32_t val);

struct mdisk_struct
{
  BlockDriverState *bs;
  char filename[64];
  char *filename_ptr;
  int iobase;
  int op;

  uint32_t ptr;     /* start ptr of data to be transferred. */
  uint32_t len;     /* length of data to be transferred. */
  uint32_t block;   /* block (sector) number on disk to transfer. */

  enum { OP, ADDR, LEN, BLOCK, NONE } state;
};

int
mdisk_device_add(char *filename)
{
  struct mdisk_struct *mdisk;
  char mdisk_name[64];
  BlockDriverState *bdrv;
  int flags = BDRV_O_LOG;

  snprintf(mdisk_name, sizeof mdisk_name, "mdisk%d", num_mdisk_devices++);
  bdrv = bdrv_new(mdisk_name);

	//printf("%s(): %s. cur_io_base=0x%x\n", __func__, filename, cur_io_base);
  //flags |= strstr(filename, ".wr.fifo")?BDRV_O_APPEND:BDRV_O_RDONLY;
  flags |= strstr(filename, ".wr.fifo")?BDRV_O_APPEND:BDRV_O_RDWR;

  if (bdrv_open(bdrv, filename, flags) < 0) {
    return -1;
  }
  mdisk = qemu_mallocz(sizeof *mdisk);
  ASSERT(mdisk);
  strncpy(mdisk->filename, filename, sizeof mdisk->filename);
  mdisk->filename[sizeof mdisk->filename - 1] = '\0';
  mdisk->bs = bdrv;
  //mdisk->readonly = strstr(filename, ".wr.fifo")?0:1;
  mdisk->iobase = cur_io_base;
  mdisk->state = NONE;
  mdisk->filename_ptr = mdisk->filename;
  register_ioport_read(cur_io_base, 4, 4, mdisk_control_read, mdisk);
  register_ioport_write(cur_io_base, 4, 4, mdisk_control_write, mdisk);
  cur_io_base += 4;
  return 0;
}

static uint32_t
mdisk_control_read(void *opaque, uint32_t addr)
{
  struct mdisk_struct *mdisk;
  uint32_t ret;

  mdisk = (struct mdisk_struct *)opaque;
	//printf("%s(): addr=0x%x\n", __func__, addr);
  ASSERT(mdisk->iobase == addr);
  ret = *mdisk->filename_ptr;
  if (   ret == '\0'
      || (mdisk->filename_ptr - mdisk->filename) == sizeof mdisk->filename) {
    mdisk->filename_ptr = mdisk->filename;
  } else {
    mdisk->filename_ptr++;
  }
  mdisk->state = NONE;
  return ret;
}

static void
mdisk_control_write(void *opaque, uint32_t addr, uint32_t val)
{
#define DISK_SECTOR_SIZE 512
  struct mdisk_struct *mdisk;
  mdisk = (struct mdisk_struct *)opaque;
	//printf("%s(): addr=0x%x, val=0x%x\n", __func__, addr, val);

  ASSERT(mdisk->iobase == addr);
  switch (mdisk->state) {
    case NONE:
      if (val == SET_ADDR) {
        mdisk->state = ADDR;
      } else if (val == SET_LEN) {
        mdisk->state = LEN;
      } else if (val == SET_BLOCK) {
        mdisk->state = BLOCK;
			} else if (val == SET_OP) {
				mdisk->state = OP;
      } else if (val == START_TRANSFER) {
        size_t BUF_SIZE = 65536;
        char buffer[BUF_SIZE];

        //printf("Starting transfer: (0x%x,0x%x)\n", mdisk->ptr, mdisk->len);
        while (mdisk->len > 0) {
          int ret;
          uint32_t txsize;
          txsize = MIN(mdisk->len, BUF_SIZE);
          ASSERT((txsize % DISK_SECTOR_SIZE) == 0);
          if (mdisk->op == READ) {
            ret = bdrv_read(mdisk->bs, mdisk->block, buffer,
                txsize/DISK_SECTOR_SIZE);
            cpu_physical_memory_rw(mdisk->ptr, buffer, txsize, 1);
          } else {
            cpu_physical_memory_rw(mdisk->ptr, buffer, txsize, 0);
            ret = bdrv_write(mdisk->bs, mdisk->block, buffer,
                txsize/DISK_SECTOR_SIZE);
          }
          mdisk->len -= txsize;
        }
        ASSERT(mdisk->len == 0);
        mdisk->state = NONE;
      } else {
        assert(0);
      }
      break;
    case ADDR:
      mdisk->ptr = val;
      mdisk->state = NONE;
      break;
    case BLOCK:
      mdisk->block = val;
      mdisk->state = NONE;
      break;
    case LEN:
      mdisk->len = val;
      ASSERT((mdisk->len % DISK_SECTOR_SIZE) == 0);
      mdisk->state = NONE;
      break;
		case OP:
			mdisk->op = val;
			ASSERT(mdisk->op == READ || mdisk->op == WRITE);
			mdisk->state = NONE;
			break;
    default:
      assert(0);
      break;
  }
	//printf("%s(): returning.\n", __func__);
}
