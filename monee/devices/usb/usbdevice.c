#include "devices/usb/usbdevice.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <debug.h>
#include <string.h>
#include <bitmap.h>
#include "devices/usb/usb.h"
#include "devices/usb/usbhub.h"
#include "devices/usb/usbmsd.h"
#include "devices/usb/uhci.h"
#include "mem/malloc.h"

#define USB 2
#define MAX_USB_DRIVERS 8
#define BITMAP_SIZE 128

typedef void *(*new_func_t)(struct usbdevice *);
typedef bool (*match_func_t)(usb_device_descriptor_t *,
    usb_config_descriptor_t *, int);

typedef struct usbclass_t {
  match_func_t match_func;
  new_func_t new_func;
  char const *name;
} usbclass_t;

struct usbclass_t usbclasses[] = {
  {usbhub_match, (new_func_t)usbhub_new, "usbhub"},
  {usbmsd_match, (new_func_t)usbmsd_new, "usbmsd"},
};

static void *usb_drivers[MAX_USB_DRIVERS];
static struct bitmap *usb_driver_bitmaps[MAX_USB_DRIVERS];
static int num_usb_drivers = 0;

static struct list devlist;

static const char *unknown_name = "? unknown name ?";
static const char *unknown_manufacturer = "? unknown manufacturer ?";
static const char *unknown_serial = "? unknown serial ?";

static bool usb_set_address(struct usbdevice *dev, uint8_t address);
static bool usbdevice_set_config(struct usbdevice *dev, int c);
static bool fill_iface(struct usbdevice *, int, int);
static void free_iface(struct usbdevice *dev, int i);


static bool
usbdevice_get_string(struct usbdevice *dev, uint16_t id, uint16_t language,
    usb_string_descriptor_t *string)
{
  if (string) {
    struct usbdevice_request req;
    req.bmRequestType = UT_READ_DEVICE;
    req.bRequest = UR_GET_DESCRIPTOR;
    req.wValue = UDESC_STRING << 8 | (id & 0xff);
    req.wIndex = language;
    req.wLength = 1;

    usbdevice_control_message(dev, NULL, &req, string, 1);
    req.wLength = string->bLength;
    return uhci_control_transfer(dev->bus, dev->default_pipe, &req, string,
        string->bLength);
  }
  return false;
}

