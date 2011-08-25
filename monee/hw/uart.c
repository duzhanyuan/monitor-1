/*
 * QEMU 16450 UART emulation
 * 
 * Copyright (c) 2003-2004 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw/uart.h"
#include <types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/serial.h"
#include "mem/malloc.h"
#include "hw/chr_driver.h"
#include "sys/vcpu.h"
//#define DEBUG_SERIAL

#define UART_LCR_DLAB	0x80	/* Divisor latch access bit */

#define UART_IER_MSI	0x08	/* Enable Modem status interrupt */
#define UART_IER_RLSI	0x04	/* Enable receiver line status interrupt */
#define UART_IER_THRI	0x02	/* Enable Transmitter holding register int. */
#define UART_IER_RDI	0x01	/* Enable receiver data interrupt */

#define UART_IIR_NO_INT	0x01	/* No interrupts pending */
#define UART_IIR_ID	0x06	/* Mask for the interrupt ID */

#define UART_IIR_MSI	0x00	/* Modem status interrupt */
#define UART_IIR_THRI	0x02	/* Transmitter holding register empty */
#define UART_IIR_RDI	0x04	/* Receiver data interrupt */
#define UART_IIR_RLSI	0x06	/* Receiver line status interrupt */

/*
 * These are the definitions for the Modem Control Register
 */
#define UART_MCR_LOOP	0x10	/* Enable loopback test mode */
#define UART_MCR_OUT2	0x08	/* Out2 complement */
#define UART_MCR_OUT1	0x04	/* Out1 complement */
#define UART_MCR_RTS	0x02	/* RTS complement */
#define UART_MCR_DTR	0x01	/* DTR complement */

/*
 * These are the definitions for the Modem Status Register
 */
#define UART_MSR_DCD	0x80	/* Data Carrier Detect */
#define UART_MSR_RI	0x40	/* Ring Indicator */
#define UART_MSR_DSR	0x20	/* Data Set Ready */
#define UART_MSR_CTS	0x10	/* Clear to Send */
#define UART_MSR_DDCD	0x08	/* Delta DCD */
#define UART_MSR_TERI	0x04	/* Trailing edge ring indicator */
#define UART_MSR_DDSR	0x02	/* Delta DSR */
#define UART_MSR_DCTS	0x01	/* Delta CTS */
#define UART_MSR_ANY_DELTA 0x0F	/* Any of the delta bits! */

#define UART_LSR_TEMT	0x40	/* Transmitter empty */
#define UART_LSR_THRE	0x20	/* Transmit-hold-register empty */
#define UART_LSR_BI	0x10	/* Break interrupt indicator */
#define UART_LSR_FE	0x08	/* Frame error indicator */
#define UART_LSR_PE	0x04	/* Parity error indicator */
#define UART_LSR_OE	0x02	/* Overrun error indicator */
#define UART_LSR_DR	0x01	/* Receiver data ready */

struct SerialState {
    uint16_t divider;
    uint8_t rbr; /* receive register */
    uint8_t ier;
    uint8_t iir; /* read only */
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr; /* read only */
    uint8_t msr; /* read only */
    uint8_t scr;
    /* NOTE: this hidden state is necessary for tx irq generation as
       it can be reset while reading iir */
    int thr_ipending;
    SetIRQFunc *set_irq;
    void *irq_opaque;
    int irq;
    CharDriverState *chr;
    int last_break_enable;
    target_ulong base;
    int it_shift;
};

typedef struct SerialState SerialState;

static void set_irq(void *opaque, int irq_num, int level);

static void
serial_update_irq(SerialState *s)
{
  if ((s->lsr & UART_LSR_DR) && (s->ier & UART_IER_RDI)) {
    s->iir = UART_IIR_RDI;
  } else if (s->thr_ipending && (s->ier & UART_IER_THRI)) {
    s->iir = UART_IIR_THRI;
  } else {
    s->iir = UART_IIR_NO_INT;
  }
  if (s->iir != UART_IIR_NO_INT) {
    s->set_irq(s->irq_opaque, s->irq, 1);
  } else {
    s->set_irq(s->irq_opaque, s->irq, 0);
  }
}

static void serial_update_parameters(SerialState *s)
{
    int speed, parity, data_bits, stop_bits;
    SerialSetParams ssp;

    if (s->lcr & 0x08) {
        if (s->lcr & 0x10)
            parity = 'E';
        else
            parity = 'O';
    } else {
            parity = 'N';
    }
    if (s->lcr & 0x04) 
        stop_bits = 2;
    else
        stop_bits = 1;
    data_bits = (s->lcr & 0x03) + 5;
    if (s->divider == 0)
        return;
    speed = 115200 / s->divider;
    ssp.speed = speed;
    ssp.parity = parity;
    ssp.data_bits = data_bits;
    ssp.stop_bits = stop_bits;
    chr_driver_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);
#if 0
    printf("speed=%d parity=%c data=%d stop=%d\n", 
           speed, parity, data_bits, stop_bits);
#endif
}

static void serial_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    SerialState *s = opaque;
    unsigned char ch;
    
    addr &= 7;
