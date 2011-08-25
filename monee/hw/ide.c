#include "hw/ide.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mem/malloc.h"
#include "mem/vaddr.h"
#include "mem/swap.h"
#include "devices/disk.h"
#include "hw/bdrv.h"
#include "hw/hw.h"
#include "sys/mode.h"
#include "sys/vcpu.h"

#ifndef NDEBUG
//#define DEBUG_IDE
#endif

/* Bits of HD_STATUS */
#define ERR_STAT    0x01
#define INDEX_STAT    0x02
#define ECC_STAT    0x04  /* Corrected error */
#define DRQ_STAT    0x08
#define SEEK_STAT   0x10
#define SRV_STAT    0x10
#define WRERR_STAT    0x20
#define READY_STAT    0x40
#define BUSY_STAT   0x80

/* Bits for HD_ERROR */
#define MARK_ERR    0x01  /* Bad address mark */
#define TRK0_ERR    0x02  /* couldn't find track 0 */
#define ABRT_ERR    0x04  /* Command aborted */
#define MCR_ERR     0x08  /* media change request */
#define ID_ERR      0x10  /* ID field not found */
#define MC_ERR      0x20  /* media changed */
#define ECC_ERR     0x40  /* Uncorrectable ECC error */
#define BBD_ERR     0x80  /* pre-EIDE meaning:  block marked bad */
#define ICRC_ERR    0x80  /* new meaning:  CRC error during transfer */

/* Bits of HD_NSECTOR */
#define CD      0x01
#define IO      0x02
#define REL     0x04
#define TAG_MASK    0xf8

#define IDE_CMD_RESET           0x04
#define IDE_CMD_DISABLE_IRQ     0x02

/* ATA/ATAPI Commands pre T13 Spec */
#define WIN_NOP       0x00
/*
 *  *  0x01->0x02 Reserved
 *   */
#define CFA_REQ_EXT_ERROR_CODE    0x03 /* CFA Request Extended Error Code */
/*
 *  *  0x04->0x07 Reserved
 *   */
#define WIN_SRST      0x08 /* ATAPI soft reset command */
#define WIN_DEVICE_RESET    0x08
/*
 *  *  0x09->0x0F Reserved
 *   */
#define WIN_RECAL     0x10
#define WIN_RESTORE     WIN_RECAL
/*
 *  *  0x10->0x1F Reserved
 *   */
#define WIN_READ      0x20 /* 28-Bit */
#define WIN_READ_ONCE     0x21 /* 28-Bit without retries */
#define WIN_READ_LONG     0x22 /* 28-Bit */
#define WIN_READ_LONG_ONCE    0x23 /* 28-Bit without retries */
#define WIN_READ_EXT      0x24 /* 48-Bit */
#define WIN_READDMA_EXT     0x25 /* 48-Bit */
#define WIN_READDMA_QUEUED_EXT    0x26 /* 48-Bit */
#define WIN_READ_NATIVE_MAX_EXT   0x27 /* 48-Bit */
/*
 *  *  0x28
 *   */
#define WIN_MULTREAD_EXT    0x29 /* 48-Bit */
/*
 *  *  0x2A->0x2F Reserved
 *   */
#define WIN_WRITE     0x30 /* 28-Bit */
#define WIN_WRITE_ONCE      0x31 /* 28-Bit without retries */
#define WIN_WRITE_LONG      0x32 /* 28-Bit */
#define WIN_WRITE_LONG_ONCE   0x33 /* 28-Bit without retries */
#define WIN_WRITE_EXT     0x34 /* 48-Bit */
#define WIN_WRITEDMA_EXT    0x35 /* 48-Bit */
#define WIN_WRITEDMA_QUEUED_EXT   0x36 /* 48-Bit */
#define WIN_SET_MAX_EXT     0x37 /* 48-Bit */
#define CFA_WRITE_SECT_WO_ERASE   0x38 /* CFA Write Sectors without erase */
#define WIN_MULTWRITE_EXT   0x39 /* 48-Bit */
/*
 *  *  0x3A->0x3B Reserved
 *   */
#define WIN_WRITE_VERIFY    0x3C /* 28-Bit */
/*
 *  *  0x3D->0x3F Reserved
 *   */
#define WIN_VERIFY      0x40 /* 28-Bit - Read Verify Sectors */
#define WIN_VERIFY_ONCE     0x41 /* 28-Bit - without retries */
#define WIN_VERIFY_EXT      0x42 /* 48-Bit */
/*
 *  *  0x43->0x4F Reserved
 *   */
#define WIN_FORMAT      0x50
/*
 *  *  0x51->0x5F Reserved
 *   */
#define WIN_INIT      0x60
/*
 *  *  0x61->0x5F Reserved
 *   */
#define WIN_SEEK      0x70 /* 0x70-0x7F Reserved */
#define CFA_TRANSLATE_SECTOR    0x87 /* CFA Translate Sector */
#define WIN_DIAGNOSE      0x90
#define WIN_SPECIFY     0x91 /* set drive geometry translation */
#define WIN_DOWNLOAD_MICROCODE    0x92
#define WIN_STANDBYNOW2     0x94
#define WIN_STANDBY2      0x96
#define WIN_SETIDLE2      0x97
#define WIN_CHECKPOWERMODE2   0x98
#define WIN_SLEEPNOW2     0x99
/*
 *  *  0x9A VENDOR
 *   */
