#ifndef THREADS_INIT_H
#define THREADS_INIT_H

#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Size of the monitor, loader, swap in 4 kB pages. */
extern size_t monitor_pages, loader_pages, swap_disk_pages;
/* Offset of monitor on disk in sectors. */
extern size_t monitor_ofs;

/* Page directory with kernel mappings only. */
extern uint32_t *base_page_dir;

void power_off (void) NO_RETURN;
void reboot (void);

#endif /* threads/init.h */
