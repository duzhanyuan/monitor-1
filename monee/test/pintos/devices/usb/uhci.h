#ifndef DEVICES_UHCI_H
#define DEVICES_UHCI_H

#include <stdint.h>
#include <list.h>
#include "devices/usb/usb.h"

struct interrupt_data;
struct uhci_transfer_desc;
struct uhci_queue_hdr;
struct uhci_data;
struct usb_pipe;
struct usbdevice;
void uhci_init(void);
void ehci_init(void);

struct usb_pipe *uhci_create_pipe(struct uhci_data *uhci,
    enum usb_pipetype type, bool fullspeed, uint8_t addr, uint8_t endp,
    uint8_t period, uint32_t maxp, uint32_t timeout);
void uhci_delete_pipe(struct uhci_data *uhci, struct usb_pipe *pipe);
void uhci_queued_transfer(struct uhci_data *uhci, struct usb_pipe *pipe,
    void *buffer, uint32_t length, bool in);
void uhci_queued_write(struct uhci_data *uhci, struct usb_pipe *pipe,
    void *buffer, uint32_t length);
void uhci_queued_read(struct uhci_data *uhci, struct usb_pipe *pipe,
    void *buffer, uint32_t length);
bool uhci_control_transfer(struct uhci_data *uhci, struct usb_pipe *pipe,
    struct usbdevice_request *request, void *buffer, uint32_t length);
bool uhci_port_reset(struct uhci_data *uhci, uint8_t p);
bool uhci_bulk_transfer(struct uhci_data *uhci, struct usb_pipe *pipe,
    void *buffer, uint32_t length);

bool uhci_add_interrupt(struct uhci_data *uhci, struct usb_pipe *pipe,
    void *buffer, size_t length, struct interrupt_data *interrupt);
bool uhci_remove_interrupt(struct uhci_data *uhci, struct usb_pipe *pipe,
    struct interrupt_data *interrupt);

//void uhci_usbhub_onoff(struct uhci_data *uhci, bool run);
struct usb_pipe *uhci_usbdevice_create_pipe(struct usbdevice *dev,
    enum usb_pipetype type, uint8_t endpoint, uint8_t period,
    uint32_t maxpacket, uint32_t timeout);


void uhci_pipe_set_timeout(struct uhci_data *uhci, struct usb_pipe *pipe,
		int64_t nanoseconds);
bool uhci_get_port_status(struct uhci_data *uhci, uint16_t p, uint16_t *status,
    uint16_t *change);
bool uhci_clear_port_feature(struct uhci_data *uhci, uint16_t p, int feature);
void uhci_shutdown(void);

#endif /* devices/uhci.h */