#define WIN_PACKETCMD     0xA0 /* Send a packet command. */
#define WIN_PIDENTIFY     0xA1 /* identify ATAPI device */
#define WIN_QUEUED_SERVICE    0xA2
#define WIN_SMART     0xB0 /* self-monitoring and reporting */
#define CFA_ERASE_SECTORS         0xC0
#define WIN_MULTREAD      0xC4 /* read sectors using multiple mode*/
#define WIN_MULTWRITE     0xC5 /* write sectors using multiple mode */
#define WIN_SETMULT     0xC6 /* enable/disable multiple mode */
#define WIN_READDMA_QUEUED    0xC7 /* read sectors using Queued DMA transfers */
#define WIN_READDMA     0xC8 /* read sectors using DMA transfers */
#define WIN_READDMA_ONCE    0xC9 /* 28-Bit - without retries */
#define WIN_WRITEDMA      0xCA /* write sectors using DMA transfers */
#define WIN_WRITEDMA_ONCE   0xCB /* 28-Bit - without retries */
#define WIN_WRITEDMA_QUEUED   0xCC /* write sectors using Queued DMA transfers */
#define CFA_WRITE_MULTI_WO_ERASE  0xCD /* CFA Write multiple without erase */
#define WIN_GETMEDIASTATUS    0xDA  
#define WIN_ACKMEDIACHANGE    0xDB /* ATA-1, ATA-2 vendor */
#define WIN_POSTBOOT      0xDC
#define WIN_PREBOOT     0xDD
#define WIN_DOORLOCK      0xDE /* lock door on removable drives */
#define WIN_DOORUNLOCK      0xDF /* unlock door on removable drives */
#define WIN_STANDBYNOW1     0xE0
#define WIN_IDLEIMMEDIATE   0xE1 /* force drive to become "ready" */
#define WIN_STANDBY               0xE2 /* Set device in Standby Mode */
#define WIN_SETIDLE1      0xE3
#define WIN_READ_BUFFER     0xE4 /* force read only 1 sector */
#define WIN_CHECKPOWERMODE1   0xE5
#define WIN_SLEEPNOW1     0xE6
#define WIN_FLUSH_CACHE     0xE7
#define WIN_WRITE_BUFFER    0xE8 /* force write only 1 sector */
#define WIN_WRITE_SAME      0xE9 /* read ata-2 to use */
  /* SET_FEATURES 0x22 or 0xDD */
#define WIN_FLUSH_CACHE_EXT   0xEA /* 48-Bit */
#define WIN_IDENTIFY      0xEC /* ask drive to identify itself  */
#define WIN_MEDIAEJECT      0xED
#define WIN_IDENTIFY_DMA    0xEE /* same as WIN_IDENTIFY, but DMA */
#define WIN_SETFEATURES     0xEF /* set special drive features */
#define EXABYTE_ENABLE_NEST   0xF0
#define WIN_SECURITY_SET_PASS   0xF1
#define WIN_SECURITY_UNLOCK   0xF2
#define WIN_SECURITY_ERASE_PREPARE  0xF3
#define WIN_SECURITY_ERASE_UNIT   0xF4
#define WIN_SECURITY_FREEZE_LOCK  0xF5
#define WIN_SECURITY_DISABLE    0xF6
#define WIN_READ_NATIVE_MAX   0xF8 /* return the native maximum address */
#define WIN_SET_MAX     0xF9
#define DISABLE_SEAGATE     0xFB

/* set to 1 set disable mult support */
#define MAX_MULT_SECTORS 16

struct IDEState;
struct PCIIDEState;
typedef void EndTransferFunc(struct IDEState *);

typedef struct IDEState
{
  /* ide config */
  int cylinders, heads, sectors;
  int64_t nb_sectors;
  int mult_sectors;
  int identify_set;
  uint16_t identify_data[256];
  SetIRQFunc *set_irq;
  void *irq_opaque;
  int irq;
  //PCIDevice *pci_dev;
  struct BMDMAState *bmdma;
  int drive_serial;
  /* ide regs */
  uint8_t feature;
  uint8_t error;
  uint32_t nsector;
  uint8_t sector;
  uint8_t lcyl;
  uint8_t hcyl;
  /* other part of tf for lba48 support */
  uint8_t hob_feature;
  uint8_t hob_nsector;
  uint8_t hob_sector;
  uint8_t hob_lcyl;
  uint8_t hob_hcyl;

  uint8_t select;
  uint8_t status;

  /* 0x3f6 command, only meaningful for drive 0 */
  uint8_t cmd;
  /* set for lba48 access */
  uint8_t lba48;
  /* depends on bit 4 in select, only meaningful for drive 0 */
  struct IDEState *cur_drive;
  BlockDriverState *bs;
  /* ATAPI specific */
  uint8_t sense_key;
  uint8_t asc;
  int packet_transfer_size;
  int elementary_transfer_size;
  int io_buffer_index;
  int lba;
  int cd_sector_size;
  int atapi_dma;    /* true if dma is requested for the packet cmd */
  /* ATA DMA state */
  int io_buffer_size;
  /* PIO transfer handling */
  int req_nb_sectors;   /* number of sectors per interrupt */
  EndTransferFunc *end_transfer_func;
  uint8_t *data_ptr;
  uint8_t *data_end;
  uint8_t io_buffer[MAX_MULT_SECTORS*512 + 4];
} IDEState;

#define BM_STATUS_DMAING 0x01
#define BM_STATUS_ERROR  0x02
#define BM_STATUS_INT    0x04

#define BM_CMD_START     0x01
#define BM_CMD_READ      0x08

#define IDE_TYPE_PIIX3   0
#define IDE_TYPE_CMD646  1

/* CMD646 specific */
#define MRDMODE   0x71
#define   MRDMODE_INTR_CH0  0x04
#define   MRDMODE_INTR_CH1  0x08
#define   MRDMODE_BLK_CH0 0x10
#define   MRDMODE_BLK_CH1 0x20
#define UDIDETCR0 0x73
#define UDIDETCR1 0x7B

typedef struct BMDMAState {
  uint8_t cmd;
  uint8_t status;
  uint32_t addr;

  struct PCIIDEState *pci_dev;
  /* current transfer state */
  uint32_t cur_addr;
  uint32_t cur_prd_last;
  uint32_t cur_prd_addr;
  uint32_t cur_prd_len;
  IDEState *ide_if;
  BlockDriverCompletionFunc *dma_cb;
  BlockDriverAIOCB *aiocb;
} BMDMAState;

