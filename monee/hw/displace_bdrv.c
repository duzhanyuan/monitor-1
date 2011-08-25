#include <debug.h>
#include <stdlib.h>
#include <stdio.h>
#include "devices/disk.h"
#include "hw/bdrv.h"

typedef struct BDRVDisplaceState {
  struct disk *disk;
  disk_sector_t offset;   /* sector 0 is displaced to sector 'offset'.
                             sectors (offset+1)..(offset+length)[inclusive]
                             are occupied.*/
  disk_sector_t length;
} BDRVDisplaceState;

static int
displace_bdrv_open(BlockDriverState *bs, char const *filename, char const *mode)
{
  char const *offset_str;
  char *colon;
  BDRVDisplaceState *bas = (BDRVDisplaceState *)bs->opaque;

  if (strstart(filename, "bootdisk:", &offset_str)) {
    bas->disk = identify_boot_disk();
    /*
    printf("%s() %d: bas->disk=%p\n", __func__, __LINE__,
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
  if (!bas->disk) {
    return -1;
  }
  bas->offset = strtoll(offset_str, &colon, 16);
  ASSERT(*colon == ':');
  bas->length = strtoll(colon+1, NULL, 16);
  /* log_printf("bas=%#x, filename=%s, disk=%p, offset=%#x\n", bas, filename,
      bas->disk, bas->offset); */
  return 0;
}

static int
displace_bdrv_read(BlockDriverState *bs, disk_sector_t sector_num, uint8_t *buf,
    int nb_sectors)
{
  disk_sector_t cur_sector;
  BDRVDisplaceState *bas;
  uint8_t *ptr;

  bas = (BDRVDisplaceState *)bs->opaque;
  ASSERT(bas);
  ASSERT(bas->disk);
  /*
  printf("%s() %d: bas->disk=%p, bas->offset=%#lx\n", __func__, __LINE__,
      bas->disk, bas->offset);
  printf("%s(%p, %#llx, %p, %d) called. bas->offset=%#llx, "
      "disk_size=%#llx\n", __func__, bs, (int64_t)sector_num, buf, nb_sectors,
      (int64_t)bas->offset, (int64_t)disk_size(bas->disk));
      */
  ptr = buf;
  for (cur_sector = sector_num; cur_sector < sector_num + nb_sectors;
      cur_sector++) {
    if (cur_sector == 0) {
      disk_read(bas->disk, bas->offset, 1, ptr);
    } else {
      disk_read(bas->disk, cur_sector, 1, ptr);
    }
    ASSERT(bas);
    ptr += DISK_SECTOR_SIZE;
  }
  return 0;
}

static int
displace_bdrv_write(BlockDriverState *bs, disk_sector_t sector_num,
    uint8_t const *buf, int nb_sectors)
{
  disk_sector_t cur_sector;
  BDRVDisplaceState *bas;
  uint8_t const *ptr;

  bas = (BDRVDisplaceState *)bs->opaque;
  ASSERT(bas);
  ptr = buf;
  for (cur_sector = sector_num; cur_sector < sector_num + nb_sectors;
      cur_sector++) {
    if (cur_sector == 0) {
      /* printf("%s(): calling disk_write(%d, 1)\n", __func__,
          (uint32_t)bas->offset); */
      disk_write(bas->disk, bas->offset, 1, ptr);
    } else if (   cur_sector >= bas->offset + 1
               && cur_sector <= bas->offset + bas->length) {
      printf("Warning: Attempt to write to a monitor block 0x%x\n",
          cur_sector);
    } else {
      /* printf("%s(): calling disk_write(%d, 1)\n", __func__,
          (uint32_t)cur_sector); */
      disk_write(bas->disk, cur_sector, 1, ptr);
    }
    ptr += DISK_SECTOR_SIZE;
  }
  return 0;
}

static disk_sector_t
displace_bdrv_getlength(BlockDriverState *bs)
{
  BDRVDisplaceState *bas;
  disk_sector_t size;

  bas = (BDRVDisplaceState *)bs->opaque;
  return disk_size(bas->disk);
}

static int
displace_bdrv_flush(BlockDriverState *bs)
{
	return 0;
}

static void
displace_bdrv_close(BlockDriverState *bs)
{
}

static BlockDriverAIOCB *
displace_bdrv_aio_read(BlockDriverState *bs, disk_sector_t sector_num,
    uint8_t *buf, int nb_sectors, BlockDriverCompletionFunc *cb, void *opaque)
{
  NOT_IMPLEMENTED();
}

static BlockDriverAIOCB *
displace_bdrv_aio_write(BlockDriverState *bs, disk_sector_t sector_num,
    uint8_t const *buf, int nb_sectors, BlockDriverCompletionFunc *cb,
    void *opaque)
{
  NOT_IMPLEMENTED();
}

static void
displace_bdrv_aio_cancel(BlockDriverAIOCB *acb)
{
  NOT_IMPLEMENTED();
}

BlockDriver displace_bdrv = {
  "displace",
  sizeof(BDRVDisplaceState),
  displace_bdrv_open,
  displace_bdrv_read,
  displace_bdrv_write,
  displace_bdrv_getlength,
  displace_bdrv_flush,
  displace_bdrv_close,
};
