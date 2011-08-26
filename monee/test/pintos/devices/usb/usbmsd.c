#include "devices/usb/usbmsd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include "devices/block.h"
#include "devices/partition.h"
#include "devices/usb/usb.h"
#include "devices/timer.h"
#include "devices/usb/usbdevice.h"
#include "threads/malloc.h"
#include "threads/interrupt.h"

#define USB_ENDPOINT_NUMBER_MASK 0x0f
#define USB_ENDPOINT_DIR_MASK    0x80

#define USB 2

static uint32_t tid = 0;
struct list unit_list;
static struct list unit_cache;
static uint32_t unit_num = 0;
static struct lock lock;

static struct block_operations msd_operations;

void
usbmsd_init(void)
{
  list_init(&unit_list);
  list_init(&unit_cache);
  lock_init(&lock);
}

static uint32_t
get_tid(void)
{
  enum intr_level old_level;
  uint32_t id;
  old_level = intr_disable();
  id = tid++;
  intr_set_level(old_level);
  return id;
}

bool usbmss_add_volume(mss_unit_t *unit);

static unit_cache_t *
get_unit_from_cache(struct usbmsd *mss, uint8_t create)
{
  unit_cache_t *unit, *u = NULL;
  //intptr_t tmp;
  uint16_t pid, vid;
  char const *product, *manufacturer, *serial;
  struct list_elem *e;

  /* Get some basic info about the device */
  pid = mss->device->descriptor.idProduct;
  vid = mss->device->descriptor.idVendor;
  product = mss->device->product_name;
  manufacturer = mss->device->manufacturer_name;
  serial = mss->device->serialnumber_name;

  DBGn(USB, "[MSS] %s(%04x:%04x, '%s', '%s', '%s')\n", __func__, pid,
      vid, manufacturer, product, serial);

  /* Check the cache list. If unit was here already, use it */
  lock_acquire(&lock);
  for (e = list_begin(&unit_cache); e != list_end(&unit_cache);e=list_next(e)) {
    unit = list_entry(e, unit_cache_t, uc_elem);
    if (pid == unit->product_id && vid == unit->vendor_id) {
      if (   strcmp(serial, unit->serial_number) == 0
          && strcmp(product, unit->product_name) == 0
          && strcmp(manufacturer, unit->manufacturer_name) == 0) {
        u = unit;
        break;
      }
    }
  }
  lock_release(&lock);

  if (u) {
    DBGn(USB, "[MSS] Found the device in cache already. Reassigning the old "
        "unit number %d\n", u->unit_number);
  } else if (create) {
    DBGn(USB, "[MSS] Unit is new to the system. Creating new cache object\n");

    u = malloc(sizeof(unit_cache_t));
    ASSERT(u);
    list_init(&u->units);
    u->product_id = pid;
    u->vendor_id = vid;

    if (product) {
      size_t psize = strlen(product) + 1;
      u->product_name = malloc(psize);
      if (u->product_name) {
        strlcpy(u->product_name, product, psize);
      }
    } else {
      u->product_name = NULL;
    }

    if (manufacturer) {
      size_t msize = strlen(manufacturer) + 1;
      u->manufacturer_name = malloc(msize);
      if (u->manufacturer_name) {
        strlcpy(u->manufacturer_name, manufacturer, msize);
      }
    } else {
      u->manufacturer_name = NULL;
    }

    if (serial) {
      size_t ssize = strlen(serial) + 1;
      u->serial_number = malloc(ssize);
      if (u->serial_number) {
        strlcpy(u->serial_number, serial, ssize);
      }
    } else {
      u->serial_number = NULL;
    }

    lock_acquire(&lock);
    u->unit_number = unit_num;
    unit_num += mss->max_lun + 1;
    list_push_back(&unit_cache, &u->uc_elem);
    lock_release(&lock);
  }

  return u;
}

