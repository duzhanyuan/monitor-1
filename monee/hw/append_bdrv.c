#include <debug.h>
#include <stdlib.h>
#include <stdio.h>
#include "devices/disk.h"
#include "hw/bdrv.h"

typedef struct BDRVAppendState {
  struct disk *disk;
  disk_sector_t offset;   /* Sectors 0..offset-1 have been pre-pended to the
                             disk. */
} BDRVAppendState;

int
append_bdrv_open(BlockDriverState *bs, char const *filename, char const *mode)
{
  char const *offset_str;
  BDRVAppendState *bas = (BDRVAppendState *)bs->opaque;

  //log_printf("%s() %d: filename=%s\n", __func__, __LINE__, filename);
  if (strstart(filename, "bootdisk:", &offset_str)) {
    bas->disk = identify_boot_disk();
    /*
    log_printf("%s() %d: bas->disk=%p\n", __func__, __LINE__,
        bas->disk);
        */
  } else {
    ASSERT(filename[0] == 'h' && filename[1] == 'd' && filename[3] == ':');

    switch (filename[2]) {
      case 'a':
        bas->disk = identify_ata_disk(0, 0);
        break;
      case 'b':
        bas->disk = identify_ata_disk(0, 1);
        break;
      case 'c':
        bas->disk = identify_ata_disk(1, 0);
        break;
      case 'd':
        bas->disk = identify_ata_disk(1, 1);
        break;
      default:
        ABORT();
    }
    offset_str = &filename[4];
    /* log_printf("%s() %d: bas->disk=%p\n", __func__, __LINE__,
        bas->disk); */
  }
  ASSERT(bas->disk);
  bas->offset = strtoll(offset_str, NULL, 16);
  /* log_printf("bas=%#x, filename=%s, disk=%p, offset=%#x\n", bas, filename,
      bas->disk, bas->offset); */
  return 0;
}

int
append_bdrv_read(BlockDriverState *bs, disk_sector_t sector_num, uint8_t *buf,
    int nb_sectors)
{
  BDRVAppendState *bas;
  disk_sector_t size;
  uint8_t *ptr;
  disk_sector_t i;

  bas = (BDRVAppendState *)bs->opaque;
  ASSERT(bas);
  ASSERT(bas->disk);
  /*
  printf("%s() %d: bas->disk=%p, bas->offset=%#lx\n", __func__, __LINE__,
      bas->disk, bas->offset);
      */
  size = bas->offset;
  /*
  printf("%s(%p, %#llx, %p, %d) called. bas->offset=%#llx, size=%#llx, "
      "disk_size=%#llx\n", __func__, bs, (int64_t)sector_num, buf, nb_sectors,
      (int64_t)bas->offset, (int64_t)size, (int64_t)disk_size(bas->disk));
      */
  ptr = buf;
  for (i = 0; i < nb_sectors; i++) {
    disk_sector_t cur_sector = sector_num + i;
    /*
    printf("%s() %d: disk_size=%#llx, cur_sector=%#llx, size=%#llx\n", __func__,
        __LINE__, (int64_t)disk_size(bas->disk), (int64_t)cur_sector,
        (int64_t)size); */
    ASSERT(cur_sector + size <= disk_size(bas->disk));
    disk_read(bas->disk, cur_sector + size, 1, ptr);
    ASSERT(bas);
    ptr += DISK_SECTOR_SIZE;
  }
  return 0;
}

int
append_bdrv_write(BlockDriverState *bs, disk_sector_t sector_num,
    uint8_t const *buf, int nb_sectors)
{
  BDRVAppendState *bas;
  disk_sector_t size;
  uint8_t const *ptr;
  int i;

  /*
  log_printf("%s(%p, %#llx, %p, %d) called.\n", __func__, bs, sector_num, buf,
      nb_sectors);
      */
  bas = (BDRVAppendState *)bs->opaque;
  ASSERT(bas);
  size = bas->offset;
  ptr = buf;
  for (i = 0; i < nb_sectors; i++) {
    disk_sector_t cur_sector = sector_num + i;
    ASSERT(cur_sector + size <= disk_size(bas->disk));
    disk_write(bas->disk, cur_sector + size, 1, ptr);
    ptr += DISK_SECTOR_SIZE;
  }
  return 0;
}

disk_sector_t
append_bdrv_getlength(BlockDriverState *bs)
{
  BDRVAppendState *bas;
  disk_sector_t size;

  bas = (BDRVAppendState *)bs->opaque;
  ASSERT(bas);    ASSERT(bas->disk);
  size = bas->offset;
  return size;
}

int
append_bdrv_flush(BlockDriverState *bs)
{
}

void
append_bdrv_close(BlockDriverState *bs)
{
}

BlockDriverAIOCB *
append_bdrv_aio_read(BlockDriverState *bs, disk_sector_t sector_num,
    uint8_t *buf, int nb_sectors, BlockDriverCompletionFunc *cb, void *opaque)
{
  NOT_IMPLEMENTED();
}

BlockDriverAIOCB *
append_bdrv_aio_write(BlockDriverState *bs, disk_sector_t sector_num,
    uint8_t const *buf, int nb_sectors, BlockDriverCompletionFunc *cb,
    void *opaque)
{
  NOT_IMPLEMENTED();
}

void
append_bdrv_aio_cancel(BlockDriverAIOCB *acb)
{
  NOT_IMPLEMENTED();
}

BlockDriver append_bdrv = {
  "append",
  sizeof(BDRVAppendState),
  append_bdrv_open,
  append_bdrv_read,
  append_bdrv_write,
  append_bdrv_getlength,
  append_bdrv_flush,
  append_bdrv_close,
};