typedef struct PCIIDEState {
  //PCIDevice dev;
  IDEState ide_if[4];
  BMDMAState bmdma[2];
  int type; /* see IDE_TYPE_xxx */
} PCIIDEState;


/* Helper functions. */
static void ide_clear_hob(IDEState *ide_if);
static void ide_identify(IDEState *s);
static void ide_transfer_start(IDEState *s, uint8_t *buf, int size,
    EndTransferFunc *end_transfer_func);
static void ide_transfer_stop(IDEState *s);
static void ide_abort_command(IDEState *s);
static void ide_set_irq(IDEState *s);
static void ide_cmd_lba48_transform(IDEState *s, int lba48);
static void ide_sector_read(IDEState *s);
static void ide_sector_write(IDEState *s);
static void ide_sector_write_dma(IDEState *s);
static void ide_dma_start(IDEState *s, BlockDriverCompletionFunc *dma_cb);
static void ide_set_sector(IDEState *s, int64_t sector_num);
static void ide_set_signature(IDEState *s);
static int64_t ide_get_sector(IDEState *s);
static void ide_set_sector(IDEState *s, int64_t sector_num);
static void ide_write_dma_cb(void *opaque, int ret);
static uint32_t ide_ioport_read(void *opaque, uint32_t port);
static void ide_ioport_write(void *opaque, uint16_t port, uint32_t data);
static uint32_t ide_status_read(void *opaque, uint32_t addr);
static void ide_cmd_write(void *opaque, uint32_t addr, uint32_t val);
static uint32_t ide_data_readw(void *opaque, uint32_t addr);
static void ide_data_writew(void *opaque, uint32_t addr, uint32_t val);
static uint32_t ide_data_readl(void *opaque, uint32_t addr);
static void ide_data_writel(void *opaque, uint32_t addr, uint32_t val);
static int dma_buf_rw(BMDMAState *bm, int is_write);
static void padstr(char *str, char const *src, int len);
static void put_le16(uint16_t *p, unsigned int v);
static uint32_t le16_to_cpu(uint32_t x);
static uint32_t cpu_to_le16(uint32_t x);
static uint32_t le32_to_cpu(uint32_t x);
static uint32_t cpu_to_le32(uint32_t x);
static void ide_dummy_transfer_stop(IDEState *s);
static void ide_reset(IDEState *s);
static void set_irq(void *opaque, int irq_num, int level);

IDEState *guest_if = NULL;
//BlockDriverState *hd0, *hd1;

static void
ide_init2(IDEState *guest_if, int irq, BlockDriverState *hda_bdrv,
    BlockDriverState *hdb_bdrv)
{
  static int drive_serial = 1;
  int i;

  for (i = 0; i < 2; i++) {
    IDEState *s;
    s = &guest_if[i];
    if (i == 0) {
      s->bs = hda_bdrv;
    } else {
      s->bs = hdb_bdrv;
    }
    if (s->bs) {
      disk_sector_t nb_sectors, cylinders;
      bdrv_get_geometry(s->bs, &nb_sectors);
      //nb_sectors = 16777216;    //XXX
      s->nb_sectors = nb_sectors;
      cylinders = nb_sectors / (16*63);
      if (cylinders > 16383) {
        cylinders = 16383;
      } else if (cylinders < 2) {
        cylinders = 2;
      }
      s->cylinders = cylinders;
      s->heads = 16;
      s->sectors = 63;
    }
    s->drive_serial = drive_serial++;
    s->set_irq = set_irq;     //XXX
    s->irq_opaque = NULL;     //XXX
    s->irq = irq;             //XXX
    //s->sector_write_timer = NULL; //XXX
    ide_reset(s);
  }
}

static void
ide_init_ioport(IDEState *ide_state, int iobase, int iobase2)
{   
  if (vcpu.replay_log) {
    return;
  }
  register_ioport_write(iobase, 8, 1, ide_ioport_write, ide_state, true);
  register_ioport_read(iobase, 8, 1, ide_ioport_read, ide_state, true);
  if (iobase2) {
    register_ioport_read(iobase2, 1, 1, ide_status_read, ide_state, true);
    register_ioport_write(iobase2, 1, 1, ide_cmd_write, ide_state, true);
  }

  /* data ports */
  register_ioport_write(iobase, 2, 2, ide_data_writew, ide_state, true);
  register_ioport_read(iobase, 2, 2, ide_data_readw, ide_state, true);
  register_ioport_write(iobase, 4, 4, ide_data_writel, ide_state, true);
  register_ioport_read(iobase, 4, 4, ide_data_readl, ide_state, true);
}   

void
ide_init(void)
{
  guest_if = (IDEState *)malloc(4*sizeof(IDEState));
  ASSERT(guest_if);

  ide_init2(guest_if, 14, hda_bdrv, hdb_bdrv);
  ide_init_ioport(&guest_if[0], 0x1f0, 0x3f6);
  //ide_init2(&guest_if[2], 15, NULL, NULL);
  //ide_init_ioport(&guest_if[2], 0x170, 0x376);
}

#if 0
void
ide_out(uint16_t port, target_ulong data, size_t data_size)
{
  int i;
  //printf("%s(%#hx, %#lx, %d) called.\n", __func__, port, data, data_size);
  ASSERT(guest_if);
  switch(data_size) {
    case 1:
      ide_ioport_write(port, data);
      break;
    case 2:
      ide_data_writew(port, data);
      break;
    case 4:
      ide_data_writel(port, data);
      break;
    default:
      ASSERT(0);
      break;
  }
}

