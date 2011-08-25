#ifndef DEVICES_USBMSD_H
#define DEVICES_USBMSD_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <list.h>
#include "devices/usb/usb.h"

typedef struct __attribute__((packed)) {
  uint32_t  dCBWSignature;
  uint32_t  dCBWTag;
  uint32_t  dCBWDataTransferLength;
  uint8_t   bmCBWFlags;
  uint8_t   bCBWLUN;
  uint8_t   bCBWCBLength;
  uint8_t   CBWCB[16];
} cbw_t;

#define CBW_SIGNATURE 0x43425355
#define CBW_FLAGS_IN  0x80
#define CBW_FLAGS_OUT 0x00

typedef struct __attribute__((packed)) {
  uint32_t  dCSWSignature;
  uint32_t  dCSWTag;
  uint32_t  dCSWDataResidue;
  uint8_t   bCSWStatus;
} csw_t;

#define CSW_SIGNATURE   0x53425355
#define CSW_STATUS_OK   0x00
#define CSW_STATUS_FAIL   0x01
#define CSW_STATUS_PHASE  0x02

struct usbmsd;
struct usbdevice;

void usbmsd_free(struct usbmsd *);
void usbmsd_reset(struct usbmsd *);

uint8_t usbmsd_get_max_lun(struct usbmsd *);
bool usbmsd_direct_scsi(struct usbmsd *, cbw_t const *, uint8_t *data);
bool usbmsd_test_unit_ready(struct usbmsd *, uint8_t lun);
bool usbmsd_inquiry(struct usbmsd *, uint8_t lun, void *buf, size_t buflen);
bool usbmsd_request_sense(struct usbmsd *, uint8_t lun, void *buf,
    size_t buflen);
bool usbmsd_read(struct usbmsd *, uint8_t, void *, uint32_t, uint16_t);
bool usbmsd_write(struct usbmsd *, uint8_t, const void *, uint32_t, uint16_t);
bool usbmsd_read_capacity(struct usbmsd *, uint8_t lun, uint32_t *block_total,
    uint32_t *block_size);
bool usbmsd_match(usb_device_descriptor_t *, usb_config_descriptor_t *, int);
struct usbmsd *usbmsd_new(struct usbdevice *device);
void usbmsd_reset(struct usbmsd *mss);
void usbmsd_init(void);
struct usbmsd *usbdev_is_msd(struct usbdevice *dev);
bool usbmsd_is_bootdisk(struct usbmsd *msd);
void usbmsd_free(struct usbmsd *mss);
char const *usbmsd_name(struct usbmsd const *mss);

typedef struct mss_unit_t {
  //struct Unit   msu_unit;
  struct list_elem ul_elem;
  uint32_t    msu_unit_num;
  uint32_t    msu_block_size;
  uint32_t    msu_block_count;
  uint32_t    msu_change_num;
  uint8_t     msu_block_shift;
  uint8_t     msu_flags;
  uint8_t     msu_lun;
  struct usbmsd *msu_object;
  void        *msu_remove_int;        /* XXX */
  //struct Interrupt *msu_removeInt;
  //struct List   msu_diskChangeList;
  //struct Task   *msu_handler;
  uint8_t     msu_inquiry[8];
} mss_unit_t;

typedef struct unit_cache_t {
  //struct MinNode  node;
  struct list units;
  struct list_elem uc_elem;

  uint16_t    product_id;
  uint16_t    vendor_id;
  char        *product_name;
  char        *manufacturer_name;
  char        *serial_number;

  uint32_t    unit_number;
} unit_cache_t;

typedef struct usbmsd {
  struct lock                 lock;
  struct usbdevice            *device;

  unit_cache_t                *cache;
  usb_config_descriptor_t     *cdesc;
  usb_device_descriptor_t     ddesc;
  usb_interface_descriptor_t  *iface;

  usb_endpoint_descriptor_t   *ep_in;
  usb_endpoint_descriptor_t   *ep_out;

  void                        *pipe_in; 
  void                        *pipe_out;

  void                        *handler[16];   //XXX
  mss_unit_t                  *unit[16];

  uint32_t          blocksize[16];
  uint8_t           max_lun;
  uint32_t          unit_num;
} usbmsd;

#define MSF_DISK_CHANGED   1
#define MSF_DISK_PRESENT   2
#define MSF_DEVICE_REMOVED 4

extern struct list unit_list;

#endif  /* devices/usbmsd.h */
