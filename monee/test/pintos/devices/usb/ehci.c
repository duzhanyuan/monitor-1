/**
 *
 * Actual EHCI will be implemented later
 *
 * For now, we just deactivate the EHCI controller and routing circuitry
 * so that any USB2.0 devices activated by the BIOS will show up on the 
 * USB1.1 controllers instead of being routed to EHCI and therefore "invisible"
 * to the system.
 *
 */
#include "devices/usb/ehci.h"
#include <stdlib.h>
#include <stdio.h>
#include <errors.h>
#include "devices/pci.h"
#include "devices/timer.h"
#include "devices/usb/usb.h"
#include "threads/malloc.h"

/* capability registers */
#define EHCI_REG_CAPLENGTH	    0x00
#define EHCI_REG_CAP_HCS_PARAMS 0x04
#define EHCI_REG_CAP_HCC_PARAMS 0x08

/* operational regisers - must be offset by op_base */
#define EHCI_REG_COMMAND      0x00
#define EHCI_REG_STATUS       0x04
#define EHCI_REG_INTR_ENABLE  0x08
#define EHCI_REG_CONFIGFLAG	  0x40
#define EHCI_REG_PORTSTATUS	  0x44

/* 31:23 reserved */
#define PORT_WKOC_E (1<<22)   /* wake on overcurrent (enable) */
#define PORT_WKDISC_E (1<<21)   /* wake on disconnect (enable) */
#define PORT_WKCONN_E (1<<20)   /* wake on connect (enable) */
/* 19:16 for port testing */
#define PORT_LED_OFF  (0<<14)
#define PORT_LED_AMBER  (1<<14)
#define PORT_LED_GREEN  (2<<14)
#define PORT_LED_MASK (3<<14)
#define PORT_OWNER  (1<<13)   /* true: companion hc owns this port */
#define PORT_POWER  (1<<12)   /* true: has power (see PPC) */
#define PORT_USB11(x) (((x)&(3<<10))==(1<<10))  /* USB 1.1 device */
/* 11:10 for detecting lowspeed devices (reset vs release ownership) */
/* 9 reserved */
#define PORT_RESET  (1<<8)    /* reset port */
#define PORT_SUSPEND  (1<<7)    /* suspend port */
#define PORT_RESUME (1<<6)    /* resume it */
#define PORT_OCC  (1<<5)    /* over current change */
#define PORT_OC   (1<<4)    /* over current active */
#define PORT_PEC  (1<<3)    /* port enable change */
#define PORT_PE   (1<<2)    /* port enable */
#define PORT_CSC  (1<<1)    /* connect status change */
#define PORT_CONNECT  (1<<0)    /* device connected */
#define PORT_RWC_BITS   (PORT_CSC | PORT_PEC | PORT_OCC)

#define HCS_N_PORTS(p)    (((p)>>0)&0xf)  /* bits 3:0, ports on HC */

/* 23:16 is r/w intr rate, in microframes; default "8" == 1/msec */
#define CMD_PARK  (1<<11)   /* enable "park" on async qh */
#define CMD_PARK_CNT(c) (((c)>>8)&3)  /* how many transfers to park for */
#define CMD_LRESET  (1<<7)    /* partial reset (no ports, etc) */
#define CMD_IAAD  (1<<6)    /* "doorbell" interrupt async advance */
#define CMD_ASE   (1<<5)    /* async schedule enable */
#define CMD_PSE   (1<<4)    /* periodic schedule enable */
/* 3:2 is periodic frame list size */
#define CMD_RESET (1<<1)    /* reset HC not bus */
#define CMD_RUN   (1<<0)    /* start/stop HC */

#define STS_ASS   (1<<15)   /* Async Schedule Status */
#define STS_PSS   (1<<14)   /* Periodic Schedule Status */
#define STS_RECL  (1<<13)   /* Reclamation */
#define STS_HALT  (1<<12)   /* Not running (any reason) */

struct ehci_data {
  struct pci_dev *pd;
  struct pci_io *io;
  uint8_t op_base;
};

static int
handshake (struct ehci_data *ehci, int reg, uint32_t mask, uint32_t done,
    int usec)
{
  uint32_t result;
  uint8_t op_base;

  op_base = ehci->op_base;

  do {
    result = pci_reg_read32 (ehci->io, op_base + reg);
    if (result == ~(uint32_t)0) {  /* card removed */
      return -ENODEV;
    }
    result &= mask;
    if (result == done) {
      return 0;
    }
    timer_udelay (1);
    usec--;
  } while (usec > 0);
  return -ETIMEDOUT;
}