target_ulong
ide_in(uint16_t port, size_t data_size)
{
  target_ulong ret = 0;
  int i;
  ASSERT(guest_if);
  switch (data_size) {
    case 1:
      ret = ide_ioport_read(port);
      break;
    case 2:
      ret = ide_data_readw(port);
      break;
    case 4:
      ret = ide_data_readl(port);
       break;
    default:
       ASSERT(0);
       break;
  }
  //printf("%s(%#hx, %d) returning %#x.\n", __func__, port, data_size, ret);
  return ret;
}

void ide_outs(uint16_t port, const void *addr, size_t cnt, size_t data_size)
{
  printf("%s(%hx, %p, %lx, %lx) called.\n", __func__, port, addr,
      cnt,data_size);
}
#endif

static void
ide_ioport_write(void *opaque, uint16_t port, uint32_t val)
{
  IDEState *ide_if = opaque;
  IDEState *s;
  int unit, n;
  int lba48 = 0;

  //printf("%s(%#hx, %#x) called.\n", __func__, port, val);
  port &= 7;
  switch(port) {
    case 0:
      break;
    case 1:
      ide_clear_hob(ide_if);
      ide_if[0].hob_feature = ide_if[0].feature;
      ide_if[1].hob_feature = ide_if[1].feature;
      ide_if[0].feature = val;
      ide_if[1].feature = val;
      break;
    case 2:
      ide_clear_hob(ide_if);
      ide_if[0].hob_nsector = ide_if[0].nsector;
      ide_if[1].hob_nsector = ide_if[1].nsector;
      ide_if[0].nsector = val;
      ide_if[1].nsector = val;
      break;
    case 3:
      ide_clear_hob(ide_if);
      ide_if[0].hob_sector = ide_if[0].sector;
      ide_if[1].hob_sector = ide_if[1].sector;
      ide_if[0].sector = val;
      ide_if[1].sector = val;
      break;
    case 4:
      ide_clear_hob(ide_if);
      ide_if[0].hob_lcyl = ide_if[0].lcyl;
      ide_if[1].hob_lcyl = ide_if[1].lcyl;
      ide_if[0].lcyl = val;
      ide_if[1].lcyl = val;
      break;
    case  5:
      ide_clear_hob(ide_if);
      ide_if[0].hob_hcyl = ide_if[0].hcyl;
      ide_if[1].hob_hcyl = ide_if[1].hcyl;
      ide_if[0].hcyl = val;
      ide_if[1].hcyl = val;
      break;
    case 6:
      ide_if[0].select = (val & ~0x10) | 0xa0;
      ide_if[1].select = (val | 0x10) | 0xa0;
      /* select drive */
      unit = (val >> 4) & 1;
      s = ide_if + unit;
      ide_if->cur_drive = s;
      break;
    default:
    case 7:
      /* command */
      s = ide_if->cur_drive;
      /* ignore commands to non existent slave */
      if (s != ide_if && !s->bs) {
        break;
      }
      switch (val) {
        case WIN_IDENTIFY:
          if (s->bs) {
            ide_identify(s);
            s->status = READY_STAT | SEEK_STAT;
            ide_transfer_start(s, s->io_buffer, 512, ide_transfer_stop);
          } else {
            ide_abort_command(s);
          }
          ide_set_irq(s);
          break;
        case WIN_SPECIFY:
        case WIN_RECAL:
          s->error = 0;
          s->status = READY_STAT | SEEK_STAT;
          ide_set_irq(s);
          break;
        case WIN_SETMULT:
          if (s->nsector > MAX_MULT_SECTORS || s->nsector == 0 ||
              (s->nsector & (s->nsector - 1)) != 0) {
            ide_abort_command(s);
          } else {
            s->mult_sectors = s->nsector;
            s->status = READY_STAT;
          }
          ide_set_irq(s);
          break;
        case WIN_VERIFY_EXT:
          lba48 = 1;
        case WIN_VERIFY:
        case WIN_VERIFY_ONCE:
          /* do sector number check? */
          ide_cmd_lba48_transform(s, lba48);
          s->status = READY_STAT;
          ide_set_irq(s);
          break;
        case WIN_READ_EXT:
          lba48 = 1;
        case WIN_READ:
        case WIN_READ_ONCE:
          if (!s->bs) {
            goto abort_cmd;
          }
          ide_cmd_lba48_transform(s, lba48);
          s->req_nb_sectors = 1;
          ide_sector_read(s);
          break;
        case WIN_WRITE_EXT:
          lba48 = 1;
        case WIN_WRITE:
        case WIN_WRITE_ONCE:
          ide_cmd_lba48_transform(s, lba48);
          s->error = 0;
          s->status = SEEK_STAT | READY_STAT;
          s->req_nb_sectors = 1;
          ide_transfer_start(s, s->io_buffer, 512, ide_sector_write);
          break;
        case WIN_MULTREAD_EXT:
          lba48 = 1;
        case WIN_MULTREAD:
          if (!s->mult_sectors) {
            goto abort_cmd;
          }
          ide_cmd_lba48_transform(s, lba48);
          s->req_nb_sectors = s->mult_sectors;
          ide_sector_read(s);
          break;
        case WIN_MULTWRITE_EXT:
          lba48 = 1;
        case WIN_MULTWRITE:
          if (!s->mult_sectors) {
            goto abort_cmd;
          }
          ide_cmd_lba48_transform(s, lba48);
          s->error = 0;
          s->status = SEEK_STAT | READY_STAT;
          s->req_nb_sectors = s->mult_sectors;
          n = s->nsector;
          if (n > s->req_nb_sectors) {
            n = s->req_nb_sectors;
          }
          ide_transfer_start(s, s->io_buffer, 512*n, ide_sector_write);
          break;
        case WIN_READDMA_EXT:
          lba48 = 1;
        case WIN_READDMA:
        case WIN_READDMA_ONCE:
          if (!s->bs) {
            goto abort_cmd;
          }
          ide_cmd_lba48_transform(s, lba48);
          ide_sector_write_dma(s);
          break;
        case WIN_READ_NATIVE_MAX_EXT:
          lba48 = 1;
        case WIN_READ_NATIVE_MAX:
          ide_cmd_lba48_transform(s, lba48);
          ide_set_sector(s, s->nb_sectors - 1);
          s->status = READY_STAT;
          ide_set_irq(s);
          break;
        case WIN_CHECKPOWERMODE1:
          s->nsector = 0xff; /* device active or idle */
          s->status = READY_STAT;
          ide_set_irq(s);
          break;
        case WIN_SETFEATURES:
          if (!s->bs) {
            goto abort_cmd;
          }
          /* XXX: valid for CDROM ? */
          switch(s->feature) {
            case 0x02: /* write cache enable */
            case 0x82: /* write cache disable */
            case 0xaa: /* read look-ahead enable */
            case 0x55: /* read look-ahead disable */
              s->status = READY_STAT | SEEK_STAT;
              ide_set_irq(s);
              break;
            case 0x03: { /* set transfer mode */
                         uint8_t val = s->nsector & 0x07;

                         switch (s->nsector >> 3) {
                           case 0x00: /* pio default */
                           case 0x01: /* pio mode */
                             put_le16(s->identify_data + 63,0x07);
                             put_le16(s->identify_data + 88,0x3f);
                             break;
                           case 0x04: /* mdma mode */
                             put_le16(s->identify_data + 63,0x07 | (1 << (val + 8)));
                             put_le16(s->identify_data + 88,0x3f);
                             break;
                           case 0x08: /* udma mode */
                             put_le16(s->identify_data + 63,0x07);
                             put_le16(s->identify_data + 88,0x3f | (1 << (val + 8)));
                             break;
                           default:
                             goto abort_cmd;
                         }
                         s->status = READY_STAT | SEEK_STAT;
                         ide_set_irq(s);
                         break;
                       }
            default:
                       goto abort_cmd;
          }
          break;
        case WIN_FLUSH_CACHE:
        case WIN_FLUSH_CACHE_EXT:
          if (s->bs) {
            bdrv_flush(s->bs);
          }
          s->status = READY_STAT;
          ide_set_irq(s);
          break;
        case WIN_STANDBYNOW1:
        case WIN_IDLEIMMEDIATE:
          s->status = READY_STAT;
          ide_set_irq(s);
          break;
          /* ATAPI commands */
        case WIN_PIDENTIFY:
          ide_abort_command(s);
          ide_set_irq(s);
          break;
        case WIN_DIAGNOSE:
          ide_set_signature(s);
          s->status = 0x00; /* NOTE: READY is _not_ set */
          s->error = 0x01;
          break;
        case WIN_SRST:
          goto abort_cmd;
        case WIN_PACKETCMD:
          goto abort_cmd;
        default:
abort_cmd:
          ide_abort_command(s);
          ide_set_irq(s);
          break;
      }
  }
}