struct usbdevice *
usbdevice_new(uint8_t address, void *hub, struct uhci_data *bus, bool fast,
    uint8_t maxpacket, int iface)
{
  usb_string_descriptor_t string;
  struct usbdevice *dev;
  uint16_t langid;
  int i;

  dev = malloc(sizeof(struct usbdevice));
  ASSERT(dev);

  dev->address = address;                   /* default 0. */
  dev->hub = hub;                           /* default 0. */
  dev->bus = bus;                           /* default 0. */
  dev->fast = fast;                         /* default true. */
  dev->maxpacket = maxpacket;               /* default 8. */
  dev->iface = iface;                       /* default 0. */
  dev->default_pipe = NULL;
  dev->config = USB_UNCONFIG_NO;
  dev->interfaces = NULL;
  dev->config_desc = NULL;

  /* The USB bus object is not specified. Try to get it from itself.  */
  if (!dev->bus && dev->hub) {
    NOT_IMPLEMENTED();
    //dev->bus = dev->hub->dev->bus;
  }

  DBGn(USB, "%s(): address=%02x, interface=%02x, bus=%p, hub=%p\n", __func__,
      dev->address, dev->iface, dev->bus, dev->hub);

  if (dev->bus) {
    if (!dev->default_pipe) {
      dev->default_pipe = usbdevice_create_pipe(dev, PIPE_CONTROL, false, 0, 0,
          100);
    }

    /* Address was either unknown or equals zero. In such case, the right
     * address has to be set. */
    if (dev->address == 0) {
      DBGn(USB, "%s(): fetching new device address\n", __func__);
      uint8_t addr = usb_alloc_address(dev->bus);
      DBGn(USB, "%s(): trying address %d...\n", __func__, addr);
      if (!usb_set_address(dev, addr)) {
        usb_free_address(dev->bus, addr);
      }
    }

    /* Check whether the address is set now */
    if (!dev->address) {
      NOT_IMPLEMENTED();
      free(dev);
      dev = NULL;
    }
  }

  usbdevice_get_device_descriptor(dev, &dev->descriptor);

  DBGn(USB, "%s(): device %04x:%04x %02x/%02x/%02x at address %p:%02x\n",
      __func__, dev->descriptor.idProduct, dev->descriptor.idVendor,
      dev->descriptor.bDeviceClass, dev->descriptor.bDeviceSubClass,
      dev->descriptor.bDeviceProtocol, dev->bus, dev->address);

  usbdevice_get_string(dev, USB_LANGUAGE_TABLE, 0, &string);
  DBGn(USB, "%s(): default lang_id=%04x\n", __func__, string.bString[0]);
  langid = string.bString[0];

  if (   dev->descriptor.iProduct 
      && usbdevice_get_string(dev, dev->descriptor.iProduct, langid, &string)) {
    dev->product_name = malloc(1 + ((string.bLength - 2) >> 1));
    for (i = 0; i < (string.bLength - 2) >> 1; i++) {
      dev->product_name[i] = (string.bString[i] == ' ')?'_':string.bString[i];
    }
    dev->product_name[(string.bLength - 2) >> 1] = 0;
    DBGn(USB, "%s(): iProduct = \"%s\"\n", __func__, dev->product_name);
  } else {
    dev->product_name = (char *)unknown_name;
  }

  if (   dev->descriptor.iManufacturer
      && usbdevice_get_string(dev, dev->descriptor.iManufacturer, langid,
                              &string)) {
    dev->manufacturer_name = malloc(1 + ((string.bLength - 2) >> 1));
    ASSERT(dev->manufacturer_name);

    for (i = 0; i < (string.bLength - 2) >> 1; i++) {
      dev->manufacturer_name[i] = string.bString[i];
    }
    dev->manufacturer_name[(string.bLength - 2) >> 1] = 0;

    DBGn(USB, "%s(): iManufacturer = \"%s\"\n", __func__,
        dev->manufacturer_name);
  } else {
    dev->manufacturer_name = (char *)unknown_manufacturer;
  }

  if (   dev->descriptor.iSerialNumber
      && usbdevice_get_string(dev, dev->descriptor.iSerialNumber, langid,
                              &string)) {
    dev->serialnumber_name = malloc(1 + ((string.bLength - 2) >> 1));
    ASSERT(dev->serialnumber_name);

    for (i = 0; i < (string.bLength - 2) >> 1; i++) {
      dev->serialnumber_name[i] = string.bString[i];
    }
    dev->serialnumber_name[(string.bLength - 2) >> 1] = 0;
    DBGn(USB, "%s(): iSerial = \"%s\"\n", __func__, dev->serialnumber_name);
  } else {
    dev->serialnumber_name = (char *)unknown_serial;
  }

  return dev;
}

bool 
usbdevice_get_descriptor(struct usbdevice *dev, uint16_t type, uint16_t index,
    uint32_t length, void *descriptor)
{
  struct usbdevice_request req;
  bool ret;

  req.bmRequestType = UT_READ_DEVICE;
  req.bRequest = UR_GET_DESCRIPTOR;
  req.wValue = type << 8 | index;
  req.wIndex = 0;
  req.wLength = length;

  return usbdevice_control_message(dev, NULL, &req, descriptor, length);
}

bool
usbdevice_get_config_descriptor(struct usbdevice *dev, uint16_t index,
    usb_config_descriptor_t *descriptor)
{
  return usbdevice_get_descriptor(dev, UDESC_CONFIG, index,
      USB_CONFIG_DESCRIPTOR_SIZE, descriptor);
}

bool
usbdevice_get_device_descriptor(struct usbdevice *dev,
    usb_device_descriptor_t *descriptor)
{
  return usbdevice_get_descriptor(dev, UDESC_DEVICE, 0,
      USB_DEVICE_DESCRIPTOR_SIZE, descriptor);
}