/* Initialize the USBStorage instance. */
struct usbmsd *
usbmsd_new(struct usbdevice *device)
{
  struct usbmsd *mss;
  usb_config_descriptor_t cdesc;
	static int dev_no = 0;
	struct block *block;
	uint32_t block_total;
  intptr_t iface;
	char name[16];
  int i;

  DBGn(USB, "%s()\n", __func__);
  mss = malloc(sizeof(struct usbmsd));
  ASSERT(mss);
  mss->device = device;
  mss->pipe_in = mss->pipe_out = NULL;

  usbdevice_configure(device, 0);
  lock_init(&mss->lock);

  /* Get the interface number */
  iface = mss->device->iface;

  DBGn(USB, "%s() %d: device->interfaces=%p, "
      "device->interfaces[0].classdev=%p\n", __func__, __LINE__,
      device->interfaces, device->interfaces[0].classdev);

  /* Get device and config descriptors */
  DBGn(USB, "[MSS] Getting device descriptor\n");
  usbdevice_get_device_descriptor(mss->device, &mss->ddesc);
  DBGn(USB, "[MSS] Getting initial config descriptor\n");
  usbdevice_get_config_descriptor(mss->device, 0, &cdesc);
  /* How many LUNs this device supports? */
  DBGn(USB, "[MSS] Getting max lun\n");
#if 0
  prints("Calling get_max_lun\n");
  mss->max_lun = usbmsd_get_max_lun(mss);
  prints("Returned from get_max_lun\n");
  if (mss->max_lun > 15) {
    DBGn(USB, "[MSS] GetMaxLUN FAILED.\n");
    mss->max_lun = 0;
  }
#endif
  mss->max_lun = 0;
  DBGn(USB, "[MSS] GetMaxLUN returns %d\n", mss->max_lun);

  if (cdesc.wTotalLength) {
    mss->cdesc = malloc(cdesc.wTotalLength);
    ASSERT(mss->cdesc);
  }

  if (mss->cdesc) {
    DBGn(USB, "[MSS] Getting config descriptor of size %d\n",
        cdesc.wTotalLength);
    usbdevice_get_descriptor(mss->device, UDESC_CONFIG, 0,
        cdesc.wTotalLength, mss->cdesc);

    DBGn(USB, "[MSS] Getting descriptor of interface %d\n", iface);
    mss->iface = usbdevice_get_interface(mss->device, iface);

    DBGn(USB, "[MSS] Interface has %d endpoints\n", mss->iface->bNumEndpoints);

    /* Iterate through the possible endpoints and look for IN and OUT
     * endpoints for BULK transfers. */
    for (i = 0; i < mss->iface->bNumEndpoints; i++) {
      usb_endpoint_descriptor_t *ep;
      ep = usbdevice_get_endpoint(mss->device, iface, i);

      DBGn(USB, "[MSS] endpoint %d: addr %02x, interval %02x, length %02x "
          "attr %02x maxpacket %04x\n", i, ep->bEndpointAddress,
          ep->bInterval, ep->bLength, ep->bmAttributes,
          ep->wMaxPacketSize);

      /* Use only BULK endpoints */
      if (UE_GET_XFERTYPE(ep->bmAttributes) == UE_BULK) {
        if (!mss->pipe_in && UE_GET_DIR(ep->bEndpointAddress) == UE_DIR_IN) {
          DBGn(USB, "[MSS] IN endpoint found 0x%x\n", ep->bEndpointAddress);
          mss->ep_in = ep;
          mss->pipe_in = usbdevice_create_pipe(mss->device, PIPE_BULK,
              ep->bEndpointAddress, 0, ep->wMaxPacketSize, 10000);
        } else if (   !mss->pipe_out
            && UE_GET_DIR(ep->bEndpointAddress) == UE_DIR_OUT) {
          DBGn(USB, "[MSS] OUT endpoint found 0x%x\n", ep->bEndpointAddress);
          mss->ep_out = ep;
          mss->pipe_out = usbdevice_create_pipe(mss->device, PIPE_BULK,
              ep->bEndpointAddress, 0, ep->wMaxPacketSize, 10000);
        }
      }
    }

    if (!mss->pipe_in || !mss->pipe_out) {
      ABORT();
    }
    //struct Task *t;
    //struct MemList *ml;

    /* Pipes are there. Let's start the handler task */
    usbmsd_reset(mss);
    /* Get the unit from cache, in case it was used before */
    mss->cache = get_unit_from_cache(mss, 0);

    /* Reuse the unit, i.e. do not create the tasks, just signal them and
     * update unit structures. */
    if (mss->cache) {
      mss->unit_num = mss->cache->unit_number;

      for (i = 0; i <= mss->max_lun; i++) {
        if (!list_empty(&mss->cache->units)) {
          struct list_elem *elem;
          elem = list_pop_front(&mss->cache->units);
          mss->unit[i] = list_entry(elem, struct mss_unit_t, ul_elem);
        } else {
          mss->unit[i] = NULL;
        }
        if (mss->unit[i]) {
          mss->unit[i]->msu_object = mss;
          if (mss->unit[i]) {
            //mss->handler[i] = mss->unit[i]->msu_handler;
            list_push_back(&unit_list, &mss->unit[i]->ul_elem);
          }
        }
        if (mss->handler[i]) {
          //Signal(mss->handler[i], SIGF_SINGLE);   //XXX
        }
      }
    } else {
      /* There was no such device used before. Create the cache element
       * and tasks. */
      mss->cache = get_unit_from_cache(mss, 1);
      mss->unit_num = mss->cache->unit_number;

      /* For each LUN create a separate task. The unit number will be
       * created as combination of unitNum and the LUN:
       * unit 0x00 is Unit 0 LUN 0, 0x12 is Unit 1 LUN 2. */
      for (i = 0; i <= mss->max_lun; i++) {
#if 0
        struct TagItem tags[] = {
          { TASKTAG_ARG1,   (IPTR)cl },
          { TASKTAG_ARG2,   (IPTR)o },
          { TASKTAG_ARG3,	  i },
          { TASKTAG_ARG4,	  (IPTR)FindTask(NULL) },
          { TAG_DONE,       0UL }};

        t = AllocMem(sizeof(struct Task), MEMF_PUBLIC|MEMF_CLEAR);
        ml = AllocMem(sizeof(struct MemList) + 2*sizeof(struct MemEntry), MEMF_PUBLIC|MEMF_CLEAR);

        if (t && ml)
        {
          uint8_t *sp = AllocMem(10240, MEMF_PUBLIC|MEMF_CLEAR);
          uint8_t *name = AllocMem(16, MEMF_PUBLIC|MEMF_CLEAR);

          /* Create individual name */
          snprintf(name, 15, "USB MSS %02x.%x",mss->unitNum, i);

          t->tc_SPLower = sp;
          t->tc_SPUpper = sp + 10240;
#if AROS_STACK_GROWS_DOWNWARDS
          t->tc_SPReg = (char *)t->tc_SPUpper - SP_OFFSET;
#else
          t->tc_SPReg = (char *)t->tc_SPLower + SP_OFFSET;
#endif

          ml->ml_NumEntries = 3;
          ml->ml_ME[0].me_Addr = t;
          ml->ml_ME[0].me_Length = sizeof(struct Task);
          ml->ml_ME[1].me_Addr = sp;
          ml->ml_ME[1].me_Length = 10240;
          ml->ml_ME[2].me_Addr = name;
          ml->ml_ME[2].me_Length = 16;

          NEWLIST(&t->tc_MemEntry);
          ADDHEAD(&t->tc_MemEntry, &ml->ml_Node);

          t->tc_Node.ln_Name = name;
          t->tc_Node.ln_Type = NT_TASK;
          t->tc_Node.ln_Pri = 1;     /* same priority as input.device */

          /* Add task. It will get back in touch soon */
          NewAddTask(t, StorageTask, NULL, &tags);
          /* Keep the initialization synchronous */
          Wait(SIGF_SINGLE);
          mss->handler[i] = t;

          /* Detect partitions here? */
          USBMSS_AddVolume(mss->unit[i]);
        }
#endif
      }
    }
  }
  DBGn(USB, "%s(): max_lun=%d\n", __func__, mss->max_lun);
	block_total = 0;
  for (i = 0; i <= mss->max_lun; i++) {
    usbmsd_read_capacity(mss, i, &block_total, NULL);
  }
  block_total++;
	snprintf(name, sizeof name, "ud%c", 'a' + dev_no++);

	block = block_register (name, BLOCK_RAW, "USB", block_total, &msd_operations,
			mss);
	partition_scan (block);
  return mss;
}