static uint32_t
ide_ioport_read(void *opaque, uint32_t port)
{
  IDEState *ide_if = opaque;
  IDEState *s = ide_if->cur_drive;
  uint32_t addr;
  int ret, hob;

  addr = port & 7;
  /* FIXME: HOB readback uses bit 7, but it's always set right now */
  //hob = s->select & (1 << 7);
  hob = 0;
  switch(addr) {
    case 0:
      ret = 0xff;
      break;
    case 1:
      if (!ide_if[0].bs && !ide_if[1].bs)
        ret = 0;
      else if (!hob)
        ret = s->error;
      else
        ret = s->hob_feature;
      break;
    case 2:
      if (!ide_if[0].bs && !ide_if[1].bs)
        ret = 0;
      else if (!hob)
        ret = s->nsector & 0xff;
      else
        ret = s->hob_nsector;
      break;
    case 3:
      if (!ide_if[0].bs && !ide_if[1].bs)
        ret = 0;
      else if (!hob)
        ret = s->sector;
      else
        ret = s->hob_sector;
      break;
    case 4:
      if (!ide_if[0].bs && !ide_if[1].bs)
        ret = 0;
      else if (!hob) {
        ret = s->lcyl;
      } else {
        ret = s->hob_lcyl;
      }
      break;
    case 5:
      if (!ide_if[0].bs && !ide_if[1].bs)
        ret = 0;
      else if (!hob)
        ret = s->hcyl;
      else
        ret = s->hob_hcyl;
      break;
    case 6:
      if (!ide_if[0].bs && !ide_if[1].bs)
        ret = 0;
      else
        ret = s->select;
      break;
    default:
    case 7:
      if ((!ide_if[0].bs && !ide_if[1].bs) ||
          (s != ide_if && !s->bs))
        ret = 0;
      else {
        ret = s->status;
      }
      s->set_irq(s->irq_opaque, s->irq, 0);
      break;
  }

#ifdef DEBUG_IDE
  printf("ide: read addr=0x%x val=%02x\n", port, ret);
#endif
  return ret;
}

static uint32_t
ide_status_read(void *opaque, uint32_t addr)
{
  IDEState *ide_if = opaque;
  IDEState *s = ide_if->cur_drive;
  int ret;

  if ((!ide_if[0].bs && !ide_if[1].bs) ||
      (s != ide_if && !s->bs)) {
    ret = 0;
  } else {
    ret = s->status;
  }
#ifdef DEBUG_IDE
  printf("ide: read status addr=0x%x val=%02x\n", addr, ret);
#endif
  return ret;
}

