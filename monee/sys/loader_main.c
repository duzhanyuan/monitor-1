#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <console.h>
#include "devices/disk.h"
#include "devices/input.h"
#include "devices/kbd.h"
#include "devices/pci.h"
#include "devices/serial.h"
#include "devices/timer.h"
#include "devices/usb/usb.h"
#include "mem/pte.h"
#include "mem/malloc.h"
#include "mem/palloc.h"
#include "mem/vaddr.h"
#include "sys/exception.h"
#include "sys/gdt.h"
#include "sys/interrupt.h"
#include "sys/loader.h"
#include "sys/tss.h"
#include "sys/vcpu.h"
#include "sys/monitor.h"
#include "threads/thread.h"

int loader_main(void);

static size_t loader_ram_pages, loader_monitor_ofs, loader_loader_pages,
              loader_monitor_pages;

/* Clear BSS and obtain RAM size from loader. */
static void
loader_ram_init (void)
{
  /* The "BSS" is a segment that should be initialized to zeros.
   * It isn't actually stored on disk or zeroed by the kernel
   * loader, so we have to zero it ourselves.
   *
   * The start and end of the BSS segment is recorded by the
   * linker as _start_bss and _end_bss.  See kernel.lds.
   */
  extern char _start_bss, _end_bss;
  memset (&_start_bss, 0, &_end_bss - &_start_bss);
  /* Get ram, loader and monitor size from bootsector.  See bootsector.S. */
  loader_ram_pages = *(uint32_t *) BOOTSECTOR_RAM_PGS;
}


static void
loader_constants_init (void)
{
  loader_monitor_ofs = *(uint32_t *) BOOTSECTOR_MONITOR_OFS;
  loader_loader_pages = *(uint32_t *) BOOTSECTOR_LOADER_PGS;
  loader_monitor_pages = *(uint32_t *) BOOTSECTOR_MONITOR_PGS;

	/* Fill the correct 'ram_pgs' value so that it is read by the monitor. */
	*(uint32_t *)BOOTSECTOR_RAM_PGS = loader_ram_pages;
}

extern size_t ram_pages;

static void
paging_init_monitor(void)
{
  target_ulong page, pd_no;
  uint32_t *pd = (void *)LOADER_PAGEDIR_BASE;
  for (pd_no = 0, page = 0; pd_no < (1 << 10); pd_no++, page += LPGSIZE) {
    /* Identity map. */
    pd[page >> LPGBITS] = pde_create_large_mon(page, true);
  }
  for (page = LOADER_MONITOR_BASE; page < LOADER_MONITOR_END; page += LPGSIZE) {
    pd[((target_ulong)ptov_mon(page)) >> LPGBITS]
      = pde_create_large_mon(page, true);
  }
  /* reload pagedir. */
  asm volatile ("movl %0, %%cr3" : : "r" (LOADER_PAGEDIR_BASE));
}

int
loader_main(void)
{
  struct disk *disk;
  disk_sector_t start, end;
  char *ptr;

	loader_ram_init();
  loader_constants_init();
  thread_init();
  synch_init();
  console_init();

  MSG("Loader loaded with %'zu kB RAM...\n", loader_ram_pages*PGSIZE/1024);

  if (loader_ram_pages*PGSIZE < LOADER_MONITOR_BASE + MONITOR_SIZE) {
    MSG("Error: too little memory! Need at least %'zu kB.\n",
        (LOADER_MONITOR_BASE + MONITOR_SIZE)*(PGSIZE/1024));
  }

  /* Initialize memory system. */
  palloc_init((void *)(LOADER_MONITOR_BASE - 1));
  malloc_init();

  tss_init();
  gdt_init();

  paging_init_monitor();
  intr_init();
  timer_init();
  kbd_init();
  input_init();
  exception_init();

  thread_start();
  serial_init_queue();
  timer_calibrate();
  pci_init();

  vcpu_set_log(VCPU_LOG_USB);

  /* Initialize disks. */
  disk_init();
  usb_init();

	/* Identify the boot disk. This may be different from the one we actually
	 * booted from. */
  disk = identify_boot_disk();
  ASSERT(disk);

	/* Re-read the bootsector from the boot disk. */
	MSG("Reading bootsector from boot disk.\n");
	disk_read(disk, 0, 1, (void *)0x7c00);
	MSG("Re-initializing ram.\n");
	loader_constants_init();

  start = loader_monitor_ofs + loader_loader_pages * 8; 
  end = start + loader_monitor_pages * 8;
  ptr = (void *)LOADER_MONITOR_BASE;
  MSG("Reading bootdisk sectors: %#x->%#x\n", start, end);
  disk_read(disk, start, end - start, ptr);
  MSG("Shutting down usb.                                               \n");
  usb_shutdown();

  asm volatile ("cli; movl $0xffffffff, %%esp; jmp *%0" :
      : "a"(LOADER_MONITOR_VIRT_BASE));

  NOT_REACHED();
}