void
usbmsd_free(struct usbmsd *mss)
{
  int i;

  DBGn(USB, "[MSS] ::Dispose()\n");
  if (mss) {
    for (i = 0; i <= mss->max_lun; i++) {
      if (mss->unit[i]) {
        list_remove(&mss->unit[i]->ul_elem);
        list_push_back(&mss->cache->units, &mss->unit[i]->ul_elem);
      }
      if (mss->handler[i]) {
        //Signal(mss->handler[i], SIGF_SINGLE);   //XXX
      }
    }

    usbdevice_delete_pipe(mss->device, mss->pipe_in);
    usbdevice_delete_pipe(mss->device, mss->pipe_out);
  }
  //OOP_GetAttr(o, aHidd_USBDevice_Bus, (intptr_t *)&drv);
  free(mss->cdesc);
}


void
usbmsd_reset(struct usbmsd *mss)
{
	struct usbdevice_request req;
	intptr_t ifnr;

	lock_acquire(&mss->lock);
  ifnr= mss->device->interfaces[mss->device->iface].interface->bInterfaceNumber;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = 0xff;
	req.wValue = 0;
	req.wIndex = ifnr;
	req.wLength = 0;

	usbdevice_control_message(mss->device, NULL, &req, NULL, 0);
	lock_release(&mss->lock);
}

