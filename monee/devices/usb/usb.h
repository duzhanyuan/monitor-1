#ifndef DEVICES_USB_H
#define DEVICES_USB_H

#include "threads/synch.h"
#include "devices/usb/usb_core.h"

enum usb_pipetype {
  PIPE_ISOCHRONOUS,
  PIPE_CONTROL,
  PIPE_BULK,
  PIPE_INTERRUPT
};

typedef struct usb_pipe {
  struct list                     interrupts;
  struct uhci_queue_hdr          *queue;          /* was volatile */
  enum usb_pipetype	              type;

  uint8_t                         fullspeed;
  uint8_t                         dev_addr;
  uint8_t                         endpoint;
  uint8_t                         next_toggle;
  uint16_t                        max_transfer;
  uint8_t                         interval;

  uint8_t                         qhdr_node;
  uint8_t                         qhdr_location;

  uint32_t                        error_code;

  //struct timerequest         *p_Timeout;
  int64_t                         timeout_val;      /* nanoseconds. */
  //struct Task                *p_SigTask;//XXX
  //uint8_t                         p_Signal;
  bool                            completed;
  struct condition                completion_wait;
  struct lock                     mutex;

  struct uhci_transfer_desc      *first_td;         /* was volatile. */
  struct uhci_transfer_desc      *last_td;          /* was volatile. */

  /* A pipe would be a part of ONE of these lists [interrupts, control_ls,
   * control_fs, bulk]. This field is used to implement these lists. */
  struct list_elem           pipels_elem;
} usb_pipe;

typedef struct interrupt_data {
  void (*code)(void *);
  void *data;
  struct list_elem ls_elem;
} interrupt_data;

struct usbhub;
struct uhci_data;
struct usbdevice;

void register_usb_driver(void *driver);
uint8_t usb_alloc_address(const void *driver);
void usb_free_address(void *driver, uint8_t address);
void usb_delay(int64_t milliseconds);
struct usbdevice *usb_new_device(struct uhci_data *bus, struct usbhub *hub,
    bool fast);
void usb_init(void);
void usb_shutdown(void);

#endif  /* devices/usb.h */
