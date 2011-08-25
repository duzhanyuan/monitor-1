#ifndef SYS_BOOTSECTOR_H
#define SYS_BOOTSECTOR_H

/* Constants fixed by the PC BIOS. */
#define BOOTSECTOR_BASE 0x7c00      /* Physical address of loader's base. */
#define BOOTSECTOR_END  0x7e00      /* Physical address of end of loader. */

/* Important loader physical addresses. */
 /* 0xaa55 BIOS signature. */
#define BOOTSECTOR_SIG     (BOOTSECTOR_END  - BOOTSECTOR_SIG_LEN)
/* Partition table. */
#define BOOTSECTOR_PARTITION_TABLE \
  (BOOTSECTOR_SIG  - BOOTSECTOR_PARTITION_TABLE_LEN)
/* Mandatory zero word. */
#define BOOTSECTOR_MANDATORY_ZERO \
  (BOOTSECTOR_PARTITION_TABLE - BOOTSECTOR_MANDATORY_ZERO_LEN)
/* Optional disk signature. */
#define BOOTSECTOR_DISK_SIG \
  (BOOTSECTOR_MANDATORY_ZERO - BOOTSECTOR_DISK_SIG_LEN)
/* Command-line args. */
#define BOOTSECTOR_ARGS    (BOOTSECTOR_DISK_SIG  - BOOTSECTOR_ARGS_LEN)
/* Number of args. */
#define BOOTSECTOR_ARG_CNT (BOOTSECTOR_ARGS - BOOTSECTOR_ARG_CNT_LEN)
/* # ram pages. */
#define BOOTSECTOR_RAM_PGS (BOOTSECTOR_ARG_CNT-BOOTSECTOR_RAM_PGS_LEN)
/* monitor offset on disk. */
#define BOOTSECTOR_MONITOR_OFS (BOOTSECTOR_RAM_PGS-BOOTSECTOR_MONITOR_OFS_LEN)
/* # loader pages. */
#define BOOTSECTOR_LOADER_PGS (BOOTSECTOR_MONITOR_OFS-BOOTSECTOR_LOADER_PGS_LEN)
/* # monitor pages. */
#define BOOTSECTOR_MONITOR_PGS    \
                  (BOOTSECTOR_LOADER_PGS-BOOTSECTOR_MONITOR_PGS_LEN)

/* Sizes of bootsector data structures. */
#define BOOTSECTOR_SIG_LEN 2
#define BOOTSECTOR_PARTITION_TABLE_LEN 64
#define BOOTSECTOR_MANDATORY_ZERO_LEN 2
#define BOOTSECTOR_DISK_SIG_LEN 4
#define BOOTSECTOR_ARGS_LEN 0
#define BOOTSECTOR_ARG_CNT_LEN 4
#define BOOTSECTOR_RAM_PGS_LEN 4
#define BOOTSECTOR_MONITOR_OFS_LEN 4
#define BOOTSECTOR_LOADER_PGS_LEN 4
#define BOOTSECTOR_MONITOR_PGS_LEN 4

/* GDT selectors defined by loader. */
//#define SEL_CNT         16                  /* Number of monitor segments. */

#define SEL_NULL        (SEL_BASE + 0x00)      /* Null selector. */
#define SEL_KCSEG       (SEL_BASE + 0x08)      /* Kernel code selector. */
#define SEL_KDSEG       (SEL_BASE + 0x10)      /* Kernel data selector. */
#define SEL_UCSEG       (SEL_BASE + 0x1B)      /* User code selector. */
#define SEL_UDSEG       (SEL_BASE + 0x23)      /* User data selector. */
#define SEL_TSS         (SEL_BASE + 0x28)      /* Task-state segment. */
#define SEL_GCSEG       (SEL_BASE + 0x33)      /* Guest code selector. */
#define SEL_GDSEG       (SEL_BASE + 0x3B)      /* Guest data selector. */
#define SEL_TMPSEG      (SEL_BASE + 0x43)      /* Used in guest context to
                                                  perform temporary
                                                  modifications to esp
                                                  (e.g.,push/pop flags). */
#define SEL_SHADOW      (SEL_BASE + 0x48)      /* shadow segdescs for cached
                                                  segments. */
#define NUM_SEGSELS     6
#define SEL_SIZE        (0x48 + NUM_SEGSELS*8)

/* Loader constants */
#define BOOTSECTOR_LOADER_BASE 0x20000
#define LOADER_SIZE 0x60000     /* memory from 0x7e00->0x7ffff is RAM guaranteed
                                   for free use.
                                   (http://wiki.osdev.org/Memory_Map_(x86) */
#define BYTES_PER_SECTOR 512

#endif /* sys/bootsector.h */
