#include "devices/disk.h"
#include <ctype.h>
#include <debug.h>
#include <prints.h>
#include <stdbool.h>
#include <stdio.h>
#include "devices/timer.h"
#include "devices/pci.h"
#include "devices/ata.h"
#include "devices/mdisk.h"
#include "devices/usb/usbdevice.h"
#include "devices/usb/usbmsd.h"
#include "mem/vaddr.h"
#include "sys/io.h"
#include "sys/interrupt.h"
#include "sys/rr_log.h"
#include "sys/vcpu.h"
#include "threads/synch.h"
#include "mem/malloc.h"

#ifndef BOOT_DISK
#define BOOT_DISK Virtual_Floppy
#endif
static char const *get_boot_disk_name(void);

enum disk_type { DISK_ATA, DISK_USB, DISK_MDISK };

static char const *record_disk_name = NULL;
static char const *replay_disk_name = NULL;
static char const *boot_disk_name = NULL;

struct disk {
  enum disk_type type;
  union {
    struct ata_disk *ata_disk;
    struct usbmsd   *usbmsd;
    struct mdisk    *mdisk;
  } u;
  disk_sector_t size;
};

/* Initialize the disk subsystem and detect disks. */
void
disk_init (void)
{
  ata_disk_init();
	boot_disk_name = get_boot_disk_name();
#ifdef __MONITOR__
  mdisk_init();
	record_disk_name = rr_log_get_record_disk_name();
	replay_disk_name = rr_log_get_replay_disk_name();
#endif
}

struct disk *
identify_disk_by_name (char const *name)
{
  struct list *devlist;
  struct list_elem *e;
  struct mdisk *mdisk;
	int chan_no;

  devlist = usb_get_devlist();
  for (e = list_begin(devlist); e != list_end(devlist); e = list_next(e)) {
    struct usbdevice *dev;
    struct usbmsd *msd;
    dev = list_entry(e, struct usbdevice, devlist_elem);
    if ((msd = usbdev_is_msd(dev)) && !strcmp(usbmsd_name(msd), name)) {
      struct disk *disk;
      disk = malloc(sizeof(struct disk));
      ASSERT(disk);
      disk->u.usbmsd = msd;
      disk->type = DISK_USB;
      disk->size = 0;
      MSG ("%s(): USB disk found: %s (%s [%s])\n", __func__, dev->product_name,
          dev->manufacturer_name, dev->serialnumber_name);
      return disk;
    }
  }
#ifdef __MONITOR__
  if (mdisk = identify_mdisk_by_name(name)) {
    struct disk *disk;
    disk = malloc(sizeof(struct disk));
    ASSERT(disk);
    disk->u.mdisk = mdisk;
    disk->type = DISK_MDISK;
    disk->size = 0;
    MSG ("%s(): MDISK found: %s\n", __func__, mdisk_name(mdisk));
    return disk;
  }
#endif
	for (chan_no = 0; chan_no < 2; chan_no++) {
		int dev_no;
		for (dev_no = 0; dev_no < 2; dev_no++) {
			struct ata_disk *ata_disk;
			if (   (ata_disk = ata_disk_get(chan_no, dev_no))
					&& !strcmp(ata_disk_name(ata_disk), name)) {
				return identify_ata_disk(chan_no, dev_no);
			}
		}
	}
	return NULL;
}

struct disk *
identify_ata_disk (int chan_no, int dev_no)
{
  struct ata_disk *ata_disk;
  struct disk *disk;
  ata_disk = ata_disk_get (chan_no, dev_no);
  if (!ata_disk) {
    return NULL;
  }
  disk = malloc(sizeof(struct disk));
  ASSERT(disk);
  disk->u.ata_disk = ata_disk;
  disk->type = DISK_ATA;
  disk->size = 0;
  return disk;
}

struct disk *
identify_boot_disk (void)
{
  struct list *devlist;
  struct list_elem *e;

  devlist = usb_get_devlist();
  for (e = list_begin(devlist); e != list_end(devlist); e = list_next(e)) {
    struct usbdevice *dev;
    struct usbmsd *msd;
    dev = list_entry(e, struct usbdevice, devlist_elem);
    if ((   msd = usbdev_is_msd(dev))
				 /*&& (!record_disk_name || strcmp(usbmsd_name(msd), record_disk_name))*/
				 /*&& (!replay_disk_name || strcmp(usbmsd_name(msd), replay_disk_name))*/
				 && usbmsd_is_bootdisk(msd)
				 && (!boot_disk_name || !strcmp(usbmsd_name(msd), boot_disk_name))) {
      struct disk *disk;
      disk = malloc(sizeof(struct disk));
      ASSERT(disk);
      disk->u.usbmsd = msd;
      disk->type = DISK_USB;
      disk->size = 0;
      MSG ("USB boot disk found: %s (%s [%s])\n", dev->product_name,
          dev->manufacturer_name, dev->serialnumber_name);
      return disk;
    }
  }
  MSG ("%s(): No USB boot disk found. Using hda..\n", __func__);

  return identify_ata_disk(0, 0);
}

