#ifndef SYS_BDRV_H
#define SYS_BDRV_H

#include <stdint.h>
#include "devices/disk.h"

typedef struct BlockDriver BlockDriver;
typedef struct BlockDriverState BlockDriverState;
typedef void BlockDriverCompletionFunc(void *opaque, int ret);
typedef struct BlockDriverAIOCB BlockDriverAIOCB;
struct disk;

struct BlockDriver {
  char const *format_name;
  int instance_size;
  int (*bdrv_open)(BlockDriverState *bs, char const *filename,
      char const *mode);
  int (*bdrv_read)(BlockDriverState *bs, disk_sector_t sector_num, uint8_t *buf,
      int nb_sectors);
  int (*bdrv_write)(BlockDriverState *bs, disk_sector_t sector_num,
      uint8_t const *buf, int nb_sectors);
  disk_sector_t (*bdrv_getlength)(BlockDriverState *bs);
  int (*bdrv_flush)(BlockDriverState *bs);
  void (*bdrv_close)(BlockDriverState *bs);
  /* aio */
  BlockDriverAIOCB *(*bdrv_aio_read)(BlockDriverState *bs,
      disk_sector_t sector_num, uint8_t *buf, int nb_sectors,
      BlockDriverCompletionFunc *cb, void *opaque);
  BlockDriverAIOCB *(*bdrv_aio_write)(BlockDriverState *bs,
      disk_sector_t sector_num, uint8_t const *buf, int nb_sectors,
      BlockDriverCompletionFunc *cb, void *opaque);
  void (*bdrv_aio_cancel)(BlockDriverAIOCB *acb);

  struct BlockDriver *next;
};

struct BlockDriverState {
  disk_sector_t total_sectors;    /* if we are reading a disk image, give its
                                     size in sectors. */
  int read_only;            /* if true, the media is read only */
  char filename[64];
  BlockDriver *drv;
  void *opaque;

  /* async read/write emulation. */
  void *sync_aiocb;
};

struct BlockDriverAIOCB {
  BlockDriverState *bs;
  BlockDriverCompletionFunc *cb;
  void *opaque;
  BlockDriverAIOCB *next;
};

void bdrv_init(void);
/*
int bdrv_create(BlockDriver *drv,
    const char *filename, disk_sector_t size_in_sectors,
    const char *backing_file, int flags);
BlockDriverState *bdrv_new(const char *device_name);
void bdrv_delete(BlockDriverState *bs);
int bdrv_file_open(BlockDriverState **pbs, const char *filename, int flags);
int bdrv_open2(BlockDriverState *bs, const char *filename, int flags,
    BlockDriver *drv);
*/
int bdrv_open(BlockDriverState *bs, char const *filename, char const *mode);
void bdrv_close(BlockDriverState *bs);
int bdrv_read(BlockDriverState *bs, disk_sector_t sector_num,
    uint8_t *buf, int nb_sectors);
int bdrv_write(BlockDriverState *bs, disk_sector_t sector_num,
    const uint8_t *buf, int nb_sectors);
void bdrv_get_geometry(BlockDriverState *bs, disk_sector_t *nb_sectors_ptr);
void bdrv_flush(BlockDriverState *bs);
void bdrv_register(BlockDriver *bdrv);
/*
int bdrv_pread(BlockDriverState *bs, disk_sector_t offset,
    void *buf, int count);
int bdrv_pwrite(BlockDriverState *bs, disk_sector_t offset,
    const void *buf, int count);
int bdrv_truncate(BlockDriverState *bs, disk_sector_t offset);
disk_sector_t bdrv_getlength(BlockDriverState *bs);
int bdrv_commit(BlockDriverState *bs);
void bdrv_set_boot_sector(BlockDriverState *bs, const uint8_t *data, int size);
*/
/* async block I/O */
BlockDriverAIOCB *bdrv_aio_read(BlockDriverState *bs, disk_sector_t sector_num,
    uint8_t *buf, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque);
BlockDriverAIOCB *bdrv_aio_write(BlockDriverState *bs, disk_sector_t sector_num,
    const uint8_t *buf, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque);
void bdrv_aio_cancel(BlockDriverAIOCB *acb);

#endif