bool
usbdevice_control_message(struct usbdevice *dev, struct usb_pipe *pipe,
    struct usbdevice_request *request, void *buffer, uint32_t length)
{
  struct usb_pipe *p;

  ASSERT(dev);
  ASSERT(dev->default_pipe);
  p = pipe ? pipe : dev->default_pipe;
  ASSERT(p);
  return uhci_control_transfer(dev->bus, p, request, buffer, length);
}

void
usbdevice_get_status(struct usbdevice *dev, usb_status_t *status)
{
  struct usbdevice_request req;

  req.bmRequestType = UT_READ_DEVICE;
  req.bRequest = UR_GET_STATUS;
  req.wValue = 0;
  req.wIndex = 0;
  req.wLength = sizeof(usb_status_t);
  usbdevice_control_message(dev, NULL, &req, status, sizeof(usb_status_t));
}

struct usb_pipe *
usbdevice_create_pipe(struct usbdevice *dev, enum usb_pipetype type,
    uint8_t endpoint, uint8_t period, uint32_t maxpacket, uint32_t timeout)
{
  return uhci_create_pipe(dev->bus, type, dev->fast, dev->address, endpoint,
      period, maxpacket ? maxpacket : dev->maxpacket, timeout);
}

void
usbdevice_delete_pipe(struct usbdevice *dev, struct usb_pipe *pipe)
{
  uhci_delete_pipe(dev->bus, pipe);
}

void
usbdevice_pipe_set_timeout(struct usbdevice *dev, struct usb_pipe *pipe,
    int64_t nanoseconds)
{
  uhci_pipe_set_timeout(dev->bus, pipe ? pipe : dev->default_pipe,nanoseconds);
}

bool
usbdevice_bulk_transfer(struct usbdevice *dev, struct usb_pipe *pipe,
    void *buffer, uint32_t length)
{
  if (pipe) {
    return uhci_bulk_transfer(dev->bus, pipe, buffer, length);
  } else {
    return false;
  }
}

static bool
usbdevice_set_config(struct usbdevice *dev, int c)
{
  struct usbdevice_request req;
  req.bmRequestType = UT_WRITE_DEVICE;
  req.bRequest = UR_SET_CONFIG;
  req.wValue = c;
  req.wIndex = 0;
  req.wLength = 0;

  return usbdevice_control_message(dev, NULL, &req, NULL, 0);
}

usb_interface_descriptor_t *
find_idesc(usb_config_descriptor_t *cd, int ifaceidx, int altidx)
{
  char *p = (char *)cd;
  char *end = p + cd->wTotalLength;
  int curidx, lastidx, curaidx = 0;
  usb_interface_descriptor_t *d;

  for (curidx = lastidx = -1; p < end; ) {
    d = (usb_interface_descriptor_t *)p;
    DBGn(USB, "%s(): idx=%d(%d) altidx=%d(%d) len=%d type=%d\n", __func__,
        ifaceidx, curidx, altidx, curaidx, d->bLength, d->bDescriptorType);
    if (d->bLength == 0) { /* bad descriptor */
      break;
    }
    p += d->bLength;
    if (p <= end && d->bDescriptorType == UDESC_INTERFACE) {
      if (d->bInterfaceNumber != lastidx) {
        lastidx = d->bInterfaceNumber;
        curidx++;
        curaidx = 0;
      } else
        curaidx++;
      if (ifaceidx == curidx && altidx == curaidx)
        return (d);
    }
  }
  return NULL;
}

usb_endpoint_descriptor_t *
find_edesc(usb_config_descriptor_t *cd, int ifaceidx, int altidx, int endptidx)
{
  char *p = (char *)cd;
  char *end = p + cd->wTotalLength;
  usb_interface_descriptor_t *d;
  usb_endpoint_descriptor_t *e;
  int curidx;

  d = find_idesc(cd, ifaceidx, altidx);
  if (d == NULL) {
    return NULL;
  }
  if (endptidx >= d->bNumEndpoints) { /* quick exit */
    return (NULL);
  }

  curidx = -1;
  for (p = (char *)d + d->bLength; p < end; ) {
    e = (usb_endpoint_descriptor_t *)p;
    if (e->bLength == 0) { /* bad descriptor */
      break;
    }
    p += e->bLength;
    if (p <= end && e->bDescriptorType == UDESC_INTERFACE)
      return (NULL);
    if (p <= end && e->bDescriptorType == UDESC_ENDPOINT) {
      curidx++;
      if (curidx == endptidx) {
        return e;
      }
    }
  }
  return NULL;
}