static void
ehci_halt(struct ehci_data *ehci)
{
  uint32_t temp;
  uint8_t op_base;
  struct pci_io *io;
  int ret;

  op_base = ehci->op_base;
  io = ehci->io;
  temp = pci_reg_read32 (io, op_base + EHCI_REG_STATUS);
  pci_reg_write32 (io, op_base + EHCI_REG_INTR_ENABLE, 0);
  if ((temp & STS_HALT) != 0) {
    return;
  }
  //printf("%s(): stopping EHCI. status=0x%x\n", __func__, temp);
  temp = pci_reg_read32 (io, op_base + EHCI_REG_STATUS);
  //printf("%s() %d: status=0x%x\n", __func__, __LINE__, temp);
  temp = pci_reg_read32 (io, op_base + EHCI_REG_STATUS);
  //printf("%s() %d: status=0x%x\n", __func__, __LINE__, temp);
  temp = pci_reg_read32 (io, op_base + EHCI_REG_COMMAND);
  //printf("%s(): command=0x%x\n", __func__, temp);
  temp &= ~CMD_RUN;
  pci_reg_write32 (io, op_base + EHCI_REG_COMMAND, temp);
  temp = pci_reg_read32 (io, op_base + EHCI_REG_STATUS);
  //printf("%s() %d: status=0x%x\n", __func__, __LINE__, temp);
  temp = pci_reg_read32 (io, op_base + EHCI_REG_STATUS);
  //printf("%s() %d: status=0x%x\n", __func__, __LINE__, temp);
  temp = pci_reg_read32 (io, op_base + EHCI_REG_STATUS);
  //printf("%s() %d: status=0x%x\n", __func__, __LINE__, temp);

  ret = handshake(ehci, EHCI_REG_STATUS, STS_HALT, STS_HALT, 16*125);
  ASSERT(ret == 0);
  return;
}

static void
ehci_turn_off_all_ports(struct ehci_data *ehci)
{
  uint32_t hcs_params;
  int port;
  
  hcs_params = pci_reg_read32 (ehci->io, EHCI_REG_CAP_HCS_PARAMS);
  port = HCS_N_PORTS(hcs_params);
  //printf("%s(): port=%d\n", __func__, port);

  while (port--) {
    pci_reg_write32 (ehci->io, ehci->op_base + EHCI_REG_PORTSTATUS + port*4,
        PORT_RWC_BITS);
  }
}

static void
ehci_silence_controller(struct ehci_data *ehci)
{
  uint32_t cf;
  ehci_halt(ehci);
  ehci_turn_off_all_ports(ehci);

  /* turn off EHCI routing */
  cf = pci_reg_read32 (ehci->io, ehci->op_base + EHCI_REG_CONFIGFLAG);
  pci_reg_write32 (ehci->io, ehci->op_base + EHCI_REG_CONFIGFLAG, 0);
  cf = pci_reg_read32 (ehci->io, ehci->op_base + EHCI_REG_CONFIGFLAG);
  timer_msleep(100);
}

static void
ehci_reset(struct ehci_data *ehci)
{
  uint32_t command;
  int ret;
  command = pci_reg_read32(ehci->io, ehci->op_base + EHCI_REG_COMMAND);
  command |= CMD_RESET;
  pci_reg_write32(ehci->io, ehci->op_base + EHCI_REG_COMMAND, command);
  ret = handshake(ehci, EHCI_REG_COMMAND, CMD_RESET, 0, 250 * 1000);
  ASSERT(ret == 0);
}

/* idle the controller (from running) */
static void ehci_quiesce (struct ehci_data *ehci)
{
  uint32_t temp;

  /* wait for any schedule enables/disables to take effect */
  temp = pci_reg_read32(ehci->io, ehci->op_base + EHCI_REG_COMMAND) << 10;
  //printf("%s(): command=0x%x\n", __func__, temp);
  temp &= STS_ASS | STS_PSS;
  if (handshake(ehci, EHCI_REG_STATUS, STS_ASS | STS_PSS, temp, 16 * 125)) {
    //printf("%s() %d:\n", __func__, __LINE__);
    return;
  }

  /* then disable anything that's still active */
  temp = pci_reg_read32(ehci->io, ehci->op_base + EHCI_REG_COMMAND);
  temp &= ~(CMD_ASE | CMD_IAAD | CMD_PSE);
  pci_reg_write32(ehci->io, ehci->op_base + EHCI_REG_COMMAND, temp);

  /* hardware can take 16 microframes to turn off ... */
  handshake(ehci, EHCI_REG_STATUS, STS_ASS|STS_PSS, 0, 16*125);
  //printf("%s() %d:\n", __func__, __LINE__);
}



