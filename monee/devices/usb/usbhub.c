#include "devices/usb/usbhub.h"
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/usb/usb.h"
#include "devices/usb/uhci.h"
#include "devices/usb/usbdevice.h"
#include "mem/malloc.h"

#define USB 2

static bool usbhub_explore(struct usbhub *hub);

static void
usbhub_interrupt(void *data)
{
  struct usbhub *hub = (struct usbhub *)data;
  cond_signal_intr(&hub->completion_wait);
}

bool
usbhub_match(usb_device_descriptor_t *dev, usb_config_descriptor_t *cfg, int i)
{
  bool ret = false; 

  if (dev->bDeviceClass == UDCLASS_IN_INTERFACE) {
    usb_interface_descriptor_t *iface = find_idesc(cfg, i, 0);

    DBGn(USB, "%s():UDCLASS_IN_INTERFACE OK. checking interface %d\n", __func__,
        i);
    DBGn(USB, "%s(): iface %d @ %p class %d subclass %d protocol %d\n",
        __func__, i, iface, iface->bInterfaceClass, iface->bInterfaceSubClass,
        iface->bInterfaceProtocol);

    if (iface->bInterfaceClass == UDCLASS_HUB) {
      ret = true;
    }
  }
  DBGn(USB, "%s(%p, %p) returning %s\n",__func__,dev,cfg,ret?"true":"false");
  return ret;
}

struct usbhub *
usbhub_new(struct usbdevice *device)
{
  usb_endpoint_descriptor_t *ep;
  struct usbhub *hub;
  static int i = 0;

  hub = malloc(sizeof(struct usbhub));
  ASSERT(hub);
  hub->device = device;
  usbdevice_pipe_set_timeout(hub->device, NULL, 5000);
  hub->root = false;
  hub->enabled = false;
  hub->got_descriptor = usbhub_get_hub_descriptor(hub, &hub->descriptor);

  if (hub->got_descriptor) {
    DBE(USB, dump_descriptor((usb_device_descriptor_t *)&hub->descriptor));
  } else {
    DBGn(USB, "[USBHub] HUB descriptor not present. I will try later...\n");
    hub->descriptor.bNbrPorts = 1;
  }
  hub->children = malloc(hub->descriptor.bNbrPorts * (sizeof *hub->children));
  ASSERT(hub->children);
  memset(hub->children, 0, hub->descriptor.bNbrPorts*(sizeof *hub->children));
  usbdevice_configure(hub->device, 0);
  ep = usbdevice_get_endpoint(hub->device, 0, 0);
  DBGn(USB, "[USBHub] Endpoint descriptor %p\n", ep);

  if (ep) {
    struct uhci_data *uhci;
    DBE(USB, dump_descriptor((usb_device_descriptor_t *)ep));

    if ((ep->bmAttributes & UE_XFERTYPE) != UE_INTERRUPT) {
      DBGn(USB, "[USBHub] Wrong endpoint type.\n");
      usbdevice_configure(hub->device, USB_UNCONFIG_INDEX);
    }
    uhci = hub->device->bus;
    if (uhci) {
      hub->intr_pipe = uhci_usbdevice_create_pipe(hub->device, PIPE_INTERRUPT,
          ep->bEndpointAddress, ep->bInterval, ep->wMaxPacketSize, 0);
      hub->interrupt.code = usbhub_interrupt;
      hub->interrupt.data = hub;
      uhci_add_interrupt(uhci, hub->intr_pipe, &hub->status[0], 1,
          &hub->interrupt);
    }
  }
  cond_init(&hub->completion_wait);
  usbhub_explore(hub);
  ASSERT(hub->device == device);
  return hub;
}

void
usbhub_free(struct usbhub *hub)
{
  free(hub->children);
  free(hub);
}

struct usbdevice *
usbhub_get_child(struct usbhub *hub, int port)
{
  if (port > 0 && port <= hub->descriptor.bNbrPorts) {
    return hub->children[port - 1];
  }
  return NULL;
}

void
usbhub_port_enable(struct usbhub *hub)
{
}

bool
usbhub_port_reset(struct usbhub *hub, uint16_t port_number)
{
  bool retval = false;
  struct usbdevice_request req;
  usb_port_status_t ps;
  int n;

  n = 10;
  req.bmRequestType = UT_WRITE_CLASS_OTHER;
  req.bRequest = UR_SET_FEATURE;
  req.wValue = UHF_PORT_RESET;
  req.wIndex = port_number;
  req.wLength = 0;

  retval = usbdevice_control_message(hub->device, NULL, &req, NULL, 0);

  if (retval) {
    do {
      usb_delay(USB_PORT_RESET_DELAY);
      retval = usbhub_get_port_status(hub, port_number, &ps);
      if (!retval) {
        break;
      }
      if (!(ps.wPortStatus & UPS_CURRENT_CONNECT_STATUS)) {
        retval = true;
        break;
      }
    } while ((ps.wPortChange & UPS_C_PORT_RESET) == 0 && --n > 0);
  }

  if (n == 0) {
    retval = false;
  } else {
    usbhub_clear_port_feature(hub, port_number, UHF_C_PORT_RESET);
    usb_delay(USB_PORT_RESET_RECOVERY);
  }
  DBGn(USB, "[USBHub] port_reset (%d) %s.\n", port_number, retval?"OK":"error");
  return retval;
}

