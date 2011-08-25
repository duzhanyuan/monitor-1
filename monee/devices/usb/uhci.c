#include "devices/usb/uhci.h"
#include <ctype.h>
#include <debug.h>
#include <string.h>
#include <errors.h>
#include <prints.h>
#include <stdbool.h>
#include <list.h>
#include <stdio.h>
#include "mem/palloc.h"
#include "mem/malloc.h"
#include "mem/vaddr.h"
#include "devices/timer.h"
#include "devices/pci.h"
#include "devices/usb/usb.h"
#include "devices/usb/usbdevice.h"
#include "devices/usb/usbhub.h"
#include "sys/io.h"
#include "sys/interrupt.h"
#include "sys/mode.h"
#include "threads/synch.h"


#define UHCI_REGSZ 0x20           /* register iospace size */
#define UHCI_MAX_PORTS 7

#define DISCOVER 2
#define PIPE 2
#define USB 2
#define INIT2 2
#define INIT 2
#define TIMEOUT 2

#ifdef __MONITOR__
#define ptov_uhci(x) ((((uint32_t)(x))&UHCI_PTR_T)?({ASSERT((uint32_t)(x)==UHCI_PTR_T); (void *)(x);}):((x)>=LOADER_MONITOR_BASE && (x)<LOADER_MONITOR_END)?ptov_mon(x):(void *)(x))
#define vtop_uhci(x) (((uint32_t)(x) >= LOADER_MONITOR_VIRT_BASE)?vtop_mon(x):(uint32_t)(x))
#else
#define ptov_uhci(x) ((void *)(x))
#define vtop_uhci(x) ((uint32_t)(x))
#endif

static char *hack_buffer;
static int hack_len;

/*
 * --------------------- UHCI registers ------------------------
 * Warning: These are BYTE offsets!
 */

#define UHCI_USBCMD         0x000 /* USB Command (r/w) */
#define UHCI_USBSTATUS      0x002 /* USB Status (r/wc) */
#define UHCI_USBINTEN       0x004 /* USB Interrupt Enable (r/w) */
#define UHCI_FRAMECOUNT     0x006 /* Frame Number (r/w) */
#define UHCI_FRAMELISTADDR  0x008 /* Framelist Base Address (LONGWORD!), 4KB aligned! (r/w) */
#define UHCI_SOFMOD         0x00c /* Start Of Frame Modify (upper byte?) (r/w) */
#define UHCI_PORT1STSCTRL   0x010 /* Port 1 Status/Control (r/wc) */
#define UHCI_PORT2STSCTRL   0x012 /* Port 2 Status/Control (r/wc) */

#define UHCI_USBLEGSUP    0xc0    /* legacy support */
#define UHCI_USBINTR    4   /* interrupt register */
#define UHCI_USBLEGSUP_RWC  0x8f00    /* the R/WC bits */
#define UHCI_USBLEGSUP_RO 0x5040    /* R/O and reserved bits */
#define UHCI_USBCMD_RUN   0x0001    /* RUN/STOP bit */
#define UHCI_USBCMD_HCRESET 0x0002    /* Host Controller reset */
#define UHCI_USBCMD_EGSM  0x0008    /* Global Suspend Mode */
#define UHCI_USBCMD_CONFIGURE 0x0040    /* Config Flag */
#define UHCI_USBINTR_RESUME 0x0002    /* Resume interrupt enable */



/* The code in this file is an interface to a UHCI controller. */

typedef struct uhci_queue_hdr {
    uint32_t    qh_hlink;
    uint32_t    qh_vlink;
    uint32_t    qh_data1;
    uint32_t    qh_data2;
} uhci_queue_hdr;

typedef struct uhci_transfer_desc {
    uint32_t 	td_link_ptr;
    uint32_t	td_status;
    uint32_t	td_token;
    uint32_t	td_buffer;
} uhci_transfer_desc;

typedef struct uhci_interrupt {
  struct interrupt_data *i_intr;
  struct uhci_transfer_desc *i_td;
  struct list_elem ls_elem;
} uhci_interrupt;

//#define MAX_DEVS			6

typedef struct td_node {
  struct list_elem td_node;
  uint32_t         td_bitmap[8];
  uint8_t          *td_page;

  struct list_elem td_elem;
} td_node;

/*
struct uhcibase
{
    struct Library          LibNode;
    struct uhci_staticdata   sd;
};
*/

/* List of pointers. */
struct plist {
  void *ptr;
  struct list_elem plist_elem;
};

#define UHCI_BITMAP_SIZE	128

typedef struct uhci_data {
    char                    name[32];

    struct list             intr_list;
    struct interrupt_data   *tmp;

    uint8_t                 reset;
    uint8_t                 running;

    uint16_t                iobase;
    //uint16_t                irq;

    uint32_t               *frame;

    struct pci_io          *io;
    uint8_t                 num_ports;

    struct timerequest     *tr;

    struct list             isochronous;
    struct list             interrupts;
    struct list             control_ls;
    struct list             control_fs;
    struct list             bulk;

    /* Interrupt queue headers */
    struct uhci_queue_hdr       *qh01;
    struct uhci_transfer_desc   *term_td;

    struct list_elem ls_elem;
} uhci_data;


struct list td_list;

#define KERNEL_VER 0
#define KERNEL_REL 0

/* usb 1.1 root hub device descriptor */
static const uint8_t usb11_rh_dev_descriptor [18] = {
  0x12,       /*  __u8  bLength; */
  0x01,       /*  __u8  bDescriptorType; Device */
  0x10, 0x01, /*  __le16 bcdUSB; v1.1 */

  0x09,       /*  __u8  bDeviceClass; HUB_CLASSCODE */
  0x00,       /*  __u8  bDeviceSubClass; */
  0x00,       /*  __u8  bDeviceProtocol; [ low/full speeds only ] */
  0x40,       /*  __u8  bMaxPacketSize0; 64 Bytes */

  0x6b, 0x1d, /*  __le16 idVendor; Linux Foundation */
  0x01, 0x00, /*  __le16 idProduct; device 0x0001 */
  KERNEL_VER, KERNEL_REL, /*  __le16 bcdDevice */

  0x03,       /*  __u8  iManufacturer; */
  0x02,       /*  __u8  iProduct; */
  0x01,       /*  __u8  iSerialNumber; */
  0x01        /*  __u8  bNumConfigurations; */
};


static const uint8_t fs_rh_config_descriptor [] = {
  /* one configuration */
  0x09,       /*  __u8  bLength; */
  0x02,       /*  __u8  bDescriptorType; Configuration */
  0x19, 0x00, /*  __le16 wTotalLength; */
  0x01,       /*  __u8  bNumInterfaces; (1) */
  0x01,       /*  __u8  bConfigurationValue; */
  0x00,       /*  __u8  iConfiguration; */
  0xc0,       /*  __u8  bmAttributes; 
                  Bit 7: must be set,
                  6: Self-powered,
                  5: Remote wakeup,
                  4..0: resvd */
  0x00,       /*  __u8  MaxPower; */

  /* USB 1.1: 
   * USB 2.0, single TT organization (mandatory):
   * one interface, protocol 0
   *
   * USB 2.0, multiple TT organization (optional):
   * two interfaces, protocols 1 (like single TT)
   * and 2 (multiple TT mode) ... config is
   * sometimes settable
   * NOT IMPLEMENTED
   */

  /* one interface */
  0x09,       /*  __u8  if_bLength; */
  0x04,       /*  __u8  if_bDescriptorType; Interface */
  0x00,       /*  __u8  if_bInterfaceNumber; */
  0x00,       /*  __u8  if_bAlternateSetting; */
  0x01,       /*  __u8  if_bNumEndpoints; */
  0x09,       /*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
  0x00,       /*  __u8  if_bInterfaceSubClass; */
  0x00,       /*  __u8  if_bInterfaceProtocol; [usb1.1 or single tt] */
  0x00,       /*  __u8  if_iInterface; */

  /* one endpoint (status change endpoint) */
  0x07,       /*  __u8  ep_bLength; */
  0x05,       /*  __u8  ep_bDescriptorType; Endpoint */
  0x81,       /*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
  0x03,       /*  __u8  ep_bmAttributes; Interrupt */
  0x02, 0x00, /*  __le16 ep_wMaxPacketSize; 1 + (MAX_ROOT_PORTS / 8) */
  0xff        /*  __u8  ep_bInterval; (255ms -- usb 2.0 spec) */
};

static const uint8_t root_hub_hub_des[] =
{
  0x09,       /*  __u8  bLength; */
  0x29,       /*  __u8  bDescriptorType; Hub-descriptor */
  0x02,       /*  __u8  bNbrPorts; */
  0x0a,       /* __u16  wHubCharacteristics; */
  0x00,       /*   (per-port OC, no power switching) */
  0x01,       /*  __u8  bPwrOn2pwrGood; 2ms */
  0x00,       /*  __u8  bHubContrCurrent; 0 mA */
  0x00,       /*  __u8  DeviceRemovable; *** 7 Ports max *** */
  0xff        /*  __u8  PortPwrCtrlMask; *** 7 ports max *** */
};


/* static helper functions. */
static struct uhci_transfer_desc *uhci_alloc_td(struct uhci_data *);
static void uhci_free_td(struct uhci_data *, struct uhci_transfer_desc *);
static void uhci_free_td_quick(struct uhci_data *, struct uhci_transfer_desc *);
static struct uhci_queue_hdr *uhci_alloc_qhdr(struct uhci_data *);
static void uhci_free_qhdr(struct uhci_data *, struct uhci_queue_hdr *);
static void uhci_free_qhdr_quick(struct uhci_data *, struct uhci_queue_hdr *);
static void uhci_global_reset(struct uhci_data *uhci);
static void uhci_reset(struct uhci_data *uhci);
static bool uhci_run(struct uhci_data *uhci, bool run);
static void uhci_rebuild_list(struct uhci_data *uhci);
static void uhci_insert_interrupt(struct uhci_data *uhci,
    struct usb_pipe *pipe);