static void
ide_cmd_write(void *opaque, uint32_t addr, uint32_t val)
{
  IDEState *ide_if = opaque;
  IDEState *s;
  int i;

#ifdef DEBUG_IDE             
  printf("ide: write control addr=0x%x val=%02x. cmd=0x%x\n", addr, val,
      ide_if[0].cmd);
#endif
  /* common for both drives */
  if (!(ide_if[0].cmd & IDE_CMD_RESET) &&
      (val & IDE_CMD_RESET)) { 
    /* reset low to high */
    for(i = 0;i < 2; i++) {
      s = &ide_if[i];
      s->status = BUSY_STAT | SEEK_STAT;
      s->error = 0x01;
    }
  } else if ((ide_if[0].cmd & IDE_CMD_RESET) &&
      !(val & IDE_CMD_RESET)) {
    /* high to low */
    for(i = 0;i < 2; i++) {
      s = &ide_if[i];
      s->status = READY_STAT | SEEK_STAT;
      ide_set_signature(s);
    }
  }

  ide_if[0].cmd = val;
  ide_if[1].cmd = val;
}



static void
ide_data_writew(void *opaque, uint32_t addr, uint32_t val)
{
  IDEState *ide_if = opaque;
  IDEState *s = ide_if->cur_drive;
  uint8_t *p;

  p = s->data_ptr;
  *(uint16_t *)p = le16_to_cpu(val);
  p += 2;
  s->data_ptr = p;
  if (p >= s->data_end) {
    s->end_transfer_func(s);
  }
}

static uint32_t
ide_data_readw(void *opaque, uint32_t addr)
{
  IDEState *ide_if = opaque;
  IDEState *s = ide_if->cur_drive;
  uint8_t *p;
  int ret;
  p = s->data_ptr;
  ret = cpu_to_le16(*(uint16_t *)p);
  //printf("%s(): p = %p, ret=%hx\n", __func__, p, ret);
  p += 2;
  s->data_ptr = p;
  if (p >= s->data_end)
    s->end_transfer_func(s);
  return ret;
}

static void
ide_data_writel(void *opaque, uint32_t addr, uint32_t val)
{
  IDEState *ide_if = opaque;
  IDEState *s = ide_if->cur_drive;
  uint8_t *p;

  p = s->data_ptr;
  *(uint32_t *)p = le32_to_cpu(val);
  p += 4;
  s->data_ptr = p;
  if (p >= s->data_end)
    s->end_transfer_func(s);
}

static uint32_t ide_data_readl(void *opaque, uint32_t addr)
{
  IDEState *ide_if = opaque;
  IDEState *s = ide_if->cur_drive;
  uint8_t *p;
  int ret;

  p = s->data_ptr;
  ret = cpu_to_le32(*(uint32_t *)p);
  p += 4;
  s->data_ptr = p;
  if (p >= s->data_end)
    s->end_transfer_func(s);
  return ret;
}


static void
ide_clear_hob(IDEState *ide_if)
{
  /* any write clears HOB high bit of device control register */
  ide_if[0].select &= ~(1 << 7);
  ide_if[1].select &= ~(1 << 7);
}

static void
ide_identify(IDEState *s)
{
  uint16_t *p;
  unsigned int oldsize;
  char buf[20];

  if (s->identify_set) {
    memcpy(s->io_buffer, s->identify_data, sizeof(s->identify_data));
    return;
  }

  memset(s->io_buffer, 0, 512);
  p = (uint16_t *)s->io_buffer;
  //printf("%s(): p = %p\n", __func__, p);
  put_le16(p + 0, 0x0040);
  put_le16(p + 1, s->cylinders);
  put_le16(p + 3, s->heads);
  put_le16(p + 4, 512 * s->sectors); /* XXX: retired, remove ? */
  put_le16(p + 5, 512); /* XXX: retired, remove ? */
  put_le16(p + 6, s->sectors);
  snprintf(buf, sizeof(buf), "MR%05d", s->drive_serial);
  padstr((uint8_t *)(p + 10), buf, 20); /* serial number */
  put_le16(p + 20, 3); /* XXX: retired, remove ? */
  put_le16(p + 21, 512); /* cache size in sectors */
  put_le16(p + 22, 4); /* ecc bytes */
  padstr((uint8_t *)(p + 23), "0", 8); /* firmware version */
  padstr((uint8_t *)(p + 27), "MONITOR HARDDISK", 40); /* model */
#if MAX_MULT_SECTORS > 1    
  put_le16(p + 47, 0x8000 | MAX_MULT_SECTORS);
#endif
  put_le16(p + 48, 1); /* dword I/O */
  put_le16(p + 49, (1 << 11) | (1 << 9) | (1 << 8)); /* DMA and LBA supported */
  put_le16(p + 51, 0x200); /* PIO transfer cycle */
  put_le16(p + 52, 0x200); /* DMA transfer cycle */
  put_le16(p + 53, 1 | (1 << 1) | (1 << 2)); /* words 54-58,64-70,88 are valid */
  put_le16(p + 54, s->cylinders);
  put_le16(p + 55, s->heads);
  put_le16(p + 56, s->sectors);
  oldsize = s->cylinders * s->heads * s->sectors;
  put_le16(p + 57, oldsize);
  put_le16(p + 58, oldsize >> 16);
  if (s->mult_sectors)
    put_le16(p + 59, 0x100 | s->mult_sectors);
  put_le16(p + 60, s->nb_sectors);
  put_le16(p + 61, s->nb_sectors >> 16);
  put_le16(p + 63, 0x07); /* mdma0-2 supported */
  put_le16(p + 65, 120);
  put_le16(p + 66, 120);
  put_le16(p + 67, 120);
  put_le16(p + 68, 120);
  put_le16(p + 80, 0xf0); /* ata3 -> ata6 supported */
  put_le16(p + 81, 0x16); /* conforms to ata5 */
  put_le16(p + 82, (1 << 14));
  /* 13=flush_cache_ext,12=flush_cache,10=lba48 */
  put_le16(p + 83, (1 << 14) | (1 << 13) | (1 <<12) | (1 << 10));
  put_le16(p + 84, (1 << 14));
  put_le16(p + 85, (1 << 14));
  /* 13=flush_cache_ext,12=flush_cache,10=lba48 */
  put_le16(p + 86, (1 << 14) | (1 << 13) | (1 <<12) | (1 << 10));
  put_le16(p + 87, (1 << 14));
  put_le16(p + 88, 0x3f | (1 << 13)); /* udma5 set and supported */
  put_le16(p + 93, 1 | (1 << 14) | 0x2000);
  put_le16(p + 100, s->nb_sectors);
  put_le16(p + 101, s->nb_sectors >> 16);
  put_le16(p + 102, s->nb_sectors >> 32);
  put_le16(p + 103, s->nb_sectors >> 48);


  //printf("cylinders=0x%x, heads=0x%x, p[1]=0x%x, p[3]=0x%x\n", s->cylinders, s->heads, p[1], p[3]);
  memcpy(s->identify_data, p, sizeof(s->identify_data));
  s->identify_set = 1;
}

