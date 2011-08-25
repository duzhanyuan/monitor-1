#ifndef DEVICES_USBDEVICE_H
#define DEVICES_USBDEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "devices/usb/usb.h"

struct usbdevice;
struct usbdevice_request;
struct usb_pipe;

typedef struct endpoint_data {
  usb_endpoint_descriptor_t *endpoint;
} endpoint_data;

typedef struct interface_data {
  usb_interface_descriptor_t  *interface;
  struct endpoint_data        *endpoints;
  int                         index;
  int                         altindex;
  void                        *classdev;
} interface_data;

typedef struct usbdevice {
  uint8_t                     address;
  uint8_t                     iface;
  uint8_t                     fast;
  uint8_t                     maxpacket;
  uint16_t                    langid;
  usb_device_descriptor_t     descriptor;
  usb_config_descriptor_t     *config_desc;
  struct interface_data       *interfaces;

  int                         config;

  char                        *product_name;
  char                        *manufacturer_name;
  char                        *serialnumber_name;

  void                        *default_pipe;
  void                        *hub;
  struct uhci_data            *bus;

  struct list_elem            devlist_elem;
  //OOP_Object                  *next;
} usbdevice;


struct usbdevice *usbdevice_new(uint8_t address, void *hub,
    struct uhci_data *bus, bool fast, uint8_t maxpacket, int iface);
void usbdevice_free(struct usbdevice *dev);
usb_endpoint_descriptor_t *usbdevice_get_endpoint(struct usbdevice *, int, int);
struct usb_pipe *usbdevice_create_pipe(struct usbdevice *, enum usb_pipetype,
    uint8_t, uint8_t, uint32_t, uint32_t);
void usbdevice_pipe_set_timeout(struct usbdevice *, struct usb_pipe *,int64_t);
bool usbdevice_control_message(struct usbdevice *dev, struct usb_pipe *pipe,
    struct usbdevice_request *request, void *buffer, uint32_t length);
void usbdevice_get_status(struct usbdevice *, usb_status_t *);
void usbdevice_delete_pipe(struct usbdevice *, struct usb_pipe *);
bool usbdevice_bulk_transfer(struct usbdevice *, struct usb_pipe *,
    void *, uint32_t);
bool usbdevice_configure(struct usbdevice *dev, int config_nr);
void dump_descriptor(usb_device_descriptor_t *desc);
usb_interface_descriptor_t *find_idesc(usb_config_descriptor_t *, int, int);
usb_endpoint_descriptor_t *find_edesc(usb_config_descriptor_t *, int, int, int);
usb_interface_descriptor_t *usbdevice_get_interface(struct usbdevice *,
    int);
bool usbdevice_get_descriptor(struct usbdevice *, uint16_t , uint16_t,
    uint32_t, void *);
bool usbdevice_get_config_descriptor(struct usbdevice *, uint16_t,
    usb_config_descriptor_t *);
bool usbdevice_get_device_descriptor(struct usbdevice *,
    usb_device_descriptor_t *);
struct list *usb_get_devlist(void);
void usb_devlist_init(void);
void usb_reset_addresses(void);


#endif /* devices/usbdevice.h */