static bool uhci_set_port_feature(struct uhci_data *, uint16_t, int);


/*** PCI config registers ***/

#define  PCI_USBREV              0x60    /* USB protocol revision */
#define  PCI_USBREV_MASK         0xff
#define  PCI_USBREV_PRE_1_0      0x00
#define  PCI_USBREV_1_0          0x10
#define  PCI_USBREV_1_1          0x11

#define  PCI_LEGSUP              0xc0    /* Legacy Support register */
#define  PCI_LEGSUP_USBPIRQDEN   0x2000  /* USB PIRQ D Enable */

#define  PCI_CBIO                0x20    /* configuration base IO */

#define  PCI_BASE_CLASS_SERIAL	 0x0c
#define  PCI_SUB_CLASS_USB		   0x03
#define  PCI_INTERFACE_UHCI      0x00

/*** UHCI registers ***/

#define  UHCI_CMD               0x00
#define  UHCI_CMD_RS            0x0001
#define  UHCI_CMD_HCRESET       0x0002
#define  UHCI_CMD_GRESET        0x0004
#define  UHCI_CMD_EGSM          0x0008
#define  UHCI_CMD_FGR           0x0010
#define  UHCI_CMD_SWDBG         0x0020
#define  UHCI_CMD_CF            0x0040
#define  UHCI_CMD_MAXP          0x0080

#define  UHCI_RESET_TIMEOUT     100

#define  UHCI_STS               0x02
#define  UHCI_STS_USBINT        0x0001
#define  UHCI_STS_USBEI         0x0002
#define  UHCI_STS_RD            0x0004
#define  UHCI_STS_HSE           0x0008
#define  UHCI_STS_HCPE          0x0010
#define  UHCI_STS_HCH           0x0020
#define  UHCI_STS_ALLINTRS      0x003f

#define  UHCI_INTR              0x04
#define  UHCI_INTR_TOCRCIE      0x0001
#define  UHCI_INTR_RIE          0x0002
#define  UHCI_INTR_IOCE         0x0004
#define  UHCI_INTR_SPIE         0x0008

#define  UHCI_FRNUM             0x06
#define  UHCI_FRNUM_MASK        0x03ff
#define UHCI_MAX_SOF_NUMBER     2047

#define  UHCI_FLBASEADDR        0x08

#define  UHCI_SOF               0x0c
#define  UHCI_SOF_MASK          0x7f
#define  UHCI_SOF_DEFAULT       0x40

#define UHCI_PORTSC1            0x010
#define UHCI_PORTSC2            0x012
#define UHCI_PORTSC_CCS         0x0001
#define UHCI_PORTSC_CSC         0x0002
#define UHCI_PORTSC_PE          0x0004
#define UHCI_PORTSC_POEDC       0x0008
#define UHCI_PORTSC_LS          0x0030
#define UHCI_PORTSC_LS_SHIFT    4
#define UHCI_PORTSC_RD          0x0040
#define UHCI_PORTSC_LSDA        0x0100
#define UHCI_PORTSC_PR          0x0200
#define UHCI_PORTSC_OCI         0x0400
#define UHCI_PORTSC_OCIC        0x0800
#define UHCI_PORTSC_SUSP        0x1000
#define UHCI_PORTSC_RES2   0x2000   /* reserved, write zeroes */
#define UHCI_PORTSC_RES3   0x4000   /* reserved, write zeroes */
#define UHCI_PORTSC_RES4   0x8000   /* reserved, write zeroes */


/* must write as zeroes */
#define WZ_BITS      (UHCI_PORTSC_RES2 | UHCI_PORTSC_RES3 | UHCI_PORTSC_RES4)

#define RWC_BITS  (UHCI_PORTSC_OCIC | UHCI_PORTSC_POEDC | UHCI_PORTSC_CSC)


#define URWMASK(x) \
    ((x) & (UHCI_PORTSC_SUSP | UHCI_PORTSC_PR | UHCI_PORTSC_RD | UHCI_PORTSC_PE))

#define TD_CTRL_ACTLEN_MASK 0x7FF /* actual length, encoded as n - 1 */

#define TD_TOKEN_EXPLEN_SHIFT 21
#define TD_TOKEN_EXPLEN_MASK  0x7FF /* expected length, encoded as n-1 */

#define uhci_actual_length(ctrl_sts)  (((ctrl_sts) + 1) & \
          TD_CTRL_ACTLEN_MASK)  /* 1-based */
#define uhci_expected_length(token) ((((token) >> TD_TOKEN_EXPLEN_SHIFT) + \
                1) & TD_TOKEN_EXPLEN_MASK)

#define uhci_explen(len)  ((((len) - 1) & TD_TOKEN_EXPLEN_MASK) << \
              TD_TOKEN_EXPLEN_SHIFT)


#define UHCI_FRAMELIST_COUNT    1024
#define UHCI_FRAMELIST_ALIGN    4096

#define UHCI_TD_ALIGN           16
#define UHCI_QH_ALIGN           16

#define UHCI_PTR_T	0x00000001
#define UHCI_PTR_TD	0x00000000
#define UHCI_PTR_QH	0x00000002
#define UHCI_PTR_VF	0x00000004

#define UHCI_TD_GET_ACTLEN(s)   (((s) + 1) & 0x3ff)
#define UHCI_TD_ZERO_ACTLEN(t)  ((t) | 0x3ff)
#define UHCI_TD_BITSTUFF        0x00020000
#define UHCI_TD_CRCTO           0x00040000
#define UHCI_TD_NAK             0x00080000
#define UHCI_TD_BABBLE          0x00100000
#define UHCI_TD_DBUFFER         0x00200000
#define UHCI_TD_STALLED         0x00400000
#define UHCI_TD_ACTIVE          0x00800000
#define UHCI_TD_IOC             0x01000000
#define UHCI_TD_IOS             0x02000000
#define UHCI_TD_LS              0x04000000
#define UHCI_TD_GET_ERRCNT(s)   (((s) >> 27) & 3)
#define UHCI_TD_SET_ERRCNT(n)   ((n) << 27)
#define UHCI_TD_SPD             0x20000000

#define UHCI_TD_PID_IN          0x00000069
#define UHCI_TD_PID_OUT         0x000000e1
#define UHCI_TD_PID_SETUP       0x0000002d
#define UHCI_TD_GET_PID(s)      ((s) & 0xff)
#define UHCI_TD_SET_DEVADDR(a)  ((a) << 8)
#define UHCI_TD_GET_DEVADDR(s)  (((s) >> 8) & 0x7f)
#define UHCI_TD_SET_ENDPT(e)    (((e)&0xf) << 15)
#define UHCI_TD_GET_ENDPT(s)    (((s) >> 15) & 0xf)
#define UHCI_TD_SET_DT(t)       ((t) << 19)
#define UHCI_TD_GET_DT(s)       (((s) >> 19) & 1)
#define UHCI_TD_SET_MAXLEN(l)   (((l)-1) << 21)
#define UHCI_TD_GET_MAXLEN(s)   ((((s) >> 21) + 1) & 0x7ff)
#define UHCI_TD_MAXLEN_MASK     0xffe00000

#define UHCI_TD_ERROR (UHCI_TD_BITSTUFF|UHCI_TD_CRCTO|UHCI_TD_BABBLE|UHCI_TD_DBUFFER|UHCI_TD_STALLED)

#define UHCI_TD_SETUP(len, endp, dev) (UHCI_TD_SET_MAXLEN(len) | \
    UHCI_TD_SET_ENDPT(endp) | UHCI_TD_SET_DEVADDR(dev) | UHCI_TD_PID_SETUP)
#define UHCI_TD_OUT(len, endp, dev, dt) (UHCI_TD_SET_MAXLEN(len) | \
    UHCI_TD_SET_ENDPT(endp) | UHCI_TD_SET_DEVADDR(dev) | \
    UHCI_TD_PID_OUT | UHCI_TD_SET_DT(dt))
#define UHCI_TD_IN(len, endp, dev, dt) (UHCI_TD_SET_MAXLEN(len) | \
    UHCI_TD_SET_ENDPT(endp) | UHCI_TD_SET_DEVADDR(dev) | UHCI_TD_PID_IN | \
    UHCI_TD_SET_DT(dt))

#define TD_TOKEN_DEVADDR_SHIFT  8
#define TD_TOKEN_TOGGLE_SHIFT 19
#define TD_TOKEN_TOGGLE   (1 << 19)
#define TD_TOKEN_EXPLEN_SHIFT 21
#define TD_TOKEN_EXPLEN_MASK  0x7FF /* expected length, encoded as n-1 */
#define TD_TOKEN_PID_MASK 0xFF
#define uhci_packetid(token)  ((token) & TD_TOKEN_PID_MASK)
#define uhci_packetout(token) (uhci_packetid(token) != UHCI_TD_PID_IN)
#define uhci_packetin(token)  (uhci_packetid(token) == UHCI_TD_PID_IN)


struct list uhci_list;

/*
 * We need a special accessor for the control/status word because it is
 * subject to asynchronous updates by the controller.
 */
static inline uint32_t
td_status(struct uhci_transfer_desc *td) {
  uint32_t status = td->td_status;

  barrier();
  return status;
}

/*
 * Map status to standard result codes
 *
 * <status> is (td_status(td) & 0xF60000), a.k.a.
 * uhci_status_bits(td_status(td)).
 * Note: <status> does not include the TD_CTRL_NAK bit.
 * <dir_out> is True for output TDs and False for input TDs.
 */
static int uhci_map_status(int status, int dir_out)
{
  if (!status)
    return 0;
  if (status & UHCI_TD_BITSTUFF)      /* Bitstuff error */
    return -EPROTO;
  if (status & UHCI_TD_CRCTO) {    /* CRC/Timeout */
    if (dir_out)
      return -EPROTO;
    else
      return -EILSEQ;
  }
  if (status & UHCI_TD_BABBLE)      /* Babble */
    return -EOVERFLOW;
  if (status & UHCI_TD_DBUFFER)     /* Buffer error */
    return -ENOSR;
  if (status & UHCI_TD_STALLED)     /* Stalled */
    return -EPIPE;
  return 0;
}