uint8_t
usbmsd_get_max_lun(struct usbmsd *mss)
{
	struct usbdevice_request req;
	intptr_t ifnr;
	uint8_t maxlun;

	//OOP_GetAttr(o, aHidd_USBDevice_InterfaceNumber, &ifnr);
  ifnr= mss->device->interfaces[mss->device->iface].interface->bInterfaceNumber;

  //printf("%s(): ifnr=%d\n", __func__, ifnr);
  maxlun = 0;
	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = 0xfe;
	req.wValue = 0;
	req.wIndex = ifnr;
	req.wLength = 1;

	if (usbdevice_control_message(mss->device, NULL, &req, &maxlun, 1)) {
    printf("maxlun=%hhd\n", maxlun);
    return 0;   /* XXX : hack */
		//return maxlun;
  } else {
		return 0xff;
  }
}

static bool
usbmsd_directSCSI(struct usbmsd *mss, uint8_t lun, uint8_t *cmd,
    uint8_t cmd_len, void *data, uint32_t data_len, uint8_t read)
{
  cbw_t cbw;
  csw_t csw;
  int i;
  bool retval = false;

  for (i = 0; i < cmd_len; i++) {
    cbw.CBWCB[i] = cmd[i];
  }
  cbw.bCBWLUN = lun;
  cbw.dCBWSignature = CBW_SIGNATURE;
  cbw.dCBWTag = get_tid();
  cbw.dCBWDataTransferLength = data_len;
  cbw.bmCBWFlags = data_len ? (read ? CBW_FLAGS_IN : CBW_FLAGS_OUT) : 0;
  cbw.bCBWCBLength = cmd_len;

	lock_acquire(&mss->lock);

  DBGn(USB, "[MSS] DirectSCSI -> (%08x,%08x,%08x,%02x,%02x,%02x) @ %p ",
        cbw.dCBWSignature, cbw.dCBWTag, cbw.dCBWDataTransferLength,
        cbw.bCBWLUN, cbw.bmCBWFlags, cbw.bCBWCBLength,
        data_len? data:0);
  DBE(USB, {
        int i;
        for (i = 0; i < cmd_len; i++) {
          printf("%02x%c", cmd[i], i < (cmd_len) ? ',':')');
        }
        printf("\n");
      });
  DBGn(USB, "%s() %d: pipe_in = %p, pipe_out = %p\n", __func__, __LINE__,
      mss->pipe_in, mss->pipe_out);

  if (usbdevice_bulk_transfer(mss->device, mss->pipe_out, &cbw, 31)) {
    DBGn(USB, "%s() %d: bulk_transfer completed for cbw.\n",__func__, __LINE__);
    if (data_len) {
      if (read) {
        usbdevice_bulk_transfer(mss->device, mss->pipe_in, data, data_len);
      } else {
        usbdevice_bulk_transfer(mss->device, mss->pipe_out, data, data_len);
      }
      DBGn(USB, "%s() %d: actual bulk_transfer completed.\n",__func__,__LINE__);
    }

    if (usbdevice_bulk_transfer(mss->device, mss->pipe_in, &csw, 13)) {
      DBGn(USB, "%s() <- (%08x,%08x,%08x,%02x)\n", __func__, csw.dCSWSignature,
          csw.dCSWTag, csw.dCSWDataResidue, csw.bCSWStatus);

      if (csw.dCSWSignature == CSW_SIGNATURE) {
        if (csw.dCSWTag == cbw.dCBWTag) {
          if (!csw.bCSWStatus) {
            retval = true;
          }
        }
      }
    }
  }
  lock_release(&mss->lock);
  DBGn(USB, "%s() returning %d\n", __func__, retval);
  return retval;
}