static bool
fill_iface(struct usbdevice *dev, int ifaceidx, int altidx)
{
  char *p, *end;
  struct interface_data *ifc = &dev->interfaces[ifaceidx];
  usb_interface_descriptor_t *idesc;
  int endpt, nendpt;

  DBGn(USB, "[USBDevice] fill_iface: ifaceidx=%d altidx=%d\n",
        ifaceidx, altidx);

  idesc = find_idesc(dev->config_desc, ifaceidx, altidx);
  if (idesc == NULL) {
    return false;
  }

  ifc->interface = idesc;
  ifc->index = ifaceidx;
  ifc->altindex = altidx;

  nendpt = ifc->interface->bNumEndpoints;

  DBGn(USB, "[USBDevice] fill_iface: found idesc nendpt=%d\n", nendpt);

  if (nendpt != 0) {
    ifc->endpoints = malloc(nendpt * sizeof(endpoint_data));
    if (ifc->endpoints == NULL) {
      return false;
    }
  } else {
    ifc->endpoints = NULL;
  }

  p = (char *)ifc->interface + ifc->interface->bLength;
  end = (char *)dev->config_desc + dev->config_desc->wTotalLength;
#define ed ((usb_endpoint_descriptor_t *)p)

  for (endpt = 0; endpt < nendpt; endpt++) {
    DBGn(USB, "[USBDevice] fill_iface: endpt=%d\n", endpt);
    for (; p < end; p += ed->bLength) {
      DBGn(USB, "[USBDevice] fill_iface: p=%p end=%p len=%d type=%d\n", p, end,
          ed->bLength, ed->bDescriptorType);
      if (p + ed->bLength <= end && ed->bLength != 0 &&
          ed->bDescriptorType == UDESC_ENDPOINT) {
        goto found;
      }
      if (ed->bLength == 0 ||
          ed->bDescriptorType == UDESC_INTERFACE) {
        break;
      }
    }
    /* passed end, or bad desc */
    MSG ("[USBDevice] fill_iface: bad descriptor(s): %s\n",
        ed->bLength == 0 ? "0 length" :
        ed->bDescriptorType == UDESC_INTERFACE ? "iface desc":"out of data");
    goto bad;
found:
    DBGn(USB, "[USBDevice] fill_iface: endpoint descriptor %p\n", ed);
    ifc->endpoints[endpt].endpoint = ed;
    p += ed->bLength;
  }
  return true;

bad:
  if (ifc->endpoints != NULL) {
    free(ifc->endpoints);
    ifc->endpoints = NULL;
  }
  return false;
}

static void
free_iface(struct usbdevice *dev, int i)
{
  struct interface_data *iface;
  iface = &dev->interfaces[i];
  if (iface->endpoints) {
    free(iface->endpoints);
  }
}

