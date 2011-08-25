#ifndef __TYPES_H
#define __TYPES_H
#include <stdint.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

typedef uint32_t target_ulong;
typedef uint32_t target_phys_addr_t;

/* Index of a disk sector within a disk.
   Good enough for disks up to 2 TB. */
typedef uint32_t disk_sector_t;


#endif