bool
usbmsd_test_unit_ready(struct usbmsd *mss, uint8_t lun)
{
  uint8_t cmd[12] = {0};

  if (lun > mss->max_lun) {
    DBGn(USB, "%s() %d: returning false.\n", __func__, __LINE__);
    return false;
  }

  DBGn(USB, "%s() %d: lun=%hhd, max_lun=%hhd\n", __func__, __LINE__, lun,
      mss->max_lun);
  if (usbmsd_directSCSI(mss, lun, cmd, 12, NULL, 0, 1)) {
    return true;
  } else {
    DBGn(USB, "%s() %d: returning false.\n", __func__, __LINE__);
    return false;
  }
}

bool
usbmsd_inquiry(struct usbmsd *mss, uint8_t lun, void *buffer,
    uint32_t buffer_length)
{
  uint8_t cmd[6] = {0x12, 0, 0, 0, 0, 0};

  if (lun > mss->max_lun) {
    return false;
  }
  cmd[4] = buffer_length;

  if (buffer) {
    DBGn(USB, "%s(): calling usbmsd_directSCSI()\n", __func__);
    return usbmsd_directSCSI(mss, lun, cmd, 6, buffer, buffer_length, 1);
  } else {
    return false;
  }
}

bool
usbmsd_request_sense(struct usbmsd *mss, uint8_t lun, void *buffer,
    uint32_t buffer_length)
{
  uint8_t cmd[6] = {0x03, 0, 0, 0, 0, 0};

  if (lun > mss->max_lun) {
    return false;
  }
  cmd[4] = buffer_length;

  if (buffer) {
    DBGn(USB, "%s(): calling usbmsd_directSCSI()\n", __func__);
    return usbmsd_directSCSI(mss, lun, cmd, 6, buffer, buffer_length, 1);
  } else {
    return false;
  }
}

