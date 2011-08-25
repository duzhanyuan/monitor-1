#include "mdisk.h"
#include <stdlib.h>
#include <stdio.h>
#include "devices/disk.h"
#include "mem/malloc.h"
#include "mem/vaddr.h"
#include "sys/io.h"
#include "sys/vcpu.h"

#define MAX_MDISKS 2

struct mdisk {
  char filename[128];
  int  iobase;
};

struct mdisk mdisk[MAX_MDISKS];
static int num_mdisks;

#define IOBASE 0x2345
#define SET_ADDR 0x123456
#define SET_LEN 0x234567
#define START_TRANSFER 0x345678
#define SET_BLOCK 0x456789
#define SET_OP 0x56789a

#define READ 0
#define WRITE 1

void
mdisk_init(void)
{
  int iobase;
  int i;
  iobase = IOBASE;
  num_mdisks = 0;
  
  for (i = 0; i < MAX_MDISKS; i++) {
    uint32_t chr;
    chr = inl(iobase);
    if (chr != 0xffffffff) {
      unsigned pos = 0;
      pos += snprintf(mdisk[num_mdisks].filename,
          sizeof mdisk[num_mdisks].filename, "QEMU:");
      mdisk[num_mdisks].filename[pos++] = chr;
      while (   (chr = inl(iobase)) != '\0'
             && pos < sizeof mdisk[num_mdisks].filename - 2) {
        mdisk[num_mdisks].filename[pos++] = chr;
      }
      mdisk[num_mdisks].filename[pos] = '\0';
      mdisk[num_mdisks].iobase = iobase;
      MSG("Found MDISK %d at 0x%x: %s\n", num_mdisks, iobase,
          mdisk[num_mdisks].filename);
      num_mdisks++;
    }
    iobase+=4;
  }
  MSG("Number of MDISKS: %d\n", num_mdisks);
}

static bool
mdisk_transfer(struct mdisk *mdisk, target_phys_addr_t paddr, uint32_t block,
    uint16_t count, int op)
{
  outl(mdisk->iobase, SET_OP);
  outl(mdisk->iobase, op);
  outl(mdisk->iobase, SET_ADDR);
  outl(mdisk->iobase, paddr);
  outl(mdisk->iobase, SET_BLOCK);
  outl(mdisk->iobase, block);
  outl(mdisk->iobase, SET_LEN);
  outl(mdisk->iobase, count*DISK_SECTOR_SIZE);
  outl(mdisk->iobase, START_TRANSFER);
  return true;
}

bool
mdisk_read(struct mdisk *mdisk, target_phys_addr_t paddr, uint32_t block,
    uint16_t count)
{
	return mdisk_transfer(mdisk, paddr, block, count, READ);
}

bool
mdisk_write(struct mdisk *mdisk, target_phys_addr_t paddr, uint32_t block,
    uint16_t count)
{
	return mdisk_transfer(mdisk, paddr, block, count, WRITE);
}

struct mdisk *
identify_mdisk_by_name(char const *name)
{
  int i;
  MSG("%s(): Searching for '%s'\n", __func__, name);
  for (i = 0; i < num_mdisks; i++) {
    if (!strcmp(mdisk[i].filename, name)) {
      return &mdisk[i];
    }
  }
  return NULL;
}

char const *
mdisk_name(struct mdisk *mdisk)
{
  return mdisk->filename;
}

void
mdisk_free(struct mdisk *mdisk)
{
  free(mdisk);
}