static void
uhci_handler (void *uhci_opaque)
{
  uint16_t frame, port1, port2, cmd, sof;
  struct uhci_data *uhci;
  struct uhci_queue_hdr *qh = NULL;
  struct usb_pipe *p;
  uint16_t status;

  ASSERT(uhci_opaque);
  uhci = uhci_opaque;

  status = inw(uhci->iobase + UHCI_STS);
  DBGn(USB, "[UHCI] Intr: status=%04x\n", status);

  /* Check if there is really an interrupt for us. */
  if ((status & (UHCI_STS_USBINT | UHCI_STS_USBEI)) == 0) {
    outw(uhci->iobase + UHCI_STS, status);
    return;
  }
  DBGn(USB, "%s: %s()\n", uhci->name, __func__);
  outw(uhci->iobase + UHCI_STS, status);

  ASSERT(!(status & ~(UHCI_STS_USBINT | UHCI_STS_USBEI | UHCI_STS_RD)));

  if (status & UHCI_STS_RD) {
    NOT_IMPLEMENTED();
  }

  if (!list_empty(&uhci->interrupts)) {
    enum intr_level old_level;
    struct list_elem *e;
    NOT_IMPLEMENTED();

    DBGn(USB, "%s: %s() %d\n", uhci->name, __func__, __LINE__);
    old_level = intr_disable();
    for (e = list_begin(&uhci->interrupts);
        e != list_end(&uhci->interrupts);
        e = list_next(e)) {
      struct usb_pipe *p;
      
      p = list_entry(e, struct usb_pipe, pipels_elem);
      /* If the link pointer of the queue does not point to the first transfer
       * descriptor, an interrupt has occurred. Unfortunately, only the first
       * one, who has added the interrupt to this pipe may receive data. */
      if (ptov_uhci(p->queue->qh_vlink & 0xfffffff1) != p->first_td) {
        struct uhci_interrupt *intr;

        if (!(p->first_td->td_status & UHCI_TD_NAK)) {
          struct list_elem *f;
          DBGn(USB, "[UHCI] Interrupt: pipe %p, vlink 0x%x, first_td %p\n", p,
              p->queue->qh_vlink, p->first_td);
          for (f = list_begin(&p->interrupts);
              f != list_end(&p->interrupts);
              f = list_next(f)) {
            struct uhci_interrupt *intr = list_entry(f, struct uhci_interrupt,
                ls_elem);
            NOT_IMPLEMENTED();
            //DBGn(USB, "[UHCI] Issuing interrupt %#x.\n", intr->intno);
            //raise_interrupt(intr->intno, 0, 0, 0);
          }
        }

        /* Reactivate the transfer descriptor. */
        p->first_td->td_status = UHCI_TD_ZERO_ACTLEN(UHCI_TD_SET_ERRCNT(3)
            | UHCI_TD_ACTIVE | UHCI_TD_IOC);
        p->first_td->td_token ^= 1 << 19;

        if (p->fullspeed) {
          p->first_td->td_status |= UHCI_TD_SPD;
        } else {
          p->first_td->td_status |= UHCI_TD_LS | UHCI_TD_SPD;
        }
        /* Link the first TD to the queue header. */
        p->queue->qh_vlink = vtop_uhci(p->first_td) | UHCI_PTR_TD;
      }
    }
    intr_set_level(old_level);
  }

  if (!list_empty(&uhci->control_ls)) {
    enum intr_level old_level;
    struct list_elem *e;

    DBGn(USB, "%s: %s() %d\n", uhci->name, __func__, __LINE__);
    old_level = intr_disable();
    for (e = list_begin(&uhci->control_ls);
        e != list_end(&uhci->control_ls);
        e = list_next(e)) {
      struct usb_pipe *p;
      struct uhci_transfer_desc *td;
      bool changed;

      p = list_entry(e, struct usb_pipe, pipels_elem);
      td = p->first_td;
      changed = false;

      if (p->queue->qh_vlink == UHCI_PTR_T) {
        p->last_td = (void *)UHCI_PTR_T;
      }

      while (td != ptov_uhci(p->queue->qh_vlink & 0xfffffff1)) {
        struct uhci_transfer_desc *tnext;
        DBGn(USB, "[UHCI] TD=%p\n", td);
        tnext = ptov_uhci(td->td_link_ptr & 0xfffffff1);
        uhci_free_td_quick(uhci, td);
        td = tnext;
        p->first_td = tnext;
        changed = true;
      }
      if (changed && p->queue->qh_vlink == UHCI_PTR_T) {
        DBGn(USB, "[UHCI] INTR Control pipe %p empty (slow)\n", p);
        p->error_code = 0;
        p->completed = true;
        cond_signal_intr(&p->completion_wait);
      }
      if (p->queue->qh_vlink != UHCI_PTR_T) {
        uint32_t ctrlstat, tdstatus;
        ctrlstat = td_status(td);
        tdstatus = ctrlstat & 0xF60000;
        MSG ("%s() [control_ls]: ctrlstat=0x%x, tdstatus=0x%x, status=0x%x\n",
            __func__, ctrlstat, tdstatus, status);
        if (tdstatus) {
          NOT_IMPLEMENTED();
        }
      }
    }
    intr_set_level(old_level);
  }

  if (!list_empty(&uhci->control_fs)) {
    enum intr_level old_level;
    struct list_elem *e;

    DBGn(USB, "%s: %s() %d\n", uhci->name, __func__, __LINE__);
    old_level = intr_disable();
    for (e = list_begin(&uhci->control_fs);
        e != list_end(&uhci->control_fs);
        e = list_next(e)) {
      struct usb_pipe *p;
      struct uhci_transfer_desc *td;
      bool changed;

      p = list_entry(e, struct usb_pipe, pipels_elem);
      td = p->first_td;
      changed = false;

      if (p->queue->qh_vlink & UHCI_PTR_T) {
        p->last_td = (void *)UHCI_PTR_T;
      }

      DBGn(USB, "%s() %d: p=%p, p->first_td=%p\n", __func__, __LINE__, p,
          p->first_td);
      DBE(USB,
          struct uhci_transfer_desc *t;
          t = ptov_uhci(p->queue->qh_vlink & 0xfffffff1);
          DBGn(USB, "[UHCI] Control Fast:  p->queue->qh_vlink=0x%08x. td=%p\n",
            p->queue->qh_vlink, td);
          while (!((uint32_t)t & UHCI_PTR_T)) {
            DBGn(USB, "[UHCI]     TD=%p (%08x %08x %08x %08x)\n", t,
              t->td_link_ptr, t->td_status, t->td_token, t->td_buffer);
            t = ptov_uhci(t->td_link_ptr & 0xfffffff1);
          });

      while (td != ptov_uhci(p->queue->qh_vlink & 0xfffffff1)) {
        struct uhci_transfer_desc *tnext;
        DBGn(USB, "[UHCI] td=%p\n", td);
        tnext = ptov_uhci(td->td_link_ptr & 0xfffffff1);
        uhci_free_td_quick(uhci, td);
        td = tnext;
        p->first_td = tnext;
        changed = true;
      }
      if (changed && p->queue->qh_vlink == UHCI_PTR_T) {
        DBGn(USB, "[UHCI] INTR Control pipe %p empty\n", p);
        p->error_code = 0;
        p->completed = true;
        cond_signal_intr(&p->completion_wait);
      }
      if (p->queue->qh_vlink != UHCI_PTR_T) {
        uint32_t ctrlstat, tdstatus;
        ctrlstat = td_status(td);
        tdstatus = ctrlstat & 0xF60000;
        /*
        printf("%s() [control_fs]: ctrlstat=0x%x, tdstatus=0x%x, status=0x%x\n",
            __func__, ctrlstat, tdstatus, status);
            */
        if (tdstatus) {
          //NOT_IMPLEMENTED();
        }
      }
    }
    intr_set_level(old_level);
  }

  if (!list_empty(&uhci->bulk)) {
    enum intr_level old_level;
    struct list_elem *e;

    //printf("%s: %s()\n", uhci->name, __func__);
    old_level = intr_disable();
    for (e = list_begin(&uhci->bulk);
        e != list_end(&uhci->bulk);
        e = list_next(e)) {
      struct usb_pipe *p;
      struct uhci_transfer_desc *td;
      bool changed;

      p = list_entry(e, struct usb_pipe, pipels_elem);
      td = p->first_td;
      changed = false;

      if (p->queue->qh_vlink == UHCI_PTR_T) {
        p->last_td = (void *)UHCI_PTR_T;
      }

#if 0
      while (td != (void *)UHCI_PTR_T) {
        struct uhci_transfer_desc *tnext;
        uint32_t ctrlstat, tdstatus;
        int len;
        ctrlstat = td_status(td);
        tdstatus = ctrlstat & 0xF60000;
        if (tdstatus & UHCI_TD_ACTIVE) {
          break;
        }
        len = uhci_actual_length(ctrlstat);
        if (tdstatus) {
          MSG ("ctrlstat=0x%x, tdstatus=0x%x, status=0x%x\n", ctrlstat,
              tdstatus, status);
          NOT_IMPLEMENTED();
        } else if (len < uhci_expected_length(td->td_token)) {
          MSG ("len=%d, expected_len=%d, ctrlstat=0x%x, tdstatus=0x%x, status=0x%x\n", len, uhci_expected_length(td->td_token), ctrlstat,
              tdstatus, status);
          NOT_IMPLEMENTED();
        }
        tnext = ptov_uhci(td->td_link_ptr & 0xfffffff1);
        //uhci_free_td_quick(uhci, td);
        DBGn(PIPE, "%s() %d: td=%p, tnext=%p. qh_vlink=%p\n", __func__,
            __LINE__,td, tnext, p->queue->qh_vlink);
        td = tnext;
        //p->first_td = tnext;
        changed = true;
      }
#endif
      while (td != ptov_uhci(p->queue->qh_vlink & 0xfffffff1)) {
        struct uhci_transfer_desc *tnext;
        tnext = ptov_uhci(td->td_link_ptr & 0xfffffff1);
        uhci_free_td_quick(uhci, td);
        DBGn(USB, "%s() %d: td=%p, tnext=%p. qh_vlink=0x%08x\n", __func__,
            __LINE__, td, tnext, p->queue->qh_vlink);
        td = tnext;
        p->first_td = tnext;
        changed = true;
      }
      if (p->queue->qh_vlink != UHCI_PTR_T) {
        uint32_t ctrlstat, tdstatus;
        unsigned len;
        ctrlstat = td_status(td);
        tdstatus = ctrlstat & 0xF60000;
        MSG ("%s() [bulk_fs]: ctrlstat=0x%x, tdstatus=0x%x, status=0x%x\n",
            __func__, ctrlstat, tdstatus, status);
        len = uhci_actual_length(ctrlstat);
        if (tdstatus) {
          NOT_IMPLEMENTED();
        } else if (len < uhci_expected_length(td->td_token)) {
          struct uhci_transfer_desc *tnext;
          int i;

          MSG ("buffer[%p] len %d:", hack_buffer, hack_len);
          for (i = 0; i < hack_len; i++) {
            MSG (" %02hhx", hack_buffer[i]);
          }
          MSG ("\n");
          NOT_IMPLEMENTED();
        }
      }

      if (changed && td == (void *)UHCI_PTR_T) {
        struct uhci_transfer_desc *tnext;
        DBGn(PIPE, "[UHCI] INTR Bulk pipe %p empty.\n", p);
        td = p->first_td;
        while (td != (void *)UHCI_PTR_T) {
          tnext = ptov_uhci(td->td_link_ptr & 0xfffffff1);
          uhci_free_td_quick(uhci, td);
          td = tnext;
        }
        p->first_td = (void *)UHCI_PTR_T;
        p->error_code = 0;
        p->completed = true;
        cond_signal_intr(&p->completion_wait);
      }
    }
    intr_set_level(old_level);
  }

}