bool
usbdevice_configure(struct usbdevice *dev, int config_nr)
{
  usb_config_descriptor_t cd, *cdp;
  int i, length;
  bool err;

  DBGn(USB, "%s(%d)\n", __func__, config_nr);

  if (dev->config != USB_UNCONFIG_NO) {
    int i, ifaces = dev->config_desc->bNumInterface;
    for (i=0; i < ifaces; i++) {
      free_iface(dev, i);
    }
    free(dev->interfaces);
    free(dev->config_desc);
    dev->interfaces = NULL;
    dev->config_desc = NULL;
    dev->config = USB_UNCONFIG_NO;
  }

  if (config_nr == USB_UNCONFIG_INDEX) {
    DBGn(USB, "%s(): Unconfiguring ", __func__);
    if ((err = usbdevice_set_config(dev, USB_UNCONFIG_NO))) {
      DBGn(USB, "with success\n");
    } else {
      DBGn(USB, "with ERROR\n");
    }
    return err;
  }

  usbdevice_get_config_descriptor(dev, 0, &cd);
  length = cd.wTotalLength;

  DBGn(USB, "%s(): Fetching config descriptor of length %d\n", __func__,
      length);

  cdp = malloc(length);
  if (cdp == NULL) {
    return false;
  }

  for (i=0; i < 3; i++) {
    if (usbdevice_get_descriptor(dev, UDESC_CONFIG, config_nr, length, cdp)) {
      break;
    }
    DBGn(USB, "%s(): retry...\n", __func__);
    usb_delay(200);
  }

  if (i == 3) {
    DBGn(USB, "%s(): failed...\n", __func__);
  }

  /* TODO: Power! Self powered? */
  err = usbdevice_set_config(dev, cdp->bConfigurationValue);

  DBGn(USB, "%s(): Allocating %d interfaces\n", __func__,
      cdp->bNumInterface);
  dev->interfaces = malloc(sizeof(interface_data) * cdp->bNumInterface);
  if (!dev->interfaces) {
    /* NOMEM! */
    ABORT();
  }

  dev->config_desc = cdp;
  dev->config = cdp->bConfigurationValue;

  for (i = 0; i < cdp->bNumInterface; i++) {
    err = fill_iface(dev, i, 0);
    if (!err) {
      DBGn(USB, "%s(): error at interface %d\n", __func__, i);
      while (--i >= 0) {
        free_iface(dev, i);
      }
      return false;
    }
  }
  return true;
}

usb_interface_descriptor_t *
usbdevice_get_interface(struct usbdevice *dev, int interface)
{
  usb_interface_descriptor_t *d = NULL;

  if (dev->config != USB_UNCONFIG_NO) {
    if (interface < dev->config_desc->bNumInterface) {
      d = dev->interfaces[interface].interface;
    }
  }
  return d;
}

usb_endpoint_descriptor_t *
usbdevice_get_endpoint(struct usbdevice *dev, int interface, int endpoint)
{
  usb_endpoint_descriptor_t *d = NULL;

  if (dev->config != USB_UNCONFIG_NO) {
    if (interface < dev->config_desc->bNumInterface) {
      if (endpoint < dev->interfaces[interface].interface->bNumEndpoints) {
        d = dev->interfaces[interface].endpoints[endpoint].endpoint;
      }
    }
  }
  DBE(USB, dump_descriptor((usb_device_descriptor_t *)d));

  return d;
}

