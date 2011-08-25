#ifndef DEVICES_USBHUB_H
#define DEVICES_USBHUB_H
#include <stdbool.h>
#include <stdint.h>
#include "devices/usb/usb_core.h"
#include "threads/synch.h"
#include "devices/usb/usb.h"

typedef struct usbhub {
  struct usbdevice            *device;
  struct condition            completion_wait;
  uint8_t                     sig_interrupt;

  struct usbdevice            **children;
  usb_hub_descriptor_t        descriptor;
  bool                        got_descriptor;

  struct usb_pipe             *intr_pipe;
  bool                        root;
  bool                        enabled;
  uint8_t                     status[20];
  struct interrupt_data       interrupt;
} usbhub;

struct uhci_data;

void usbhub_free(struct usbhub *hub);
struct usbdevice *usbhub_get_child(struct usbhub *hub, int port);
void usbhub_port_enable(struct usbhub *hub);
bool usbhub_port_reset(struct usbhub *hub, uint16_t port_number);
bool usbhub_get_port_status(struct usbhub *hub, uint16_t port,
    usb_port_status_t *status);
bool usbhub_get_hub_status(struct usbhub *hub, usb_port_status_t *status);
bool usbhub_clear_hub_feature(struct usbhub *hub, uint16_t feature);
bool usbhub_set_hub_feature(struct usbhub *hub, uint16_t feature);
bool usbhub_clear_port_feature(struct usbhub *hub, uint16_t port, uint16_t);
bool usbhub_set_port_feature(struct usbhub *hub, uint16_t port, uint16_t feature);
bool usbhub_get_hub_descriptor(struct usbhub *hub, usb_hub_descriptor_t *desc);
void usbhub_enable(struct usbhub *hub);
void usbhub_disable(struct usbhub *hub);
struct usbhub *usbhub_new(struct usbdevice *device); 
//struct uhci_data *usbhub_get_bus(struct usbhub const *hub);
bool usbhub_match(usb_device_descriptor_t *, usb_config_descriptor_t *, int);
//void usbhub_interrupt(void *data);
//bool usbhub_explore(struct usbhub *hub);

#endif /* devices/usbhub.h */