#ifdef DEBUG_SERIAL
    printf("serial: write addr=0x%02x val=0x%02x\n", addr, val);
#endif
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0xff00) | val;
            serial_update_parameters(s);
        } else {
            s->thr_ipending = 0;
            s->lsr &= ~UART_LSR_THRE;
            serial_update_irq(s);
            ch = val;
            chr_driver_write(s->chr, &ch, 1);
            s->thr_ipending = 1;
            s->lsr |= UART_LSR_THRE;
            s->lsr |= UART_LSR_TEMT;
            serial_update_irq(s);
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0x00ff) | (val << 8);
            serial_update_parameters(s);
        } else {
            s->ier = val & 0x0f;
            if (s->lsr & UART_LSR_THRE) {
                s->thr_ipending = 1;
            }
            serial_update_irq(s);
        }
        break;
    case 2:
        break;
    case 3:
        {
            int break_enable;
            s->lcr = val;
            serial_update_parameters(s);
            break_enable = (val >> 6) & 1;
            if (break_enable != s->last_break_enable) {
                s->last_break_enable = break_enable;
                chr_driver_ioctl(s->chr, CHR_IOCTL_SERIAL_SET_BREAK, 
                               &break_enable);
            }
        }
        break;
    case 4:
        s->mcr = val & 0x1f;
        break;
    case 5:
        break;
    case 6:
        break;
    case 7:
        s->scr = val;
        break;
    }
}

static uint32_t serial_ioport_read(void *opaque, uint32_t addr)
{
    SerialState *s = opaque;
    uint32_t ret;

    addr &= 7;
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            ret = s->divider & 0xff; 
        } else {
            ret = s->rbr;
            s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
            serial_update_irq(s);
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            ret = (s->divider >> 8) & 0xff;
        } else {
            ret = s->ier;
        }
        break;
    case 2:
        ret = s->iir;
        /* reset THR pending bit */
        if ((ret & 0x7) == UART_IIR_THRI)
            s->thr_ipending = 0;
        serial_update_irq(s);
        break;
    case 3:
        ret = s->lcr;
        break;
    case 4:
        ret = s->mcr;
        break;
    case 5:
        ret = s->lsr;
        break;
    case 6:
        if (s->mcr & UART_MCR_LOOP) {
            /* in loopback, the modem output pins are connected to the
               inputs */
            ret = (s->mcr & 0x0c) << 4;
            ret |= (s->mcr & 0x02) << 3;
            ret |= (s->mcr & 0x01) << 5;
        } else {
            ret = s->msr;
        }
        break;
    case 7:
        ret = s->scr;
        break;
    }
#ifdef DEBUG_SERIAL
    printf("serial: read addr=0x%02x val=0x%02x\n", addr, ret);
#endif
    return ret;
}

static int serial_can_receive(SerialState *s)
{
    return !(s->lsr & UART_LSR_DR);
}

static void serial_receive_byte(SerialState *s, int ch)
{
    s->rbr = ch;
    s->lsr |= UART_LSR_DR;
    serial_update_irq(s);
}

static void serial_receive_break(SerialState *s)
{
    s->rbr = 0;
    s->lsr |= UART_LSR_BI | UART_LSR_DR;
    serial_update_irq(s);
}

static int serial_can_receive1(void *opaque)
{
    SerialState *s = opaque;
    return serial_can_receive(s);
}

static void serial_receive1(void *opaque, const uint8_t *buf, int size)
{
    SerialState *s = opaque;
    serial_receive_byte(s, buf[0]);
}

static void serial_event(void *opaque, int event)
{
    SerialState *s = opaque;
    if (event == CHR_EVENT_BREAK)
        serial_receive_break(s);
}

/* If fd is zero, it means that the serial device uses the console */
void uart_init(void)
{
  SerialState *s;
  int base, irq;
  CharDriverState *chr;

  base = SERIAL_IO_BASE;
  irq = SERIAL_IRQ;
  chr = chr_driver_open("null");

  s = malloc(sizeof(SerialState));
  if (!s) {
    return;
  }
  memset(s, 0, sizeof *s);
  s->set_irq = set_irq;
  s->irq_opaque = NULL; //opaque;
  s->irq = irq - 0x20;
  s->lsr = UART_LSR_TEMT | UART_LSR_THRE;
  s->iir = UART_IIR_NO_INT;
  s->msr = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;

  register_ioport_write(base, 8, 1, serial_ioport_write, s, true);
  register_ioport_read(base, 8, 1, serial_ioport_read, s, true);
  s->chr = chr;
  chr_driver_add_handlers(chr, serial_can_receive1, serial_receive1,
      serial_event, s);
}

static void
set_irq(void *opaque, int irq_num, int level)
{
  ASSERT(opaque == NULL);
  if (level == 1) {
    //printf("%s: setting CPU_INTERRUPT_HARD\n", __FILE__);
    cpu_interrupt(CPU_INTERRUPT_HARD);
  }
  pic_set_irq(&vcpu.isa_pic, irq_num, level);
}