static const usb_device_descriptor_t device_descriptor = {
  bLength:            sizeof(usb_device_descriptor_t),
  bDescriptorType:    UDESC_DEVICE,
  bcdUSB:             0x0101,
  bDeviceClass:       UDCLASS_HUB,
  bDeviceSubClass:    UDSUBCLASS_HUB,
  bDeviceProtocol:    UDPROTO_FSHUB,
  bMaxPacketSize:     64,
  iManufacturer:      2,
  iProduct:           1,
  iSerialNumber:      3,
  bNumConfigurations: 1,
};

static const usb_hub_descriptor_t hub_descriptor = {
  bDescLength:        sizeof(usb_hub_descriptor_t) - 31,
  bDescriptorType:    UDESC_HUB,
  bNbrPorts:          2,
  wHubCharacteristics:0,
  bPwrOn2PwrGood:     50,
  bHubContrCurrent:   0,
  DeviceRemovable:    {0,},
};

static const usb_endpoint_descriptor_t endpoint_descriptor = {
  bLength:            sizeof(usb_endpoint_descriptor_t),
  bDescriptorType:    UDESC_ENDPOINT,
  bEndpointAddress:   0x81,
  bmAttributes:       0x03,
  wMaxPacketSize:     1,
  bInterval:          255
};

static void
uhci_card_shutdown(struct uhci_data *uhci)
{
  int i;
  outw(uhci->iobase + UHCI_INTR, 0);
  outw(uhci->iobase + UHCI_CMD, 0);
  /* Clearing the RS bit will stop the controller. Clearing CF bit will tell
   * potential legacy USB BIOS that monitor takes over. */
  outw(uhci->iobase + UHCI_CMD,
      inw(uhci->iobase + UHCI_CMD) & ~(UHCI_CMD_RS | UHCI_CMD_CF));
  for (i = 0; i < 10; i++) {
    timer_msleep(1);
    if (inw(uhci->iobase + UHCI_STS) & UHCI_STS_HCH) {
      DBGn(INIT2, "[uhci] Host controller halted.\n");
      break;
    }
  }
}

static void
uhci_detect_ports (struct uhci_data *uhci)
{
  unsigned i;
  size_t io_size = pci_io_size(uhci->io);

  uhci->num_ports = 0;
  for (i = 0; i < (io_size - UHCI_PORTSC1)/2; i++) {
    uint16_t status;
    status = pci_reg_read16 (uhci->io, UHCI_PORTSC1 + i * 2);
    if (!(status & 0x0080) || status == 0xffff) {
      break;
    }
    uhci->num_ports++;
  }

  if (uhci->num_ports > UHCI_MAX_PORTS) {
    MSG ("%s: port count misdetected? forcing to 2 ports\n", uhci->name);
    uhci->num_ports = 2;
  } else {
    MSG ("%s: %d ports detected.\n", uhci->name, uhci->num_ports);
  }
}

static void
configure_hc(struct uhci_data *uhci)
{
  struct pci_dev *pcidev;

  pcidev = pio_to_pci_dev(uhci->io);

  /* Set the frame length to the default: 1 ms exactly */
  outb(uhci->iobase + UHCI_SOF, UHCI_SOF_DEFAULT);

  /* Store the frame list base address */
  outl(uhci->iobase + UHCI_FLBASEADDR, vtop(uhci->frame));

  /* Set the current frame number */
  outw(uhci->iobase + UHCI_FRNUM, 0);

  /* Enable PIRQ */
  pci_write_config16(pcidev, PCI_LEGSUP,
      PCI_LEGSUP_USBPIRQDEN);
}

