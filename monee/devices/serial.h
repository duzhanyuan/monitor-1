#ifndef DEVICES_SERIAL_H
#define DEVICES_SERIAL_H

#include <stdint.h>
#include <stdlib.h>

/* I/O port base address for the first serial port. */
//#define SERIAL_IO_BASE 0x3f8
//#define SERIAL_IRQ     (0x20 + 4)

#define SERIAL_IO_BASE 0x2f8       /* virtual serial port for ILO2 is at this port. */
#define SERIAL_IRQ     (0x20 + 3)

void serial_init_queue (void);
void serial_putc (uint8_t);
void serial_flush (void);
void serial_notify (void);
void serial_write(void *buf, size_t count);

#endif /* devices/serial.h */