disk_sector_t
disk_size(struct disk *disk)
{
  bool cap;
  uint32_t numblocks, blocksize;

  ASSERT(disk);
  if (disk->size) {
    return disk->size;
  }
  switch (disk->type) {
    case DISK_ATA:
      disk->size = ata_disk_size(disk->u.ata_disk);
      return disk->size;
      break;
    case DISK_USB:
      cap = usbmsd_read_capacity(disk->u.usbmsd, 0, &numblocks, &blocksize);
      ASSERT(cap);
      ASSERT(blocksize == DISK_SECTOR_SIZE);
      return (disk->size = numblocks);
      break;
    case DISK_MDISK:
      return 0xffffffff;
    default:
      ABORT();
  }
  NOT_REACHED();
}

void
disk_read (struct disk *disk, disk_sector_t sec_no, size_t count, void *buf)
{
  bool ret;
  char *ptr = buf;
  size_t i;
  size_t transfer_size = 1;
  ASSERT(disk);
  switch (disk->type) {
    case DISK_ATA:
      for (i = 0; i < count; i++) {
        ata_disk_read(disk->u.ata_disk, sec_no + i, ptr);
        ptr += DISK_SECTOR_SIZE;
      }
      return;
    case DISK_USB:
      for (i = 0; i < count; i+=transfer_size) {
        /*
        printf("%s(): sec_no=%#x, size=%d\n", __func__,
            sec_no+i*transfer_size, count-i);
            */
        ret = usbmsd_read(disk->u.usbmsd, 0, ptr, sec_no + i,
            min(transfer_size, count - i));
        ASSERT(ret);
        ptr += DISK_SECTOR_SIZE*transfer_size;
      }
      return;
#ifdef __MONITOR__
    case DISK_MDISK:
      ASSERT(is_monitor_vaddr(buf));
      ret = mdisk_read(disk->u.mdisk, vtop_mon(buf), sec_no, count);
      ASSERT(ret);
      return;
#endif
    default:
      ABORT();
  }
  NOT_REACHED();
}

void
disk_write (struct disk *disk, disk_sector_t sec_no, size_t count,
    const void *buf)
{
  bool ret;
  char const *ptr = buf;
  size_t i;
  ASSERT(disk);
  switch (disk->type) {
    case DISK_ATA:
      for (i = 0; i < count; i++) {
        ASSERT(disk->u.ata_disk);
        ata_disk_write(disk->u.ata_disk, sec_no + i, ptr);
        ptr += DISK_SECTOR_SIZE;
      }
      return;
    case DISK_USB:
      ret = usbmsd_write(disk->u.usbmsd, 0, buf, sec_no, count);
      ASSERT(ret);
      return;
#ifdef __MONITOR__
    case DISK_MDISK:
      ASSERT(is_monitor_vaddr(buf));
      ret = mdisk_write(disk->u.mdisk, vtop_mon(buf), sec_no, count);
      ASSERT(ret);
      return;
#endif
    default:
      ABORT();
  }
  NOT_REACHED();
}

void
disk_free (struct disk *disk)
{
  switch (disk->type) {
    case DISK_ATA:
      break;
    case DISK_USB:
      usbmsd_free(disk->u.usbmsd);
      break;
#ifdef __MONITOR__
    case DISK_MDISK:
      mdisk_free(disk->u.mdisk);
      break;
#endif
    default:
      ABORT();
  }
  free(disk);
}

void
disk_check (struct disk *disk)
{
  if (!disk) {
    return;
  }
  switch (disk->type) {
    case DISK_ATA:
      ASSERT(disk->u.ata_disk);
      break;
    case DISK_USB:
      ASSERT(disk->u.usbmsd);
      break;
    case DISK_MDISK:
      ASSERT(disk->u.mdisk);
      break;
    default:
      ABORT();
  }
}

static char const *
get_boot_disk_name(void)
{
#ifdef BOOT_DISK
	return xstr(BOOT_DISK);
#endif
	return NULL;
}
