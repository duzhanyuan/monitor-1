#ifndef DEVICES_DISK_H
#define DEVICES_DISK_H

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <types.h>

/* Size of a disk sector in bytes. */
#define DISK_SECTOR_SIZE 512

/* Format specifier for printf(), e.g.:
   printf ("sector=%"PRDSNu"\n", sector); */
#define PRDSNu PRIu32

void disk_init (void);

//struct disk *identify_record_disk (void);
struct disk *identify_disk_by_name (char const *name);
struct disk *identify_boot_disk (void);
struct disk *identify_ata_disk (int chan_no, int dev_no);
disk_sector_t disk_size (struct disk *);
void disk_read (struct disk *, disk_sector_t, size_t, void *);
void disk_write (struct disk *, disk_sector_t, size_t, const void *);
void disk_free (struct disk *);
void disk_check (struct disk *);

#endif /* devices/disk.h */