/* prepare data transfer and tell what to do after */
static void
ide_transfer_start(IDEState *s, uint8_t *buf, int size,
    EndTransferFunc *end_transfer_func)
{
  s->end_transfer_func = end_transfer_func;
  s->data_ptr = buf;
  s->data_end = buf + size;
  s->status |= DRQ_STAT;
}

static void
ide_transfer_stop(IDEState *s)
{
  s->end_transfer_func = ide_transfer_stop;
  s->data_ptr = s->io_buffer;
  s->data_end = s->io_buffer;
  s->status &= ~DRQ_STAT;
}

static void
ide_abort_command(IDEState *s)
{
  s->status = READY_STAT | ERR_STAT;
  s->error = ABRT_ERR;
}

static void
ide_set_irq(IDEState *s)
{
  BMDMAState *bm = s->bmdma;
  if (!(s->cmd & IDE_CMD_DISABLE_IRQ)) {
    if (bm) {
      bm->status |= BM_STATUS_INT;
    }
    s->set_irq(s->irq_opaque, s->irq, 1);
  }
}

static void
ide_cmd_lba48_transform(IDEState *s, int lba48)
{
  s->lba48 = lba48;

  /* handle the 'magic' 0 nsector count conversion here. to avoid
   * fiddling with the rest of the read logic, we just store the
   * full sector count in ->nsector and ignore ->hob_nsector from now
   */
  if (!s->lba48) {
    if (!s->nsector)
      s->nsector = 256;
  } else {
    if (!s->nsector && !s->hob_nsector)
      s->nsector = 65536;
    else {
      int lo = s->nsector;
      int hi = s->hob_nsector;

      s->nsector = (hi << 8) | lo;
    }
  }
}

static void
ide_sector_read(IDEState *s)
{
  int64_t sector_num;
  int ret, n;

  s->status = READY_STAT | SEEK_STAT;
  s->error = 0; /* not needed by IDE spec, but needed by Windows */
  sector_num = ide_get_sector(s);
  n = s->nsector;
  if (n == 0) {
    /* no more sector to read from disk */
    ide_transfer_stop(s);
  } else {
#if defined(DEBUG_IDE)
    printf("read sector=%lld ", sector_num);
#endif
    if (n > s->req_nb_sectors)
      n = s->req_nb_sectors;

    ret = bdrv_read(s->bs, sector_num, s->io_buffer, n);
    ide_transfer_start(s, s->io_buffer, 512 * n, ide_sector_read);
    ide_set_irq(s);
    ide_set_sector(s, sector_num + n);
    s->nsector -= n;
  }

}

static void
ide_sector_write(IDEState *s)
{
  int64_t sector_num;
  int ret, n, n1;

  s->status = READY_STAT | SEEK_STAT;
  sector_num = ide_get_sector(s);
#if defined(DEBUG_IDE)
  printf("write sector=%Ld\n", sector_num);
#endif
  n = s->nsector;
  if (n > s->req_nb_sectors)
    n = s->req_nb_sectors;
  ret = bdrv_write(s->bs, sector_num, s->io_buffer, n);
  s->nsector -= n;
  if (s->nsector == 0) {
    /* no more sector to write */
    ide_transfer_stop(s);
  } else {
    n1 = s->nsector;
    if (n1 > s->req_nb_sectors)
      n1 = s->req_nb_sectors;
    ide_transfer_start(s, s->io_buffer, 512 * n1, ide_sector_write);
  }
  ide_set_sector(s, sector_num + n);
  ide_set_irq(s);
}

static void
ide_sector_write_dma(IDEState *s)
{
  s->status = READY_STAT | SEEK_STAT | DRQ_STAT | BUSY_STAT;
  s->io_buffer_index = 0;
  s->io_buffer_size = 0;
  ide_dma_start(s, ide_write_dma_cb);
}

static void
ide_dma_start(IDEState *s, BlockDriverCompletionFunc *dma_cb)
{
  BMDMAState *bm = s->bmdma;
  if(!bm)
    return;
  bm->ide_if = s;
  bm->dma_cb = dma_cb;
  bm->cur_prd_last = 0;
  bm->cur_prd_addr = 0;
  bm->cur_prd_len = 0;
  if (bm->status & BM_STATUS_DMAING) {
    bm->dma_cb(bm, 0);
  }
}


static void
ide_write_dma_cb(void *opaque, int ret)
{
  BMDMAState *bm = opaque;
  IDEState *s = bm->ide_if;
  int n;
  int64_t sector_num;

  n = s->io_buffer_size >> 9;
  sector_num = ide_get_sector(s);
  if (n > 0) {
    sector_num += n;
    ide_set_sector(s, sector_num);
    s->nsector -= n;
  }

  /* end of transfer ? */
  if (s->nsector == 0) {
    s->status = READY_STAT | SEEK_STAT;
    ide_set_irq(s);
eot:
    bm->status &= ~BM_STATUS_DMAING;
    bm->status |= BM_STATUS_INT;
    bm->dma_cb = NULL;
    bm->ide_if = NULL;
    bm->aiocb = NULL;
    return;
  }

  /* launch next transfer */
  n = s->nsector;
  if (n > MAX_MULT_SECTORS) {
    n = MAX_MULT_SECTORS;
  }
  s->io_buffer_index = 0;
  s->io_buffer_size = n * 512;

  if (dma_buf_rw(bm, 0) == 0)
    goto eot;
  bm->aiocb = bdrv_aio_write(s->bs, sector_num, s->io_buffer, n,
      ide_write_dma_cb, bm);
}