void
dump_descriptor(usb_device_descriptor_t *desc)
{
  MSG ("[USB] Descriptor dump:\n");
  MSG ("[USB]  bLength = %d\n", desc->bLength);

  if (desc->bDescriptorType == UDESC_DEVICE)
  {
    MSG ("[USB]  bDescriptorType = %d (Device)\n", desc->bDescriptorType);
    usb_device_descriptor_t *d = (usb_device_descriptor_t *)desc;
    MSG ("[USB]  bcdUSB = 0x%04x\n", d->bcdUSB);
    MSG ("[USB]  bDeviceClass = 0x%02x\n", d->bDeviceClass);
    MSG ("[USB]  bDeviceSubClass = 0x%02x\n", d->bDeviceSubClass);
    MSG ("[USB]  bDeviceProtocol = 0x%02x\n", d->bDeviceProtocol);
    MSG ("[USB]  bMaxPacketSize = 0x%04x\n", d->bMaxPacketSize);
    MSG ("[USB]  idVendor = 0x%04x\n", d->idVendor);
    MSG ("[USB]  idProduct = 0x%04x\n", d->idProduct);
    MSG ("[USB]  bcdDevice = 0x%04x\n", d->bcdDevice);
    MSG ("[USB]  iManufacturer = 0x%02x\n", d->iManufacturer);
    MSG ("[USB]  iProduct = 0x%02x\n", d->iProduct);
    MSG ("[USB]  iSerialNumber = 0x%02x\n", d->iSerialNumber);
    MSG ("[USB]  bNumConfigurations = %d\n", d->bNumConfigurations);
  } else if (desc->bDescriptorType == UDESC_CONFIG) {
    MSG ("[USB]  bDescriptorType = %d (Config)\n", desc->bDescriptorType);
    usb_config_descriptor_t *d = (usb_config_descriptor_t *)desc;
    MSG ("[USB]  wTotalLength = %d\n", d->wTotalLength);
    MSG ("[USB]  bNumInterface = %d\n", d->bNumInterface);
    MSG ("[USB]  bConfigurationValue = %d\n", d->bConfigurationValue);
    MSG ("[USB]  iConfiguration = %d\n", d->iConfiguration);
    MSG ("[USB]  bmAttributes = 0x%02x  ", d->bmAttributes);
    if (d->bmAttributes & UC_SELF_POWERED) {
      MSG ("SELF_POWERED ");
		}
    if (d->bmAttributes & UC_BUS_POWERED) {
      MSG ("BUS_POWERED ");
		}
    if (d->bmAttributes & UC_REMOTE_WAKEUP) {
      MSG ("REMOTE_WAKEUP ");
		}
    MSG("\n");
    MSG("[USB]  bMaxPower = %d mA\n", d->bMaxPower * UC_POWER_FACTOR);
  } else if (desc->bDescriptorType == UDESC_HUB) {
    usb_hub_descriptor_t *d = (usb_hub_descriptor_t *)desc;
    int i;
    MSG("[USB]  bDescriptorType = %d (Hub)\n", desc->bDescriptorType);
    MSG("[USB]  bNbrPorts = %d\n", d->bNbrPorts);
    MSG("[USB]  wHubCharacteristics = 0x%04x\n", d->wHubCharacteristics);
    MSG("[USB]  bPwrOn2PwrGood = %d ms\n", d->bPwrOn2PwrGood * UHD_PWRON_FACTOR);
    MSG("[USB]  bHubContrCurrent = %d\n", d->bHubContrCurrent);
    MSG("[USB]  DeviceRemovable = ");
    for (i=0; i < 32; i++) {
      MSG("%02x", d->DeviceRemovable[i]);
		}
    MSG("\n");
  } else if (desc->bDescriptorType == UDESC_ENDPOINT) {
    MSG("[USB]  bDescriptorType = %d (Endpoint)\n",
        desc->bDescriptorType);
    usb_endpoint_descriptor_t *d = (usb_endpoint_descriptor_t *)desc;
    MSG("[USB]  bEndpointAddress = %02x\n", d->bEndpointAddress);
    MSG("[USB]  bmAttributes = %02x\n", d->bmAttributes);
    MSG("[USB]  wMaxPacketSize = %d\n", d->wMaxPacketSize);
    MSG("[USB]  bInterval = %d\n", d->bInterval);
  } else {
    MSG("[USB]  bDescriptorType = %d\n", desc->bDescriptorType);
	}
}

static bool
usb_set_address(struct usbdevice *dev, uint8_t address)
{
  void *pipe = NULL;

  struct usbdevice_request request = {
    bmRequestType:  UT_WRITE_DEVICE,
    bRequest:       UR_SET_ADDRESS,
    wValue:         address,
    wIndex:         0,
    wLength:        0
  };

  if (dev->default_pipe) {
    pipe = dev->default_pipe;
  } else {
    pipe = uhci_create_pipe(dev->bus, PIPE_CONTROL, dev->fast, dev->address,
        0, 0, dev->maxpacket, 100);
  }

  bool ret = uhci_control_transfer(dev->bus, pipe, &request, NULL, 0);
  ASSERT(ret);
  uhci_delete_pipe(dev->bus, pipe);

  dev->default_pipe = NULL;

  if (ret) {
    DBGn(USB, "%s(): testing address %d...\n", __func__, address);
    usb_device_descriptor_t descriptor;

    pipe = uhci_create_pipe(dev->bus, PIPE_CONTROL, dev->fast, address, 0, 0,
        dev->maxpacket, 100);

    struct usbdevice_request request = {
      bmRequestType:  UT_READ_DEVICE,
      bRequest:       UR_GET_DESCRIPTOR,
      wValue:         UDESC_DEVICE << 8,
      wIndex:         0,
      wLength:        USB_DEVICE_DESCRIPTOR_SIZE
    };
    ret = uhci_control_transfer(dev->bus, pipe, &request, &descriptor,
        sizeof(descriptor));
    ASSERT(ret);

    if (ret) {
      DBGn(USB, "%s(): New address set correctly\n", __func__);
      dev->address = address;
      dev->default_pipe = pipe;
      return true;
    } else {
      uhci_delete_pipe(dev->bus, pipe);
      return false;
    }
  } else {
    return false;
  }
}