bool
usbhub_get_port_status(struct usbhub *hub, uint16_t port,
    usb_port_status_t *status)
{
  struct usbdevice_request req;
  DBGn(USB, "usbhub_get_port_status().\n");
  req.bmRequestType = UT_READ_CLASS_OTHER;
  req.bRequest = UR_GET_STATUS;
  req.wValue = 0;
  req.wIndex = port;
  req.wLength = sizeof(usb_port_status_t);

  return usbdevice_control_message(hub->device, NULL, &req, status,
      sizeof(usb_port_status_t));
}

bool
usbhub_get_hub_status(struct usbhub *hub, usb_port_status_t *status)
{
  struct usbdevice_request req;
  DBGn(USB, "[USBHub] get_hub_status().\n");
  req.bmRequestType = UT_READ_CLASS_DEVICE;
  req.bRequest = UR_GET_STATUS;
  req.wValue = 0;
  req.wIndex = 0;
  req.wLength = sizeof(usb_hub_status_t);

  usbdevice_control_message(hub->device, NULL, &req, status,
      sizeof(usb_port_status_t));
  return true;
}

bool
usbhub_clear_hub_feature(struct usbhub *hub, uint16_t feature)
{
  struct usbdevice_request req;
  DBGn(USB, "[USBHub] clear_hub_feature().\n");
  req.bmRequestType = UT_WRITE_CLASS_DEVICE;
  req.bRequest = UR_CLEAR_FEATURE;
  req.wValue = feature;
  req.wIndex = 0;
  req.wLength = 0;

  usbdevice_control_message(hub->device, NULL, &req, NULL, 0);
  return true;
}

bool
usbhub_set_hub_feature(struct usbhub *hub, uint16_t feature)
{
  struct usbdevice_request req;
  DBGn(USB, "[USBHub] SetHubFeature()\n");

  req.bmRequestType = UT_WRITE_CLASS_DEVICE;
  req.bRequest = UR_SET_FEATURE;
  req.wValue = feature;
  req.wIndex = 0;
  req.wLength = 0;

  usbdevice_control_message(hub->device, NULL, &req, NULL, 0);
  return true;
}

bool
usbhub_clear_port_feature(struct usbhub *hub, uint16_t port, uint16_t feature)
{
  struct usbdevice_request req;
  DBGn(USB, "[USBHub] ClearPortFeature(%d, %d)\n", port, feature);

  req.bmRequestType = UT_WRITE_CLASS_OTHER;
  req.bRequest = UR_CLEAR_FEATURE;
  req.wValue = feature;
  req.wIndex = port;
  req.wLength = 0;

  usbdevice_control_message(hub->device, NULL, &req, NULL, 0);
  return true;
}

bool
usbhub_set_port_feature(struct usbhub *hub, uint16_t port, uint16_t feature)
{
  struct usbdevice_request req;
  DBGn(USB, "[USBHub] SetPortFeature(%d, %d)\n", port, feature);

  req.bmRequestType = UT_WRITE_CLASS_OTHER;
  req.bRequest = UR_SET_FEATURE;
  req.wValue = feature;
  req.wIndex = port;
  req.wLength = 0;

  return usbdevice_control_message(hub->device, NULL, &req, NULL, 0);
}

bool
usbhub_get_hub_descriptor(struct usbhub *hub, usb_hub_descriptor_t *desc)
{
  struct usbdevice_request request = {
    bmRequestType:  UT_READ_CLASS_DEVICE,
    bRequest:       UR_GET_DESCRIPTOR,
    wValue:         ((uint8_t)UDESC_HUB) << 8,
    wIndex:         0,
    wLength:        USB_HUB_DESCRIPTOR_SIZE
  };
  usbdevice_control_message(hub->device, NULL, &request, desc,
      USB_HUB_DESCRIPTOR_SIZE);
  return true;
}

void
usbhub_enable(struct usbhub *hub)
{
  int pwrdly, port;

  if (!hub->got_descriptor) {
    hub->got_descriptor = usbhub_get_hub_descriptor(hub, &hub->descriptor);
  }
  pwrdly = hub->descriptor.bPwrOn2PwrGood * UHD_PWRON_FACTOR
    + USB_EXTRA_POWER_UP_TIME;

  for (port = 1; port <= hub->descriptor.bNbrPorts; port++) {
    if (!usbhub_set_port_feature(hub, port, UHF_PORT_POWER)) {
      printf("[usbhub] powerOn on port %d failed\n", port);
    }
    usb_delay(pwrdly);
  }
}