static struct ehci_data *
ehci_card_register(struct pci_io *pio)
{
  struct ehci_data *ehci;
  //uint32_t hcc_params, offset;
  //int count = 64;

  printf("%s() %d:\n", __func__, __LINE__);
  ehci = malloc(sizeof(struct ehci_data));
  ASSERT(ehci);

  printf("%s() %d:\n", __func__, __LINE__);
  ehci->io = pio;
  printf("%s() %d:\n", __func__, __LINE__);
  ehci->op_base = pci_reg_read8 (pio, EHCI_REG_CAPLENGTH);
  //printf("ehci->op_base=0x%hhx\n", ehci->op_base);

#if 0
  ehci->pd = pio_to_pci_dev(ehci->io);
  hcc_params = pci_reg_read32 (pio, EHCI_REG_CAP_HCC_PARAMS);
  offset = (hcc_params >> 8) & 0xff;    // Get the address of first capability

  printf("%s(): hcc_params=%08x\n", __func__, hcc_params);
  printf("%s(): Try to perform the BIOS handoff procedure\n", __func__);

  while (offset && count--) {
    uint32_t cap;

    cap = pci_read_config32(ehci->pd, offset);

    if ((cap & 0xff) == 1) {
      printf("[EHCI]  cap=%08x\n", cap);
      printf("[EHCI]  LEGSUP capability found\n");
      uint8_t delay = 200;

      if ((cap & 0x10000)) {
        printf("[EHCI]  BIOS was owning the EHCI. Changing it.\n");
        pci_write_config8(ehci->pd, offset + 3, 1);
      }

      while ((cap & 0x10000) && delay > 0) {
        timer_udelay(40000);
        cap = pci_read_config32(ehci->pd, offset);
        printf("%s() %d:\n", __func__, __LINE__);
      }

      if (cap & 0x10000) {
        pci_write_config8(ehci->pd, offset + 2, 0);
        printf("%s() %d:\n", __func__, __LINE__);
      }
      pci_write_config32(ehci->pd, offset + 4, 0);
    } else if ((cap & 0xff) == 0) {
      cap = 0;
    }
    offset = (cap >> 8) & 0xff;
  }
  printf("Performing full reset of the EHCI\n");

  //Read the status register
  uint32_t value = pci_reg_read32(ehci->io, ehci->op_base + 0x04);
  printf("%s() %d: value=0x%x\n", __func__, __LINE__, value);
  if ((value & 0x1000) == 0) {
    // Halted flag not cleared? Then the EHCI is still running. Stop it.
    int nloop = 10;

    value = pci_reg_read32(ehci->io, ehci->op_base + 0);     // USBCMD reg
    value &= ~1;                      // clear RUN flag
    pci_reg_write32(ehci->io, ehci->op_base + 0, value);

    do {
      pci_reg_write32(ehci->io, ehci->op_base + 0x4, 0x3f);
      timer_udelay(40000);
      value = pci_reg_read32(ehci->io, ehci->op_base + 0x4);
      if ((value == ~(uint32_t)0) || value & 0x1000) {
        break;
      }
      printf("%s() %d: nloop=%d\n", __func__, __LINE__, nloop);
    } while(--nloop > 0);
  }

  pci_reg_write32(ehci->io, ehci->op_base + 0x08, 0);
  pci_reg_write32(ehci->io, ehci->op_base + 0x04, 0x3f);
  pci_reg_write32(ehci->io, ehci->op_base + 0x40, 0x0); // Unconfigure the chip

  pci_reg_write32(ehci->io, ehci->op_base, 2);
  timer_usleep(100000);
  pci_reg_write32(ehci->io, ehci->op_base, 0);

  hcc_params &= 0xf;

  while (hcc_params--) {
    pci_reg_write32(ehci->io, ehci->op_base + 0x44 + 4*hcc_params, 1 << 13);
  }
#endif

  printf("%s() %d:\n", __func__, __LINE__);
  ehci_quiesce(ehci);
  printf("%s() %d:\n", __func__, __LINE__);
  ehci_silence_controller(ehci);
  printf("%s() %d:\n", __func__, __LINE__);
  ehci_reset(ehci);
  printf("%s() %d:\n", __func__, __LINE__);
  return ehci;
}


void
ehci_init (void)
{
  struct pci_dev *pd;
  int dev_num;

  dev_num = 0;
  while ((pd = pci_get_dev_by_class (PCI_MAJOR_SERIALBUS, PCI_MINOR_USB,
          PCI_USB_IFACE_EHCI, dev_num)) != NULL) {
    struct pci_io *io;
    struct ehci_data *ehci;

    dev_num++;
    io = pci_io_enum (pd, NULL);
    if (io == NULL) {
      printf ("IO not found on EHCI device?\n");
      continue;
    }
    printf ("Disabling the EHCI controller #%d\n", dev_num - 1);
    ehci = ehci_card_register(io);
    free(ehci);
  }
}