static void
start_rh(struct uhci_data *uhci)
{
  /* Mark it configured and running with a 64-byte max packet.
   * All interrupts are enabled, even though RESUME won't do anything.
   */
  uhci->running = true;
  outw(uhci->iobase + UHCI_CMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
  outw(uhci->iobase + UHCI_INTR,
      UHCI_INTR_TOCRCIE | UHCI_INTR_RIE | UHCI_INTR_IOCE | UHCI_INTR_SPIE);
}

static void
uhci_check_and_reset(struct uhci_data *uhci)
{
  uint16_t legsup, intr;
  struct pci_dev *pd = pio_to_pci_dev(uhci->io);

  /*
   * When restarting a suspended controller, we expect all the
   * settings to be the same as we left them:
   *
   *  PIRQ and SMI disabled, no R/W bits set in USBLEGSUP;
   *  Controller is stopped and configured with EGSM set;
   *  No interrupts enabled except possibly Resume Detect.
   *
   * If any of these conditions are violated we do a complete reset.
   */
  legsup = pci_read_config16(pd, UHCI_USBLEGSUP);
  if (legsup & ~(UHCI_USBLEGSUP_RO | UHCI_USBLEGSUP_RWC)) {
    DBGn(USB, "%s: legsup = 0x%04x\n", __func__, legsup);
    goto reset_needed;
  }

  intr = inw(uhci->iobase + UHCI_USBINTR);
  if (intr & (~UHCI_USBINTR_RESUME)) {
    DBGn(USB, "%s: intr = 0x%04x\n", __func__, intr);
    goto reset_needed;
  }
  return;

reset_needed:
  uhci_reset(uhci);
}

/*
static inline int
get_hub_status_data(struct uhci_data *uhci, char *buf)
{
  int port;
  int mask = RWC_BITS;

  *buf = 0;
  for (port = 0; port < uhci->num_ports; ++port) {
    if (inw(uhci->iobase + UHCI_PORTSC1 + port * 2) & mask)
      *buf |= (1 << (port + 1));
  }
}
*/

/* A port that either is connected or has a changed-bit set will prevent
 * us from AUTO_STOPPING.
 */
static int
any_ports_active(struct uhci_data *uhci)
{
  int port;

  for (port = 0; port < uhci->num_ports; ++port) {
    if (inw(uhci->iobase + UHCI_PORTSC1 + port * 2) &
          (UHCI_PORTSC_CCS | RWC_BITS))
      return 1;
  }
  return 0;
}

#define CLR_RH_PORTSTAT(x) \
     status = inw(port_addr); \
   status &= ~(RWC_BITS|WZ_BITS); \
   status &= ~(x); \
   status |= RWC_BITS & (x); \
   outw(status, port_addr)

#define SET_RH_PORTSTAT(x) \
     status = inw(port_addr); \
   status |= (x); \
   status &= ~(RWC_BITS|WZ_BITS); \
   outw(status, port_addr)


static struct uhci_data *
uhci_card_register(struct pci_io *pio)
{
  struct uhci_data *uhci;
  struct pci_dev *pcidev;
  uint16_t reg;
  struct usbdevice *dev;
  struct usbhub *hub;
  usb_endpoint_descriptor_t const *ep;
  uint8_t sts;
  static int num_cards = 0;
  int i, port;

  uhci = malloc(sizeof(struct uhci_data));
  ASSERT(uhci);

  uhci->io = pio;
  pcidev = pio_to_pci_dev(pio);
  uhci->iobase = pci_io_base(pio);
  snprintf(uhci->name, sizeof uhci->name, "UHCI Card %d", num_cards);

  list_init(&uhci->isochronous);
  list_init(&uhci->interrupts);
  list_init(&uhci->control_ls);
  list_init(&uhci->control_fs);
  list_init(&uhci->bulk);
  list_init(&uhci->intr_list);

  /*
  if (uhci->tmp) {
    list_push_back(&uhci->intr_list, &uhci->tmp->ls_elem);
  }
  */

  uhci_detect_ports(uhci);
  uhci_check_and_reset(uhci);
  for (port = 0; port < uhci->num_ports; ++port) {
    outw(uhci->iobase + UHCI_PORTSC1 + (port * 2), 0);
  }

  DBE(INIT2, ({
     uint16_t status, frame, port1, port2, cmd;
     uint32_t base;
     uint8_t sof;
     base = inw(uhci->iobase + UHCI_FLBASEADDR);
     status = inw(uhci->iobase + UHCI_STS);
     frame = inw(uhci->iobase + UHCI_FRNUM);
     port1 = inw(uhci->iobase + UHCI_PORTSC1);
     port2 = inw(uhci->iobase + UHCI_PORTSC2);
     cmd = inw(uhci->iobase + UHCI_CMD);
     sof = inb(uhci->iobase + UHCI_SOF);
   
     DBGn(INIT2, "[uhci] Initial state: base=%08x, cmd=%04x, SOF=%02x, "
         "status=%04x, frame=%04x, port1=%04x, port2=%04x\n", base, cmd,
         sof, status, frame, port1, port2);
     })
  );

  uhci->frame = palloc_get_page(PAL_ASSERT | PAL_ZERO);

  /* Register interrupt handler. */
  pci_register_irq(pcidev, uhci_handler, uhci);

  uhci->term_td = uhci_alloc_td(uhci);
  uhci->term_td->td_status = 0;
  uhci->term_td->td_token =   uhci_explen(0)
                            | (0x7f << TD_TOKEN_DEVADDR_SHIFT)
                            | UHCI_TD_PID_IN;
  uhci->term_td->td_buffer = 0;
  uhci->term_td->td_link_ptr = UHCI_PTR_T;
  DBGn(USB, "uhci->term_td=%p\n", uhci->term_td);

  uhci->qh01 = uhci_alloc_qhdr(uhci);
  uhci->qh01->qh_vlink = vtop_uhci(uhci->term_td);
  uhci->qh01->qh_hlink = UHCI_PTR_T;

  for (i = 0; i < 1024; i++) {
    uhci->frame[i] = UHCI_PTR_QH | vtop_uhci(uhci->qh01);
  }

  DBGn(INIT2, "[UHCI] Enabling the controller.\n");

  DBGn(INIT2, "%s() %d: calling configure_hc()\n", __func__, __LINE__);
  configure_hc(uhci);
  DBGn(INIT2, "%s() %d: calling start_rh()\n", __func__, __LINE__);
  start_rh(uhci);
  register_usb_driver(uhci);

  /* Reset the uhci root hub ports. This call will also discover devices. */
  for (i = 1; i <= uhci->num_ports; i++) {
    uhci_set_port_feature(uhci, i, UHF_PORT_RESET);
  }

  num_cards++;
  return uhci;
}

void
usb_init (void)
{
  usb_devlist_init();
  usbmsd_init();
  MSG ("Initializing EHCI\n");
  ehci_init();
  MSG ("Initializing UHCI\n");
  uhci_init();
}

void
uhci_init(void)
{
  struct pci_dev *pd;
  int dev_num;

  list_init(&uhci_list);
  /* initialize static data structures. */
  list_init(&td_list);

  dev_num = 0;
  while ((pd = pci_get_dev_by_class (PCI_MAJOR_SERIALBUS, PCI_MINOR_USB,
          PCI_USB_IFACE_UHCI, dev_num)) != NULL) {
    struct pci_io *io;
    struct uhci_data *uhci;

    dev_num++;

    /* find IO space */
    io = NULL;
    while ((io = pci_io_enum(pd, io)) != NULL) {
      if (pci_io_size(io) == UHCI_REGSZ) {
        break;
      }
    }

    /* not found, next PCI */
    if (io == NULL) {
      continue;
    }

    uhci = uhci_card_register(io);
    list_push_back(&uhci_list, &uhci->ls_elem);
  }
}

/* This function allocates a new 16-byte transfer descriptor from the
 * pool of 4K-aligned PCI-accessible memory regions. Within each 4K page,
 * a bitmap is used to determine, which of the TD elements are available
 * for use.
 *
 * This function returns NULL if no free TD's are found and no more memory
 * is available.
 */
static struct uhci_transfer_desc *
uhci_alloc_td(struct uhci_data *uhci)
{
  struct td_node *n;
  uint32_t node_num = 32;
  uint32_t bmp_pos = 8;
  enum intr_level old_level;
  struct list_elem *e;

  old_level = intr_disable();

  /* Walk through the list of already available 4K pages */
  for (e = list_begin(&td_list); e != list_end(&td_list); e = list_next(e)) {
    /* For each 4K page, search the first free node (cleared bit) and alloc
     * it. */
    n = list_entry(e, struct td_node, td_elem);
    for (bmp_pos = 0; bmp_pos < 8; bmp_pos++) {
      if (n->td_bitmap[bmp_pos] != 0xffffffff) {
        for (node_num = 0; node_num < 32; node_num++) {
          if (!(n->td_bitmap[bmp_pos] & (1 << node_num))) {
            struct uhci_transfer_desc * td;
            td = (struct uhci_transfer_desc *)&n->td_page[(bmp_pos*32
                                                          + node_num) * 16];
            /* Mark the TD as used and return a pointer to it */
            n->td_bitmap[bmp_pos] |= 1 << node_num;
            intr_set_level(old_level);
            return td;
          }
        }
      }
    }
  }
  /*
   * No free TDs have been found on the list of 4K pages. create new page node
   * and alloc 4K PCI accessible memory region for it
   */
  if (n = malloc(sizeof(struct td_node))) {
    if (n->td_page = palloc_get_page(PAL_ZERO)) {
      struct uhci_transfer_desc *td;
      /* Make 4K node available for future allocations */
      list_push_front(&td_list, &n->td_elem);
      td = (struct uhci_transfer_desc *)&n->td_page[0];

      /* Mark first TD as used and return a pointer to it */
      n->td_bitmap[0] |= 1;
      intr_set_level(old_level);
      return td;
    }
    free(n);
  }
  /* Everything failed? Out of memory, most likely */
  intr_set_level(old_level);
  ABORT();
  return NULL;
}

/* This function allocates a new 8-bit Queue Header aligned at the 16-byte
 * boundary. See uhci_alloc_td for more details. */
static struct uhci_queue_hdr *
uhci_alloc_qhdr(struct uhci_data *uhci)
{
  /* Since the queue headers have to be aligned at the 16-byte boundary, they
   * may be allocated from the same pool TD's do. */
  return (struct uhci_queue_hdr *)uhci_alloc_td(uhci);
}

/* Mark the Transfer Descriptor free, so that it may be allocated by another
 * one. A quick version which may be called from interrupts.
 */
static void
uhci_free_td_quick(struct uhci_data *uhci, struct uhci_transfer_desc *td)
{
  struct td_node *t;
  enum intr_level old_level;
  struct list_elem *e;
  old_level = intr_disable();

  /* traverse through the list of 4K pages */ 
  for (e = list_begin(&td_list); e != list_end(&td_list); e = list_next(e)) {
    struct td_node *t;

    t = list_entry(e, struct td_node, td_elem);
    /* Address match? */
    if ((uint32_t)t->td_page == ((uint32_t)td & ~0xfff)) {
      /* extract the correct location of the TD within the bitmap */
      int bmp = (((uint32_t)td & 0xe00) >> 9);
      int node = (((uint32_t)td & 0x1f0) >> 4);
      /* Free the node */
      t->td_bitmap[bmp] &= ~(1 << node);
      break;
    }
  }
  intr_set_level(old_level);
}

static void
uhci_free_qhdr_quick(struct uhci_data *uhci, struct uhci_queue_hdr *qh)
{
  uhci_free_td_quick(uhci, (struct uhci_transfer_desc *)qh);
}

/*
 * Mark the transfer descriptor free, so that it may be allocated by another
 * one. If the 4K page contains no used descriptors, the page will be freed. */
void uhci_free_td(struct uhci_data *uhci, struct uhci_transfer_desc *td)
{
  struct td_node *t, *next;
  enum intr_level old_level;
  struct list_elem *e;

  old_level = intr_disable();

  /* traverse through the list of 4K pages */
  for (e = list_begin(&td_list); e != list_end(&td_list); e = list_next(e)) {
    t = list_entry(e, struct td_node, td_elem);
    /* Address match? */
    if ((uint32_t)t->td_page == ((uint32_t)td & ~0xfff)) {
      /* extract the correct location of the TD within the bitmap */
      int bmp = (((uint32_t)td & 0xe00) >> 9);
      int node = (((uint32_t)td & 0x1f0) >> 4);

      /* Free the node */
      t->td_bitmap[bmp] &= ~(1 << node);

      /* Check if all TD nodes are free within the 4K page */
      int i;
      for (i=0; i < 8; i++) {
        if (t->td_bitmap[i] != 0) {
          break;
        }
      }

      /* So it is. Free the 4K page */
      if (i==8) {
        list_remove(&t->td_elem);
        free(t->td_page);
        //HIDD_PCIDriver_FreePCIMem(uhci->pciDriver, t->td_Page);
        free(t);
      }
      break;
    }
  }
  intr_set_level(old_level);
}

static void
uhci_free_qhdr(struct uhci_data *uhci, struct uhci_queue_hdr *qh)
{
  uhci_free_td(uhci, (struct uhci_transfer_desc *)qh);
}

static void
uhci_global_reset(struct uhci_data *uhci)
{
  outw(uhci->iobase + UHCI_CMD, UHCI_CMD_GRESET);
  timer_msleep(USB_BUS_RESET_DELAY);
  outw(uhci->iobase + UHCI_CMD, 0);
}

static void
uhci_reset(struct uhci_data *uhci)
{
  struct pci_dev *pd = pio_to_pci_dev(uhci->io);
  int n;
  /* Turn off PIRQ enable and SMI enable.  (This also turns off the
   * BIOS's USB Legacy Support.)  Turn off all the R/WC bits too.
   */
  pci_write_config16(pd, UHCI_USBLEGSUP, UHCI_USBLEGSUP_RWC);

  outw(uhci->iobase + UHCI_CMD, UHCI_CMD_HCRESET);
  for (n = 0;
      n < UHCI_RESET_TIMEOUT && (inw(uhci->iobase+UHCI_CMD) & UHCI_CMD_HCRESET);
      n++) {
    timer_msleep(10);
  }

  if (n >= UHCI_RESET_TIMEOUT) {
    ERR("[UHCI] Device did not reset properly\n");
  }
  outw(uhci->iobase + UHCI_INTR, 0);
  outw(uhci->iobase + UHCI_CMD, 0);

}

/* Toggle the run/stop bit in the command register. */
static bool
uhci_run(struct uhci_data *uhci, bool run)
{
  bool running;
  uint16_t cmd;
  int n;

  cmd = inw(uhci->iobase + UHCI_CMD);
  if (run) {
    cmd |= UHCI_CMD_RS;
  } else {
    cmd &= ~UHCI_CMD_RS;
  }
  outw(uhci->iobase + UHCI_CMD, cmd);

  for (n = 0; n < 10; n++) {
    running = !(inw(uhci->iobase + UHCI_STS) & UHCI_STS_HCH);

    if (run == running) {
      DBGn(INIT2, "[UHCI] %s done. cmd=%04x, sts=%04x\n",
          run ? "start":"stop", inw(uhci->iobase + UHCI_CMD),
          inw(uhci->iobase + UHCI_STS));
      return true;
    }
    timer_msleep(10);
  }

  DBGn(INIT2, "[UHCI] Failed to change the running state\n");
  return false;
}

static void
uhci_rebuild_list(struct uhci_data *uhci)
{
  struct usb_pipe *p;
  struct uhci_queue_hdr *qh, *first_fast = NULL;
  struct list_elem *e;

  qh = uhci->qh01;

  /*
   * If low speed control list is not empty, iterate through all its elements
   * and link them linear using the qh pointer
   */
  if (!list_empty(&uhci->control_ls)) {
    for (e = list_begin(&uhci->control_ls);
        e != list_end(&uhci->control_ls);
        e = list_next(e)) {
      p = list_entry(e, struct usb_pipe, pipels_elem);
      /* The successor QH's horizontal link should point to end of the list
       * as long as the node is not linked properly. This way we guarantee,
       * that the list of QH's may be always executed */
      p->queue->qh_hlink = UHCI_PTR_T;
      qh->qh_hlink = vtop_uhci(p->queue) | UHCI_PTR_QH;
      qh = p->queue;
    }
  }

  if (!list_empty(&uhci->control_fs)) {
    struct usb_pipe *pipe;
    pipe = list_entry(list_head(&uhci->control_fs), struct usb_pipe,
        pipels_elem);
    first_fast = pipe->queue;

    for (e = list_begin(&uhci->control_fs);
        e != list_end(&uhci->control_fs);
        e = list_next(e)) {
      p = list_entry(e, struct usb_pipe, pipels_elem);
      p->queue->qh_hlink = UHCI_PTR_T;
      qh->qh_hlink = vtop_uhci(p->queue) | UHCI_PTR_QH;
      qh = p->queue;
    }
  }

  if (!list_empty(&uhci->bulk)) {
    if (!first_fast) {
      struct usb_pipe *pipe;
      pipe = list_entry(list_head(&uhci->bulk), struct usb_pipe, pipels_elem);
      first_fast = pipe->queue;
    }

    for (e = list_begin(&uhci->bulk);
        e != list_end(&uhci->bulk);
        e = list_next(e)) {
      p = list_entry(e, struct usb_pipe, pipels_elem);
      p->queue->qh_hlink = UHCI_PTR_T;
      qh->qh_hlink = vtop_uhci(p->queue) | UHCI_PTR_QH;
      qh = p->queue;
    }
  }
  /* If the first fast node exists, make a loop of FullSpeed control and bulk
   * queue headers, in order to reclaim the bandwidth (since all bulk QH's are
   * executed by breadth first). */
#ifdef ENABLE_RECLAIM_BANDWIDTH
  if (first_fast) {
    qh->qh_hlink = vtop_uhci(first_fast) | UHCI_PTR_QH;
  } else {
    qh->qh_hlink = UHCI_PTR_T;
  }
#else
  qh->qh_hlink = UHCI_PTR_T;
#endif
}

static void
uhci_insert_interrupt(struct uhci_data *uhci, struct usb_pipe *pipe)
{
  NOT_IMPLEMENTED();
#if 0
  struct uhci_queue_hdr **q;
  uint8_t count, i, minimum, bestat;

  DBGn(USB, "[UHCI] Inserting Interrupt queue %p with period %d\n", pipe,
      pipe->interval);

  if (pipe->interval < 2) {
    q = &uhci->qh01;
    count = 1;
  } else if (pipe->interval < 4) {
    q = uhci->qh02;
    count = 2;
  } else if (pipe->interval < 8) {
    q = uhci->qh04;
    count = 4;
  } else if (pipe->interval < 16) {
    q = uhci->qh08;
    count = 8;
  } else if (pipe->interval < 32) {
    q = uhci->qh16;
    count = 16;
  } else {
    q = uhci->qh32;
    count = 32;
  }

  i = 0;
  minimum = q[0]->qh_data2;
  bestat = 0;
  do {
    if (minimum > q[i]->qh_data2) {
      minimum = q[i]->qh_data2;
      bestat = i;
    }
  } while (++i < count);

  DBGn(USB, "[UHCI] Best node (%d uses) %d from %d\n", minimum, bestat, count);

  pipe->qhdr_node = bestat;
  pipe->qhdr_location = count;

  q[bestat]->qh_data2++;

  pipe->queue->qh_vlink = q[bestat]->qh_vlink;
  pipe->queue->qh_hlink = q[bestat]->qh_hlink;
  q[bestat]->qh_vlink = vtop_uhci(pipe->queue) | UHCI_PTR_QH;
#endif
}

struct usb_pipe *
uhci_create_pipe(struct uhci_data *uhci, enum usb_pipetype type,
    bool fullspeed, uint8_t addr, uint8_t endp, uint8_t period, uint32_t maxp,
    uint32_t timeout)
{
  struct usb_pipe *pipe;

  pipe = malloc(sizeof(struct usb_pipe));
  ASSERT(pipe);


  if (pipe) {
    pipe->queue = uhci_alloc_qhdr(uhci);

    pipe->type = type;
    pipe->fullspeed = fullspeed;
    pipe->next_toggle = 0;
    pipe->dev_addr = addr;
    pipe->endpoint = endp;
    pipe->max_transfer = maxp;
    pipe->interval = period;
    pipe->first_td = (void *)UHCI_PTR_T;
    pipe->last_td = (void *)UHCI_PTR_T;
    pipe->queue->qh_vlink = UHCI_PTR_T;
    pipe->queue->qh_data1 = (uint32_t)pipe;
    pipe->queue->qh_data2 = 0;

    list_init(&pipe->interrupts);
    pipe->timeout_val = timeout;

    cond_init(&pipe->completion_wait);
    lock_init(&pipe->mutex);
    DBGn(PIPE, "pipe->queue=%p\n", pipe->queue);

    /* According to the pipe's type, add it to proper list */
    switch (type) {
      case PIPE_BULK:
        DBGn(USB, "%s() %d: Adding pipe %p to bulk list\n", __func__,
            __LINE__, pipe);
        list_push_back(&uhci->bulk, &pipe->pipels_elem);
        break;

      case PIPE_CONTROL:
        if (fullspeed) {
          list_push_back(&uhci->control_fs, &pipe->pipels_elem);
        } else {
          list_push_back(&uhci->control_ls, &pipe->pipels_elem);
        }
        break;

      case PIPE_ISOCHRONOUS:
        list_push_back(&uhci->isochronous, &pipe->pipels_elem);
        break;

      case PIPE_INTERRUPT:
        uhci_insert_interrupt(uhci, pipe);
        list_push_back(&uhci->interrupts, &pipe->pipels_elem);
        break;
    }

    uhci_rebuild_list(uhci);
  }

  DBGn(USB, "%s(%d, %d, %d, %d, %d) returning %p\n", __func__, type, fullspeed,
      addr, endp, maxp, pipe);
  return pipe;
}

void
uhci_delete_pipe(struct uhci_data *uhci, struct usb_pipe *pipe)
{
  DBGn(USB, "[UHCI] DeletePipe(%p)\n", pipe);

  if (pipe) {
    struct uhci_transfer_desc *td;
    enum intr_level old_level;

    lock_acquire(&pipe->mutex);
    td = pipe->first_td;
    old_level = intr_disable();

    /* Remove the pipe from transfer list and rebuild the lists */
    list_remove(&pipe->pipels_elem);
    uhci_rebuild_list(uhci);
    intr_set_level(old_level);

    /* FIXME: Don't delay. Just wait for the incomplete TD's */
    //Delay(1);

    /* At this stage the transfer nodes may be safely deleted from the queue */
    while ((uint32_t)td != UHCI_PTR_T) {
      struct uhci_transfer_desc *tnext;
      
      tnext = ptov_uhci(td->td_link_ptr & 0xfffffff1);
      uhci_free_td(uhci, td);
      td = tnext;
    }
    lock_release(&pipe->mutex);
    free(pipe);
  }
}

void
uhci_queued_transfer(struct uhci_data *uhci, struct usb_pipe *pipe,
    void *buffer, uint32_t length, bool in)
{
  struct uhci_transfer_desc *td = NULL, *t = NULL;
  enum intr_level old_level;
  uint8_t dt, *ptr;
  uint32_t ntd;

  ptr = (uint8_t*)buffer;
  dt = pipe->next_toggle;
  ntd = (length + pipe->max_transfer - 1) / pipe->max_transfer;

  if (ntd % 2 == 0) {
    pipe->next_toggle = dt;
  } else {
    pipe->next_toggle = dt ^ 1;
  }

  while (ntd--) {
    uint32_t len;

    len = (length > pipe->max_transfer) ? pipe->max_transfer : length;
    if (td) {
      struct uhci_transfer_desc *tmp;
      tmp = uhci_alloc_td(uhci);
      ASSERT(tmp);
      td->td_link_ptr = vtop_uhci(tmp) | UHCI_PTR_TD;
      td = tmp;
    } else {
      td = t = uhci_alloc_td(uhci);
      ASSERT(td);
    }

    td->td_buffer = vtop_uhci(ptr);
    td->td_link_ptr = UHCI_PTR_T;
    td->td_status = UHCI_TD_ZERO_ACTLEN(UHCI_TD_SET_ERRCNT(3) | UHCI_TD_ACTIVE);
    if (in) {
      td->td_token = UHCI_TD_IN(len, pipe->endpoint, pipe->dev_addr, dt);
    } else {
      td->td_token = UHCI_TD_OUT(len, pipe->endpoint, pipe->dev_addr, dt);
    }
    DBGn(USB, "in=%d, len=%d, endpoint=%#x, dev_addr=%#x, dt=%#hhx\n", in, len,
        pipe->endpoint, pipe->dev_addr, dt);

    if (pipe->fullspeed) {
      td->td_status |= UHCI_TD_SPD;
    } else {
      td->td_status |= UHCI_TD_LS | UHCI_TD_SPD;
    }

    dt ^= 1;
    ptr += len;

    DBGn(PIPE, "[UHCI]     TD=%p (%08x %08x %08x %08x)\n", td,
          td->td_link_ptr, td->td_status, td->td_token, td->td_buffer);
  }

  td->td_status |= UHCI_TD_IOC;

  old_level = intr_disable();
  if (pipe->first_td == (void *)UHCI_PTR_T) {
    pipe->first_td = t;
    pipe->last_td = td;
  } else {
    pipe->last_td->td_link_ptr = vtop_uhci(t) | UHCI_PTR_TD;
    pipe->last_td = td;
  }
  DBGn(PIPE, "[UHCI]    first_td=%p, last_td=%p\n", pipe->first_td, pipe->last_td);

  if (pipe->queue->qh_vlink == UHCI_PTR_T) {
    pipe->queue->qh_vlink = vtop_uhci(t) | UHCI_PTR_TD;
  }
  DBGn(PIPE, "[UHCI]   pipe->queue=%p, pipe->queue->qh_vlink=0x%x\n",
      pipe->queue, pipe->queue->qh_vlink);
  intr_set_level(old_level);
}

void
uhci_queued_write(struct uhci_data *uhci, struct usb_pipe *pipe, void *buffer,
    uint32_t length)
{
  uhci_queued_transfer(uhci, pipe, buffer, length, false);
}

void
uhci_queued_read(struct uhci_data *uhci, struct usb_pipe *pipe, void *buffer,
    uint32_t length)
{
  uhci_queued_transfer(uhci, pipe, buffer, length, true);
}

static void
__uhci_control_transfer(struct uhci_data *uhci, struct usb_pipe *pipe,
    struct usbdevice_request *request, void *buffer, uint32_t length)
{
  struct uhci_transfer_desc *req, *td = NULL, *stat;
  enum intr_level old_level;
  bool isread;
  uint32_t len;

  isread = request->bmRequestType & UT_READ;
  len = request->wLength;

  DBGn(USB, "%s(): %p(%02x %02x %04x %04x %04x) %p %d\n", __func__,
        request, request->bmRequestType, request->bRequest, request->wValue,
        request->wIndex, request->wLength, buffer, length);
  ASSERT(length <= 1023);

  if (length <= 1023) {
    req = uhci_alloc_td(uhci);
    DBGn(PIPE, "%s(): req=%p\n", __func__, req);
    ASSERT(req);
    req->td_token = UHCI_TD_SETUP(sizeof(usbdevice_request), pipe->endpoint,
        pipe->dev_addr);
    req->td_buffer = vtop_uhci(request);
    req->td_status = UHCI_TD_ZERO_ACTLEN(UHCI_TD_SET_ERRCNT(3)| UHCI_TD_ACTIVE);

    if (!pipe->fullspeed) {
      req->td_status |= UHCI_TD_LS;
    }

    stat = uhci_alloc_td(uhci);
    ASSERT(stat);
    stat->td_link_ptr = UHCI_PTR_T;
    stat->td_status = UHCI_TD_ZERO_ACTLEN(UHCI_TD_SET_ERRCNT(3) |
        UHCI_TD_ACTIVE | UHCI_TD_IOC);
    stat->td_buffer = 0;
    stat->td_token = isread ? UHCI_TD_OUT(0, pipe->endpoint, pipe->dev_addr, 1)
      : UHCI_TD_IN (0, pipe->endpoint, pipe->dev_addr, 1);

    if (!pipe->fullspeed) {
      stat->td_status |= UHCI_TD_LS;
    }

    if (buffer) {
      struct uhci_transfer_desc *prev;
      uint8_t d, *buff;

      d = 1;
      prev = req;
      buff = buffer;
      length = len;

      while (length > 0) {
        len = (length > pipe->max_transfer) ? pipe->max_transfer : length;
        DBGn(USB, "length=%d, len=%d, max_transfer=%d\n", length, len,
            pipe->max_transfer);
        td = uhci_alloc_td(uhci);
        ASSERT(td);
        td->td_token = isread ? UHCI_TD_IN (len, pipe->endpoint, pipe->dev_addr,
                                            d)
          : UHCI_TD_OUT(len, pipe->endpoint, pipe->dev_addr, d);
        td->td_status = UHCI_TD_ZERO_ACTLEN(UHCI_TD_SET_ERRCNT(3)
            | UHCI_TD_ACTIVE  | UHCI_TD_SPD);
        td->td_buffer = vtop_uhci(buff);

        if (!pipe->fullspeed) {
          td->td_status |= UHCI_TD_LS;
        }
        td->td_status |= UHCI_TD_SPD;
        prev->td_link_ptr = vtop_uhci(td) | UHCI_PTR_TD | UHCI_PTR_VF;

        d ^= 1;
        prev = td;
        buff += len;
        length -= len;
      }
      td->td_link_ptr = vtop_uhci(stat) | UHCI_PTR_TD | UHCI_PTR_VF;
    } else {
      req->td_link_ptr = vtop_uhci(stat) | UHCI_PTR_TD | UHCI_PTR_VF;
    }

    DBE(PIPE, {
        struct uhci_transfer_desc *t;
        
        t = req;
        while ((uint32_t)t != UHCI_PTR_T) {
          DBGn(PIPE, "[UHCI] td=%p (%08x %08x %08x %08x)\n", t,
            t->td_link_ptr, t->td_status, t->td_token, t->td_buffer);
          t = ptov_uhci(t->td_link_ptr & 0xfffffff1);
        }
        });

    old_level = intr_disable();
    if (pipe->first_td == (void *)UHCI_PTR_T) {
      pipe->first_td = req;
      pipe->last_td = stat;
    } else {
      pipe->last_td->td_link_ptr = vtop_uhci(req) | UHCI_PTR_TD;
      pipe->last_td = stat;
    }

    if (pipe->queue->qh_vlink == UHCI_PTR_T) {
      pipe->queue->qh_vlink = vtop_uhci(req) | UHCI_PTR_TD;
      DBGn(PIPE, "pipe->queue[%p]->qh_vlink=%x\n", pipe->queue,
          vtop_uhci(req) | UHCI_PTR_TD);
    }
    intr_set_level(old_level);
  }
}

void
uhci_pipe_set_timeout(struct uhci_data *uhci UNUSED,struct usb_pipe *pipe,
		int64_t nanoseconds)
{
  pipe->timeout_val = nanoseconds;
}

static void
handle_timeout(struct uhci_data *uhci, struct usb_pipe *pipe)
{
  struct uhci_transfer_desc *td;
  pipe->queue->qh_vlink = UHCI_PTR_T;
  td = pipe->first_td;

  while ((uint32_t)td != UHCI_PTR_T) {
    struct uhci_transfer_desc *tnext;
    DBGn(PIPE, "[UHCI] td= %p (%08x %08x %08x %08x)\n", td, td->td_link_ptr,
        td->td_status, td->td_token, td->td_buffer);
    tnext = ptov_uhci(td->td_link_ptr & 0xfffffff1);
    uhci_free_td(uhci, td);
    td = tnext;
    pipe->first_td = tnext;
  }
  pipe->last_td = (void *)UHCI_PTR_T;
}

bool
uhci_control_transfer(struct uhci_data *uhci, struct usb_pipe *pipe,
    struct usbdevice_request *request, void *buffer, uint32_t length)
{
  enum intr_level old_level;

  ASSERT(!intr_context());
  old_level = intr_disable();
  pipe->completed = 0;
  DBGn(TIMEOUT, "%s() %d:\n", __func__, __LINE__);
  __uhci_control_transfer(uhci, pipe, request, buffer, length);
  if (!pipe->completed) {
    bool timeout;
    DBGn(TIMEOUT, "%s() %d: calling cond_timed_wait_intr(%#llx)\n", __func__,
        __LINE__, pipe->timeout_val);
    timeout = cond_timed_wait_intr (&pipe->completion_wait, pipe->timeout_val);
    if (timeout) {
      DBGn(TIMEOUT, "%s() cond_timed_wait_intr timed out.\n", __func__);
      handle_timeout(uhci, pipe);
      intr_set_level(old_level);
      return false;
    } else {
      DBGn(TIMEOUT, "%s() success.\n", __func__);
      ASSERT(pipe->completed);
    }
  }
  intr_set_level(old_level);
  return true;
}

bool
uhci_bulk_transfer(struct uhci_data *uhci, struct usb_pipe *pipe,
    void *buffer, uint32_t length)
{
  enum intr_level old_level;
  ASSERT(!intr_context());

  old_level = intr_disable();
  pipe->completed = 0;
  hack_buffer = buffer;
  hack_len = length;
  uhci_queued_transfer(uhci, pipe, buffer, length,
      (pipe->endpoint & 0x80)?true:false);
  if (!pipe->completed) {
    bool timeout;
    DBGn(TIMEOUT, "%s() %d: calling cond_timed_wait_intr(%#llx)\n", __func__,
        __LINE__, pipe->timeout_val);
    timeout = cond_timed_wait_intr (&pipe->completion_wait, pipe->timeout_val);
    if (timeout) {
      DBGn(TIMEOUT, "%s() cond_timed_wait_intr timed out.\n", __func__);
      handle_timeout(uhci, pipe);
      intr_set_level(old_level);
      return false;
    } else {
      DBGn(TIMEOUT, "%s() success.\n", __func__);
      ASSERT(pipe->completed);
    }
  }
  intr_set_level(old_level);
  DBGn(USB, "%s() returning true.\n", __func__);
  return true;
}

bool
uhci_port_reset(struct uhci_data *uhci, uint8_t p)
{
  int lim, port, x;
  struct usbdevice *rh_dev;
  bool fast;

  if (p == 1) {
    port = UHCI_PORTSC1;
  } else if (p == 2) {
    port = UHCI_PORTSC2;
  } else {
    return false;
  }

  x = URWMASK(inw(uhci->iobase + port));
  outw(uhci->iobase + port, x | UHCI_PORTSC_PR);

  timer_msleep(100);

  x = URWMASK(inw(uhci->iobase + port));
  outw(uhci->iobase + port, x & ~UHCI_PORTSC_PR);

  timer_msleep(100);

  x = URWMASK(inw(uhci->iobase + port));
  outw(uhci->iobase + port, x | UHCI_PORTSC_PE);

  for (lim = 10; --lim > 0;) {
    timer_msleep(10);
    x = inw(uhci->iobase + port);
    if (!(x & UHCI_PORTSC_CCS)) {
      break;
    }
    if (x & (UHCI_PORTSC_POEDC | UHCI_PORTSC_CSC)) {
      outw(uhci->iobase + port,
          URWMASK(x) | (x & (UHCI_PORTSC_POEDC | UHCI_PORTSC_CSC)));
      continue;
    }
    if (x & UHCI_PORTSC_PE) {
      break;
    }
    outw(uhci->iobase + port, URWMASK(x) | UHCI_PORTSC_PE);
  }

  if (lim <= 0) {
    DBGn(USB, "[UHCI] Port reset timeout\n");
    return false;
  }
  uhci->reset |= 1 << (p-1);
  timer_msleep(100);
  fast = (inw(uhci->iobase + port) & UHCI_PORTSC_LSDA)?false:true;
  if (rh_dev = usb_new_device(uhci, NULL, fast)) {
    MSG ("%s(): Found device '%s' at port %d [%s].\n", __func__,
        rh_dev->product_name, p - 1, fast?"fast":"slow");
  } else {
    MSG ("%s(): No device found at port %d.\n", __func__, p - 1);
  }

  return true;
}


bool
uhci_get_port_status(struct uhci_data *uhci, uint16_t p, uint16_t *status,
    uint16_t *change)
{
  uint16_t port, x;
  bool retval = false;

  *change = 0;
  *status = 0;

  if (p == 1) {
    port = UHCI_PORTSC1;
  } else if (p == 2) {
    port = UHCI_PORTSC2;
  } else {
    return false;
  }

  x = inw(uhci->iobase + port);

  if (x & UHCI_PORTSC_CCS) {
    *status |= UPS_CURRENT_CONNECT_STATUS;
  }
  if (x & UHCI_PORTSC_CSC) {
    *change |= UPS_C_CONNECT_STATUS;
  }
  if (x & UHCI_PORTSC_PE) {
    *status |= UPS_PORT_ENABLED;
  }
  if (x & UHCI_PORTSC_POEDC) {
    *change |= UPS_C_PORT_ENABLED;
  }
  if (x & UHCI_PORTSC_OCI) {
    *status |= UPS_OVERCURRENT_INDICATOR;
  }
  if (x & UHCI_PORTSC_OCIC) {
    *change |= UPS_C_OVERCURRENT_INDICATOR;
  }
  if (x & UHCI_PORTSC_SUSP) {
    *status |= UPS_SUSPEND;
  }
  if (x & UHCI_PORTSC_LSDA) {
    *status |= UPS_LOW_SPEED;
  }

  *status |= UPS_PORT_POWER;

  if (uhci->reset & (1 << (p - 1))) {
    *status |= UPS_C_PORT_RESET;
  }

  return true;
}

bool
uhci_clear_port_feature(struct uhci_data *uhci, uint16_t p, int feature)
{
  uint16_t port, x;
  bool retval = false;

  if (p == 1) {
    port = UHCI_PORTSC1;
  } else if (p == 2) {
    port = UHCI_PORTSC2;
  } else {
    return false;
  }

  switch(feature) {
    case UHF_PORT_ENABLE:
      x = URWMASK(inw(uhci->iobase + port));
      outw(uhci->iobase + port, x & ~UHCI_PORTSC_PE);
      retval = true;
      break;

    case UHF_PORT_SUSPEND:
      x = URWMASK(inw(uhci->iobase + port));
      outw(uhci->iobase + port, x & ~UHCI_PORTSC_SUSP);
      retval = true;
      break;

    case UHF_PORT_RESET:
      x = URWMASK(inw(uhci->iobase + port));
      outw(uhci->iobase + port, x & ~UHCI_PORTSC_PR);
      retval = true;
      break;

    case UHF_C_PORT_CONNECTION:
      x = URWMASK(inw(uhci->iobase + port));
      outw(uhci->iobase + port, x | UHCI_PORTSC_CSC);
      retval = true;
      break;

    case UHF_C_PORT_ENABLE:
      x = URWMASK(inw(uhci->iobase + port));
      outw(uhci->iobase + port, x | UHCI_PORTSC_POEDC);
      retval = true;
      break;

    case UHF_C_PORT_OVER_CURRENT:
      x = URWMASK(inw(uhci->iobase + port));
      outw(uhci->iobase + port, x | UHCI_PORTSC_OCIC);
      retval = true;
      break;

    case UHF_C_PORT_RESET:
      uhci->reset &= ~(1 << (p - 1));
      retval = true;
      break;

    default:
      retval = false;
      break;
  }

  return retval;
}

bool
uhci_add_interrupt(struct uhci_data *uhci, struct usb_pipe *pipe, void *buffer,
    size_t length, struct interrupt_data *interrupt)
{
  bool retval = false;
  struct usb_pipe *p = pipe;

  if (pipe == (void *)0xbaadf00d) {
    DBGn(USB, "[UHCI] add_interrupt local for the UHCI. intr %p, list %p\n",
        interrupt, &uhci->intr_list);

    if (!uhci->iobase) {
      uhci->tmp = interrupt;
    } else {
      list_push_back(&uhci->intr_list, &interrupt->ls_elem);
    }
    retval = true;
  } else {
    struct uhci_interrupt *intr;
    DBGn(USB, "uhci_add interrupt()\n");
    intr = malloc(sizeof(struct interrupt_data));
    ASSERT(intr);
    DBGn(USB, "[uhci_add_interrupt] intr = %p\n", intr);
    if (intr) {
      intr->i_intr = interrupt;
      uhci_queued_read(uhci, p, buffer, length);
      list_push_back(&p->interrupts, &intr->ls_elem);
    }
  }
  DBGn(USB, "[uhci_add_interrupt] %s\n", retval?"success":"failure");
  return retval;
}

bool
uhci_remove_interrupt(struct uhci_data *uhci, struct usb_pipe *pipe,
    struct interrupt_data *interrupt)
{
  struct uhci_interrupt *intr = NULL;
  struct usb_pipe *p;

  p = pipe;;
  if (p == (struct usb_pipe *)0xbaadf00d) {
    list_remove(&interrupt->ls_elem);
    return true;
  } else {
    struct list_elem *e;
    for (e = list_begin(&p->interrupts);
        e != list_end(&p->interrupts);
        e = list_next(e)) {
      intr = list_entry(e, struct uhci_interrupt, ls_elem);
      if (intr->i_intr == interrupt) {
        break;
      }
    }
    if (intr) {
      enum intr_level old_level;
      old_level = intr_disable();
      list_remove(&intr->ls_elem);
      intr_set_level(old_level);

      uhci_free_td(uhci, intr->i_td);
      free(intr);
      return true;
    }
    return false;
  }
}

bool
uhci_set_port_feature(struct uhci_data *uhci, uint16_t p, int feature)
{
  uint16_t port, x;
  bool retval = false;

  if (p == 1) {
    port = UHCI_PORTSC1;
  } else if (p == 2) {
    port = UHCI_PORTSC2;
  } else {
    return false;
  }

  switch(feature)
  {
    case UHF_PORT_ENABLE:
      x = URWMASK(inw(uhci->iobase + port));
      outw(uhci->iobase + port, x | UHCI_PORTSC_PE);
      retval = true;
      break;

    case UHF_PORT_SUSPEND:
      x = URWMASK(inw(uhci->iobase + port));
      outw(uhci->iobase + port, x | UHCI_PORTSC_SUSP);
      retval = true;
      break;

    case UHF_PORT_RESET:
      retval = uhci_port_reset(uhci, p);
      break;

    case UHF_PORT_POWER:
      retval = true;
      break;

    default:
      retval = false;
      break;
  }

  return retval;
}

/*
void
uhci_usbhub_onoff(struct uhci_data *uhci, bool run)
{
  uhci->running = run;
  uhci_run(uhci, run);
  timer_msleep(100);
}
*/

struct usb_pipe *
uhci_usbdevice_create_pipe(struct usbdevice *dev, enum usb_pipetype type,
    uint8_t endpoint, uint8_t period, uint32_t maxpacket, uint32_t timeout)
{
  if (type == PIPE_INTERRUPT && endpoint == 0x81) {
    return (void *)0xbaadf00d;
  } else {
    return NULL;
  }
}

void
uhci_shutdown(void)
{
  struct list_elem *e;

  for (e = list_begin(&uhci_list); e != list_end(&uhci_list);) {
    struct uhci_data *uhci;
    uhci = list_entry(e, struct uhci_data, ls_elem);
    uhci_card_shutdown(uhci);
    e = list_next(e);
    free(uhci);
  }
  usb_devlist_init();
}

void
usb_shutdown(void)
{
  uhci_shutdown();
}


