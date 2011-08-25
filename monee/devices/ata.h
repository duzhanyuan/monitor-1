#ifndef DEVICES_ATA_H
#define DEVICES_ATA_H

#include "disk.h"

struct ata_disk;

void ata_disk_init (void);
struct ata_disk *ata_disk_get(int chan_no, int dev_no);
disk_sector_t ata_disk_size (struct ata_disk *disk);
void ata_disk_read (struct ata_disk *disk, disk_sector_t sector_no, void *buf);
void ata_disk_write (struct ata_disk *disk, disk_sector_t sector_no,
    const void *buf);
char const *ata_disk_name(struct ata_disk const *disk);

#endif