void
usbhub_disable(struct usbhub *hub)
{
  int pwrdly, port;

  if (!hub->got_descriptor) {
    hub->got_descriptor = usbhub_get_hub_descriptor(hub, &hub->descriptor);
  }
  pwrdly = hub->descriptor.bPwrOn2PwrGood * UHD_PWRON_FACTOR
    + USB_EXTRA_POWER_UP_TIME;

  for (port = 1; port <= hub->descriptor.bNbrPorts; port++) {
    if (!usbhub_clear_port_feature(hub, port, UHF_PORT_POWER)) {
      printf("[usbhub] poweroff on port %d failed\n", port);
    }
    usb_delay(pwrdly);
  }
}

static bool
usbhub_explore(struct usbhub *hub)
{
  bool more = false;
  int port;

  if (!hub->got_descriptor) {
    hub->got_descriptor = usbhub_get_hub_descriptor(hub, &hub->descriptor);
  }

  DBGn(USB, "usbhub_explore(): num_ports = %hhd\n", hub->descriptor.bNbrPorts);

  for (port = 1; port <= hub->descriptor.bNbrPorts; port++) {
    usb_port_status_t port_status;
    uint16_t status, change;
    bool fast;

    if (!usbhub_get_port_status(hub, port, &port_status)) {
      LOG(USB, "%s(): usbhub_get_port_status(%p, %d, %p) failed.\n", __func__,
          hub, port, &status);
      continue;
    }
    status = port_status.wPortStatus;
    change = port_status.wPortChange;

    DBGn(USB, "%s(): port %d, status %04x, change %04x\n", __func__, port,
        status, change);

    if (change & UPS_C_PORT_ENABLED) {
      DBGn(USB, "%s(): C_PORT_ENABLED\n", __func__);
      usbhub_clear_port_feature(hub, port, UHF_C_PORT_ENABLE);
    }

    if (change & UPS_C_CONNECT_STATUS) {
      DBGn(USB, "%s(): C_CONNECT_STATUS\n", __func__);
      usbhub_clear_port_feature(hub, port, UHF_C_PORT_CONNECTION);
    }

    /* If the connection status has not changed, and device is still
     * disconnected, skip this port. */
    if (   (0 == (status & UPS_CURRENT_CONNECT_STATUS))
        == (NULL == hub->children[port-1])) {
      DBGn(USB, "%s(): UPS_CURRENT_CONNECT_STATUS reflects actual mapping "
          "at port %d\n", __func__, port);
      continue;
    }

    if (hub->children[port-1]) {
      usbdevice_free(hub->children[port - 1]);
      hub->children[port-1] = NULL;
    }

    if (!(status & UPS_CURRENT_CONNECT_STATUS)) {
      LOG(USB, "%s(): !CURRENT_CONNECT_STATUS\n", __func__);
      continue;
    }

    if (!(status & UPS_PORT_POWER)) {
      DBGn(USB, "%s(): port %d without power???\n", __func__, port);
    }

    /* We probably want to keep restarting USB ports, in cases where we have
     * no driver to utilize the device. However, I will leave it here for now.*/
    usb_delay(USB_PORT_POWERUP_DELAY);

    DBGn(USB, "%s(): restarting device at port %d\n", __func__, port);
    if (!usbhub_port_reset(hub, port)) {
      LOG(USB, "%s(): port %d reset failed\n", __func__, port);
      continue;
    }
    if (!usbhub_get_port_status(hub, port, &port_status)) {
      LOG(USB, "%s(): usbhub_get_port_status(%p, %d, %p) failed\n", __func__,
          hub, port, &status);
      continue;
    }
    status = port_status.wPortStatus;
    change = port_status.wPortChange;

    DBGn(USB, "%s(): port %d, status %04x, change %04x\n", __func__, port,
        status, change);

    if (!(status & UPS_CURRENT_CONNECT_STATUS)) {
      LOG(USB, "%s(): device on port %d disappeared after reset???\n",
          __func__, port);
      continue;
    }

    fast = (status & UPS_LOW_SPEED)?false:true;
    DBGn(USB, "%s(): device found at port %d\n", __func__, port);
    hub->children[port-1] = usb_new_device(hub->device->bus, hub, fast);
    if (!hub->children[port-1]) {
      DBGn(USB, "%s(): no known handler for selected drive. restoring "
          "connection flag.\n", __func__);
      more = true;
    }
  }
  return more;
}


/*
struct uhci_data *
usbhub_get_bus(struct usbhub const *hub)
{
  ASSERT(hub);
  ASSERT(hub->device);
  return hub->device->bus;
}

void
usbhub_onoff(struct usbhub *hub, bool run)
{
  hub->enabled = run;
}
*/