void
usbdevice_free(struct usbdevice *dev)
{
  int i;
  if (dev->product_name && dev->product_name != unknown_name) {
    free(dev->product_name);
  }
  if (dev->manufacturer_name && dev->manufacturer_name!=unknown_manufacturer) {
    free(dev->manufacturer_name);
  }
  if (dev->serialnumber_name && dev->serialnumber_name != unknown_serial) {
    free(dev->serialnumber_name);
  }
  if (dev->default_pipe) {
    uhci_delete_pipe(dev->bus, dev->default_pipe);
  }
  //XXX
  switch (dev->descriptor.bDeviceClass) {
    case UDCLASS_HUB:
      if (dev->config_desc) {
        ASSERT(dev->config_desc->bNumInterface == 1);
        for (i = 0; i < dev->config_desc->bNumInterface; i++) {
          usbhub_free(dev->interfaces[i].classdev);
        }
      }
      break;
    default:
      NOT_IMPLEMENTED();
      break;
  }
}


void
register_usb_driver(void *driver)
{
  ASSERT(num_usb_drivers < MAX_USB_DRIVERS);
  usb_drivers[num_usb_drivers] = driver;
  usb_driver_bitmaps[num_usb_drivers] = bitmap_create (BITMAP_SIZE);
  bitmap_mark(usb_driver_bitmaps[num_usb_drivers], 0);
  bitmap_mark(usb_driver_bitmaps[num_usb_drivers], 1);
  num_usb_drivers++;
}

uint8_t
usb_alloc_address(const void *driver)
{
  size_t addr;
  int i;
  for (i = 0; i < num_usb_drivers; i++) {
    if (usb_drivers[i] == driver) {
      break;
    }
  }
  ASSERT(i < num_usb_drivers);
  addr = bitmap_scan(usb_driver_bitmaps[i], 0, 1, false);
  ASSERT(addr != BITMAP_ERROR);
  bitmap_mark(usb_driver_bitmaps[i], addr);
  return addr;
}

void
usb_free_address(void *driver, uint8_t addr)
{
  int i;
  for (i = 0; i < num_usb_drivers; i++) {
    if (usb_drivers[i] == driver) {
      break;
    }
  }
  ASSERT(i < num_usb_drivers);
  bitmap_reset(usb_driver_bitmaps[i], addr);
}

void
usb_delay(int64_t milliseconds)
{
  timer_msleep(milliseconds);
}