bool
usbmsd_read(struct usbmsd *mss, uint8_t lun, void *buffer,
    uint32_t block, uint16_t count)
{
  uint8_t cmd[10] = {0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t *buf = buffer;

  if (lun > mss->max_lun) {
    DBGn(USB, "%s() %d: returning false\n", __func__, __LINE__);
    return false;
  }

  DBGn(USB, "%s(%08x, %04hx) => %p\n", __func__, block, count, buffer);
  if (buf) {
    while(count) {
      uint32_t cnt = count > 1024 ? 1024 : count;

      cmd[2] = block >>24;
      cmd[3] = block >>16;
      cmd[4] = block >>8;
      cmd[5] = block;

      cmd[7] = cnt >> 8;
      cmd[8] = cnt;

      if (!usbmsd_directSCSI(mss, lun, cmd, 10, buf,
            cnt * mss->blocksize[lun], 1)) {
        DBGn(USB, "%s() %d: returning false\n", __func__, __LINE__);
        return false;
      }

      block += cnt;
      buf += cnt * mss->blocksize[lun];
      count -= cnt;
    }
    return true;
  } else {
    DBGn(USB, "%s() %d: returning false\n", __func__, __LINE__);
    return false;
  }
}

bool
usbmsd_write(struct usbmsd *mss, uint8_t lun, const void *buffer,
    uint32_t block, uint16_t count)
{
  uint8_t cmd[10] = {0x2a, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t const *buf = buffer;

  if (lun > mss->max_lun) {
    return false;
  }
  DBGn(USB, "%s(%08x, %04x) <= %p\n", __func__, block, count, buffer);

  if (buf) {
    while(count) {
      uint32_t cnt = count > 1024 ? 1024 : count;

      cmd[2] = block >>24;
      cmd[3] = block >>16;
      cmd[4] = block >>8;
      cmd[5] = block;

      cmd[7] = cnt >> 8;
      cmd[8] = cnt;

      if (!usbmsd_directSCSI(mss, lun, cmd, 10, (void *)buffer,
            count * mss->blocksize[lun], 0)) {
        return false;
      }
      block += cnt;
      buf += cnt * mss->blocksize[lun];
      count -= cnt;
    }
    return true;
  } else {
    return false;
  }
}

bool
usbmsd_read_capacity(struct usbmsd *mss, uint8_t lun,
    uint32_t *block_total, uint32_t *block_size)
{
  uint8_t cmd[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t capacity[8];
  bool retval = false;
  int i;

  if (lun > mss->max_lun) {
    return false;
  }
  DBGn(USB, "%s(%d, %p, %p)\n", __func__, lun, block_total, block_size);

  if (usbmsd_directSCSI(mss, lun, cmd, 10, capacity, 8, 1)) {
    mss->blocksize[lun] = (capacity[4] << 24) | (capacity[5] << 16)
      | (capacity[6] << 8) | capacity[7];

    if (block_total) {
      *block_total = (capacity[0] << 24) | (capacity[1] << 16)
        | (capacity[2] << 8) | capacity[3];
    }
    if (block_size) {
      *block_size = mss->blocksize[lun];
    }
    retval = true;
  }
  DBGn(USB, "[MSS] == ");

  for (i = 0; i < 8; i++) {
    DBGn(USB, "%02x ", capacity[i]);
  }
  DBGn(USB, "\n");
  return retval;
}

static const char *subclass[] = {
  NULL, "SCSI subset", "SFF-8020i", "QIC-157", "UFI", "SFF-8070i",
  "SCSI complete"
};

bool
usbmsd_match(usb_device_descriptor_t *dev, usb_config_descriptor_t *cfg, int i)
{
  bool ret = false;

  DBGn(USB, "usbmsd_match(%p, %p). deviceClass=%#x\n", dev, cfg,
      dev->bDeviceClass);

  if (dev->bDeviceClass == UDCLASS_IN_INTERFACE) {
    usb_interface_descriptor_t *iface = find_idesc(cfg, i, 0);

    DBGn(USB, "%s():UDCLASS_IN_INTERFACE OK. checking interface %d\n", __func__,
        i);
    DBGn(USB, "%s(): iface %d @ %p class %d subclass %d protocol %d\n",
        __func__, i, iface, iface->bInterfaceClass, iface->bInterfaceSubClass,
        iface->bInterfaceProtocol);

    if (iface->bInterfaceClass == UICLASS_MASS) {
      if (iface->bInterfaceProtocol == UIPROTO_DATA_Q921M) {
        if (iface->bInterfaceSubClass > 0 && iface->bInterfaceSubClass < 7) {
					if (0) {
						printf("pintos: %s(): protocol=%s\n", __func__,
								subclass[iface->bInterfaceSubClass]);
					}
          if (iface->bInterfaceSubClass == 1 ||
              iface->bInterfaceSubClass == 2 ||
              iface->bInterfaceSubClass == 5 ||/*XXX*/
              iface->bInterfaceSubClass == 6) {
            ret = true;
          } else {
            printf("iface->bInterfaceSubClass = %d\n", iface->bInterfaceSubClass);
          }
        }
      }
    }
  }
  return ret;
}

struct usbmsd *
usbdev_is_msd(struct usbdevice *dev)
{
  int i;
  if (!dev->config_desc) {
    return NULL;
  }
  for (i = 0; i < dev->config_desc->bNumInterface; i++) {
    if (usbmsd_match(&dev->descriptor, dev->config_desc, i)) {
      struct usbmsd *msd;
      msd = dev->interfaces[i].classdev;
      ASSERT(msd);
      return msd;
    }
  }
  return NULL;
}

bool
usbmsd_is_bootdisk(struct usbmsd *msd)
{
  uint8_t sector[512];

  if (!usbmsd_read(msd, 0, sector, 0, 1)) {
    printf("%s(): usbmsd_read() on sector 0 failed.\n", __func__);
    return false;
  }
  if (sector[510] == 0x55 && sector[511] == 0xaa) {
    return true;
  }
  printf("%s(): sector[510]=0x%hhx,0x%hhx\n", __func__, sector[510],
      sector[511]);
  return false;
}

char const *
usbmsd_name(struct usbmsd const *mss)
{
	return mss->device->product_name;
}

static void
msd_read (void *mss_, block_sector_t sector, void *buffer)
{
	struct usbmsd *mss = mss_;
	usbmsd_read(mss, 0, buffer, sector, 1);
}

static void
msd_write (void *mss_, block_sector_t sector, const void *buffer)
{
	struct usbmsd *mss = mss_;
	usbmsd_write(mss, 0, buffer, sector, 1);
}

static struct block_operations msd_operations = {
	msd_read,
	msd_write,
};
