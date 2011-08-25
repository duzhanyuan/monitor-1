#include "hw/bdrv.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include "devices/disk.h"
#include "mem/malloc.h"
#include "sys/init.h"
#include "sys/vcpu.h"

extern BlockDriver displace_bdrv;
static BlockDriver *first_drv = NULL;


static BlockDriver *
find_protocol(char const *filename)
{
  char *colon;
  if (colon = strchr(filename, ':')) {
    char const *str;
    char *num_end;

    str = colon + 1;
    strtoll(str, &num_end, 16);
    /*
    if (num_end == str + strlen(str)) {
      return &append_bdrv;
    }
    */
    if (num_end == strchr(str, ':')) {
      char *num2_end;
      strtoll(num_end + 1, &num2_end, 16);
      if (num2_end == str + strlen(str)) {
        return &displace_bdrv;
      }
    }
    return NULL;
  }
  return NULL;
}

int
bdrv_open(BlockDriverState *bs, char const *filename, char const *mode)
{
  BlockDriver *drv;
  int ret;

  strlcpy(bs->filename, filename, sizeof bs->filename);
  drv = find_protocol(filename);
  if (!drv) {
    return -1;
  }
  bs->drv = drv;
  bs->opaque = malloc(drv->instance_size);
  if (!bs->opaque && drv->instance_size > 0) {
    return -1;
  }
  ret = drv->bdrv_open(bs, filename, mode);
  if (ret < 0) {
    free(bs->opaque);
    bs->opaque = NULL;
    bs->drv = NULL;
    return ret;
  }
  bs->total_sectors = drv->bdrv_getlength(bs) / DISK_SECTOR_SIZE;
  return 0;
}

void
bdrv_close(BlockDriverState *bs)
{
  ASSERT(bs);           ASSERT(bs->drv);
  ASSERT(bs->drv->bdrv_close);
  bs->drv->bdrv_close(bs);
}

int
bdrv_read(BlockDriverState *bs, disk_sector_t sector_num, uint8_t *buf,
    int nb_sectors)
{
  ASSERT(bs);           ASSERT(bs->drv);
  ASSERT(bs->drv->bdrv_read);
  /*
  printf("%s(): bs=%p, sector_num=%#llx, buf=%p, nb_sectors=%#x "
      "bs->drv=%p, bs->drv->bdrv_read=%p\n", __func__, bs,
      (int64_t)sector_num, buf, nb_sectors, bs->drv, bs->drv->bdrv_read);
      */
  return bs->drv->bdrv_read(bs, sector_num, buf, nb_sectors);
}

int
bdrv_write(BlockDriverState *bs, disk_sector_t sector_num, const uint8_t *buf,
    int nb_sectors)
{
  ASSERT(bs);           ASSERT(bs->drv);
  ASSERT(bs->drv->bdrv_write);
  return bs->drv->bdrv_write(bs, sector_num, buf, nb_sectors);
}

BlockDriverAIOCB *
bdrv_aio_read(BlockDriverState *bs, disk_sector_t sector_num, uint8_t *buf,
    int nb_sectors, BlockDriverCompletionFunc *cb, void *opaque)
{
  ASSERT(bs);           ASSERT(bs->drv);
  ASSERT(bs->drv->bdrv_aio_read);
  return bs->drv->bdrv_aio_read(bs, sector_num, buf, nb_sectors, cb,
      opaque);
}

BlockDriverAIOCB *
bdrv_aio_write(BlockDriverState *bs, disk_sector_t sector_num,
    const uint8_t *buf, int nb_sectors, BlockDriverCompletionFunc *cb,
    void *opaque)
{
  ASSERT(bs);           ASSERT(bs->drv);
  ASSERT(bs->drv->bdrv_aio_write);
  return bs->drv->bdrv_aio_write(bs, sector_num, buf, nb_sectors, cb,
      opaque);
}

void
bdrv_aio_cancel(BlockDriverAIOCB *acb)
{
  NOT_IMPLEMENTED();
}

void
bdrv_flush(BlockDriverState *bs)
{
  ASSERT(bs);           ASSERT(bs->drv);
  ASSERT(bs->drv->bdrv_flush);
  bs->drv->bdrv_flush(bs);
}

void
bdrv_get_geometry(BlockDriverState *bs, disk_sector_t *nb_sectors_ptr)
{
  ASSERT(nb_sectors_ptr);
  ASSERT(bs);
  ASSERT(bs->drv);
  ASSERT(bs->drv->bdrv_getlength);
  *nb_sectors_ptr = bs->drv->bdrv_getlength(bs);
}

void
bdrv_register(BlockDriver *bdrv)
{
  bdrv->next = first_drv;
  first_drv = bdrv;
}

void
bdrv_init(void)
{
  char drive_str[128];
  int ret;
  bdrv_register(&displace_bdrv);

  snprintf(drive_str, sizeof drive_str, "bootdisk:0x%x:0x%x", monitor_ofs - 1,
      (loader_pages + monitor_pages + swap_disk_pages) * 8 + 1);
  MSG ("%s() %d: hda drive_str=%s\n", __func__, __LINE__, drive_str);
  hda_bdrv = malloc(sizeof *hda_bdrv);
  ASSERT(hda_bdrv);
  ret = bdrv_open(hda_bdrv, drive_str, "rw");
  ASSERT(ret >= 0);
  snprintf(drive_str, sizeof drive_str, "hdb:0:0");
  hdb_bdrv = malloc(sizeof *hdb_bdrv);
  ASSERT(hdb_bdrv);
  ret = bdrv_open(hdb_bdrv, drive_str, "rw");
  if (ret >= 0) {
    MSG ("hdb found...\n");
  } else {
    free(hdb_bdrv);
    hdb_bdrv = NULL;
  }
}