struct usbdevice *
usb_new_device(struct uhci_data *bus, struct usbhub *hub, bool fast)
{
  struct usbdevice *new_device = NULL;
  void *pipe;
  usb_device_descriptor_t descriptor;
  usb_config_descriptor_t config;
  void *cdesc;
  uint8_t address;

  struct usbdevice_request request = {
    bmRequestType:      UT_READ_DEVICE,
    bRequest:           UR_GET_DESCRIPTOR,
    wValue:             UDESC_DEVICE << 8,
    wIndex:             0,
    wLength:            8
  };
  memset(&descriptor, 0, sizeof(descriptor));

  if (bus) {
    pipe = uhci_create_pipe(bus, PIPE_CONTROL, fast, 0, 0, 0, 8, 100);
    if (!uhci_control_transfer(bus, pipe, &request, &descriptor, 8)) {
      MSG("%s() %d: returning NULL.\n", __func__, __LINE__);
      return NULL;
    }

    DBGn(USB, "%s():\n", __func__);
    DBE(USB, dump_descriptor(&descriptor));

    if (descriptor.bDescriptorType == 0) {
      return NULL;
    }

    if ((address = usb_alloc_address(bus))) {
      struct usbdevice_request req = {
        bmRequestType:  UT_WRITE_DEVICE,
        bRequest:       UR_SET_ADDRESS,
        wValue:         address,
        wIndex:         0,
        wLength:        0
      };

      uhci_control_transfer(bus, pipe, &req, NULL, 0);
      DBGn(USB, "%s() %d:\n", __func__, __LINE__);
      uhci_delete_pipe(bus, pipe);
      DBGn(USB, "%s() %d:\n", __func__, __LINE__);

      pipe = uhci_create_pipe(bus, PIPE_CONTROL, fast, address, 0, 0,
          descriptor.bMaxPacketSize, 100);
      DBGn(USB, "%s() %d:\n", __func__, __LINE__);
      if (!pipe) {
        MSG("[USB] Could not set device address\n");
        return NULL;
      }
    }

    request.wValue = UDESC_CONFIG << 8;
    request.wLength = USB_CONFIG_DESCRIPTOR_SIZE;

    DBGn(USB, "%s() %d:\n", __func__, __LINE__);
    uhci_control_transfer(bus, pipe, &request, &config,
        USB_CONFIG_DESCRIPTOR_SIZE);
    DBGn(USB, "%s() %d:\n", __func__, __LINE__);

    if (cdesc = malloc(config.wTotalLength)) {
      request.wLength = config.wTotalLength;
      uhci_control_transfer(bus, pipe, &request, cdesc, config.wTotalLength);
    }
    uhci_delete_pipe(bus, pipe);


    switch(descriptor.bDeviceClass) {
      case UDCLASS_HUB:
        DBGn(USB, "%s() %d: calling usbdevice_new(%hhx, %p)\n", __func__,
            __LINE__, address, hub);
        new_device = usbdevice_new(address, hub, bus, fast,
            descriptor.bMaxPacketSize, 0);
        hub = usbhub_new(new_device);
        ASSERT(new_device->config_desc);
        ASSERT(new_device->config_desc->bNumInterface == 1);
        ASSERT(new_device->interfaces);
        new_device->interfaces[0].classdev = NULL;
        break;

      default:
        {
          int i;
          /* Try a match for every interface */
          for (i = config.bNumInterface; i > 0; i--) {
            unsigned c;
            bool found = false;
            DBGn(USB, "%s() %d: calling usbdevice_new(%hhx, %p)\n", __func__,
                __LINE__, address, hub);
            new_device = usbdevice_new(address, hub, bus, fast,
                descriptor.bMaxPacketSize, i - 1);

            DBGn(USB, "%s(): interface %d\n", __func__, i - 1);
            /* check interface. */
            for (c = 0; c < sizeof usbclasses/sizeof usbclasses[0]; c++) {
              struct usbclass_t *uc = &usbclasses[c];
              if (uc->match_func(&descriptor, cdesc, i - 1)) {
                void *classdev;
                LOG(USB, "[USB] Found %s class device '%s'\n",
                    uc->name, new_device->product_name);
                classdev = uc->new_func(new_device);
                ASSERT(new_device->config_desc);
                ASSERT(new_device->config_desc->bNumInterface
                    == config.bNumInterface);
                ASSERT(new_device->interfaces);
                new_device->interfaces[i - 1].classdev = classdev;
                DBGn(USB, "%s() %d: new_device->interfaces=%p, "
                    "new_device->interfaces[0].classdev=%p\n", __func__,
                    __LINE__, new_device->interfaces,
                    new_device->interfaces[0].classdev);
                found = true;
                break;
              }
            }
            if (!found) {
              LOG(USB, "[USB] Unknown USB device '%s'. class %02hhx "
                  "subclass %02hhx\n",
                  new_device->product_name, descriptor.bDeviceClass,
                  descriptor.bDeviceSubClass);
            }

          }
          break;
        }
    }

    if (cdesc) {
      free(cdesc);
    }
  }
  list_push_back(&devlist, &new_device->devlist_elem);
  return new_device;
}

void
usb_devlist_init(void)
{
  list_init(&devlist);
}

struct list *
usb_get_devlist(void)
{
  return &devlist;
}