static int64_t
ide_get_sector(IDEState *s)
{
  int64_t sector_num;
  if (s->select & 0x40) {
    /* lba */
    if (!s->lba48) {
      sector_num = ((s->select & 0x0f) << 24) | (s->hcyl << 16) |
        (s->lcyl << 8) | s->sector;
    } else {
      sector_num = ((int64_t)s->hob_hcyl << 40) |
        ((int64_t) s->hob_lcyl << 32) |
        ((int64_t) s->hob_sector << 24) |
        ((int64_t) s->hcyl << 16) |
        ((int64_t) s->lcyl << 8) | s->sector;
    }
  } else {
    sector_num = ((s->hcyl << 8) | s->lcyl) * s->heads * s->sectors +
      (s->select & 0x0f) * s->sectors + (s->sector - 1);
  }
  return sector_num;
}

static void
ide_set_sector(IDEState *s, int64_t sector_num)
{
  unsigned int cyl, r;
  if (s->select & 0x40) {
    if (!s->lba48) {
      s->select = (s->select & 0xf0) | (sector_num >> 24);
      s->hcyl = (sector_num >> 16);
      s->lcyl = (sector_num >> 8);
      s->sector = (sector_num);
    } else {
      s->sector = sector_num;
      s->lcyl = sector_num >> 8;
      s->hcyl = sector_num >> 16;
      s->hob_sector = sector_num >> 24;
      s->hob_lcyl = sector_num >> 32;
      s->hob_hcyl = sector_num >> 40;
    }
  } else {
    cyl = sector_num / (s->heads * s->sectors);
    r = sector_num % (s->heads * s->sectors);
    s->hcyl = cyl >> 8;
    s->lcyl = cyl;
    s->select = (s->select & 0xf0) | ((r / s->sectors) & 0x0f);
    s->sector = (r % s->sectors) + 1;
  }
}

static void
ide_set_signature(IDEState *s)
{
  s->select &= 0xf0; /* clear head */
  /* put signature */
  s->nsector = 1;
  s->sector = 1;
  if (0/*s->is_cdrom*/) {
    s->lcyl = 0x14;
    s->hcyl = 0xeb;
  } else if (s->bs) {
    s->lcyl = 0;
    s->hcyl = 0;
  } else {
    s->lcyl = 0xff;
    s->hcyl = 0xff;
  }

}

/* return 0 if buffer completed */
static int
dma_buf_rw(BMDMAState *bm, int is_write)
{
  IDEState *s = bm->ide_if;
  struct {
    uint32_t addr;
    uint32_t size;
  } prd;
  int l, len;

  for(;;) {
    l = s->io_buffer_size - s->io_buffer_index;
    if (l <= 0)
      break;
    if (bm->cur_prd_len == 0) {
      /* end of table (with a fail safe of one page) */
      if (bm->cur_prd_last ||
          (bm->cur_addr - bm->addr) >= 4096)
        return 0;
      cpu_physical_memory_read(bm->cur_addr, (uint8_t *)&prd, 8);
      bm->cur_addr += 8;
      prd.addr = le32_to_cpu(prd.addr);
      prd.size = le32_to_cpu(prd.size);
      len = prd.size & 0xfffe;
      if (len == 0) {
        len = 0x10000;
      }
      bm->cur_prd_len = len;
      bm->cur_prd_addr = prd.addr;
      bm->cur_prd_last = (prd.size & 0x80000000);
    }
    if (l > (int)bm->cur_prd_len) {
      l = bm->cur_prd_len;
    }
    if (l > 0) {
      if (is_write) {
        cpu_physical_memory_write(bm->cur_prd_addr,
            s->io_buffer + s->io_buffer_index, l);
      } else {
        cpu_physical_memory_read(bm->cur_prd_addr,
            s->io_buffer + s->io_buffer_index, l);
      }
      bm->cur_prd_addr += l;
      bm->cur_prd_len -= l;
      s->io_buffer_index += l;
    }
  }
  return 1;
}


static void
padstr(char *str, const char *src, int len)
{
  int i, v;
  for(i = 0; i < len; i++) {
    if (*src)
      v = *src++;
    else
      v = ' ';
    *(char *)((long)str ^ 1) = v;
    str++;
  }
}

static void
put_le16(uint16_t *p, unsigned int v)
{
  *p = (uint16_t)v;
}

static uint32_t
le32_to_cpu(uint32_t x)
{
  return x;
}

static uint32_t
le16_to_cpu(uint32_t x)
{
  return x;
}

static uint32_t
cpu_to_le32(uint32_t x)
{
  return x;
}

static uint32_t
cpu_to_le16(uint32_t x)
{
  return x;
}

static void
ide_dummy_transfer_stop(IDEState *s)
{
  s->data_ptr = s->io_buffer;
  s->data_end = s->io_buffer;
  memset(s->io_buffer, 0xff, 4);
}

static void
ide_reset(IDEState *s)
{
  s->cur_drive = s;
  s->status = READY_STAT;
  ide_set_signature(s);
  /* init the transfer handler so that 0xffff is returned on data accesses. */
  s->end_transfer_func = ide_dummy_transfer_stop;
  ide_dummy_transfer_stop(s);
}

static void set_irq(void *opaque, int irq_num, int level)
{
  ASSERT(opaque == NULL);
  if (level == 1) {
    //printf("%s: setting CPU_INTERRUPT_HARD\n", __FILE__);
    cpu_interrupt(CPU_INTERRUPT_HARD);
  }
  pic_set_irq(&vcpu.isa_pic, irq_num, level);
}
