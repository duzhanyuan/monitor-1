#ifndef SYS_CHR_DRIVER_H
#define SYS_CHR_DRIVER_H

#include <stdint.h>
#include "hw/hw.h"

/* character device */

#define CHR_EVENT_BREAK 0 /* serial break char */
#define CHR_EVENT_FOCUS 1 /* focus to this terminal (modal input needed) */
#define CHR_EVENT_RESET 2 /* new connection established */


#define CHR_IOCTL_SERIAL_SET_PARAMS   1
typedef struct {
    int speed;
    int parity;
    int data_bits;
    int stop_bits;
} SerialSetParams;

#define CHR_IOCTL_SERIAL_SET_BREAK    2

#define CHR_IOCTL_PP_READ_DATA        3
#define CHR_IOCTL_PP_WRITE_DATA       4
#define CHR_IOCTL_PP_READ_CONTROL     5
#define CHR_IOCTL_PP_WRITE_CONTROL    6
#define CHR_IOCTL_PP_READ_STATUS      7

typedef struct CharDriverState {
    int (*chr_write)(struct CharDriverState *s, const uint8_t *buf, int len);
    void (*chr_update_read_handler)(struct CharDriverState *s);
    int (*chr_ioctl)(struct CharDriverState *s, int cmd, void *arg);
    IOEventHandler *chr_event;
    IOCanRWHandler *chr_can_read;
    IOReadHandler *chr_read;
    void *handler_opaque;
    void (*chr_send_event)(struct CharDriverState *chr, int event);
    void (*chr_close)(struct CharDriverState *chr);
    void *opaque;
    //QEMUBH *bh;
} CharDriverState;

CharDriverState *chr_driver_open(char const *filename);
void chr_driver_printf(CharDriverState *s, char const *fmt, ...);
int  chr_driver_write(CharDriverState *s, uint8_t const *buf, int len);
void chr_driver_send_event(CharDriverState *s, int event);
void chr_driver_add_handlers(CharDriverState *s, 
                             IOCanRWHandler *fd_can_read, 
                             IOReadHandler *fd_read,
                             IOEventHandler *fd_event,
                             void *opaque);
int  chr_driver_ioctl(CharDriverState *s, int cmd, void *arg);
void chr_driver_reset(CharDriverState *s);
int  chr_driver_can_read(CharDriverState *s);
void chr_driver_read(CharDriverState *s, uint8_t *buf, int len);

#endif
