/*
 * QEMU 8259 interrupt controller emulation
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
#include "hw/i8259.h"
#include <stdlib.h>
#include "mem/malloc.h"
#include "mem/swap.h"
#include "sys/vcpu.h"

/* debug PIC */
//#define DEBUG_PIC

//#define DEBUG_IRQ_LATENCY
//#define DEBUG_IRQ_COUNT

typedef struct PicState2 PicState2;

//PicState2 *isa_pic;

#if defined(DEBUG_PIC) || defined (DEBUG_IRQ_COUNT)
static int irq_level[16];
#endif
#ifdef DEBUG_IRQ_COUNT
static uint64_t irq_count[16];
#endif

/* set irq level. If an edge is detected, then the IRR is set to 1 */
static inline void pic_set_irq1(PicState *s, int irq, int level)
{
    int mask;
    mask = 1 << irq;

    if (s->elcr & mask) {
        /* level triggered */
        if (level) {
            s->irr |= mask;
            s->last_irr |= mask;
        } else {
            s->irr &= ~mask;
            s->last_irr &= ~mask;
        }
    } else {
        /* edge triggered */
        if (level) {
            if ((s->last_irr & mask) == 0)
                s->irr |= mask;
            s->last_irr |= mask;
        } else {
            s->last_irr &= ~mask;
        }
    }
}

/* return the highest priority found in mask (highest = smallest
   number). Return 8 if no irq */
static inline int get_priority(PicState *s, int mask)
{
    int priority;
    if (mask == 0)
        return 8;
    priority = 0;
    while ((mask & (1 << ((priority + s->priority_add) & 7))) == 0)
        priority++;
    return priority;
}

/* return the pic wanted interrupt. return -1 if none */
static int pic_get_irq(PicState *s)
{
    int mask, cur_priority, priority;

    mask = s->irr & ~s->imr;
    priority = get_priority(s, mask);
    if (priority == 8) {
        return -1;
    }
    /* compute current priority. If special fully nested mode on the
       master, the IRQ coming from the slave is not taken into account
       for the priority computation. */
    mask = s->isr;
    if (s->special_fully_nested_mode && s == &s->pics_state->pics[0]) {
        mask &= ~(1 << 2);
    }
    cur_priority = get_priority(s, mask);
    if (priority < cur_priority) {
        /* higher priority found: an irq should be generated */
        return (priority + s->priority_add) & 7;
    } else {
        return -1;
    }
}

/* raise irq to CPU if necessary. must be called every time the active
   irq may change */
/* XXX: should not export it, but it is needed for an APIC kludge */
static void pic_update_irq(PicState2 *s)
{
    int irq2, irq;

    /* first look at slave pic */
    irq2 = pic_get_irq(&s->pics[1]);
    if (irq2 >= 0) {
        /* if irq request by slave pic, signal master PIC */
        pic_set_irq1(&s->pics[0], 2, 1);
        //pic_set_irq1(&s->pics[0], 2, 0);//XXX: sorav
    }
    /* look at requested irq */
		/*
    irq = pic_get_irq(&s->pics[0]);
    if (irq >= 0) {
#if defined(DEBUG_PIC)
        {
            int i;
            for(i = 0; i < 2; i++) {
                printf("pic%d: imr=%x irr=%x padd=%d\n", 
                       i, s->pics[i].imr, s->pics[i].irr, 
                       s->pics[i].priority_add);
                
            }
        }
        printf("pic: cpu_interrupt\n");
#endif
        //s->irq_request(s->irq_request_opaque, 1);
    }
		*/
}

#ifdef DEBUG_IRQ_LATENCY
int64_t irq_time[16];
#endif

void
pic_set_irq(struct PicState2 *s, int irq, int level)
{
#if defined(DEBUG_PIC) || defined(DEBUG_IRQ_COUNT)
    if (level != irq_level[irq]) {
#if defined(DEBUG_PIC)
        printf("pic_set_irq: irq=%d level=%d\n", irq, level);
#endif
        irq_level[irq] = level;
#ifdef DEBUG_IRQ_COUNT
	if (level == 1)
	    irq_count[irq]++;
#endif
    }
#endif
#ifdef DEBUG_IRQ_LATENCY
    if (level) {
        irq_time[irq] = qemu_get_clock(vm_clock);
    }
#endif
    pic_set_irq1(&s->pics[irq >> 3], irq & 7, level);
    /* used for IOAPIC irqs */
    if (s->alt_irq_func)
        s->alt_irq_func(s->alt_irq_opaque, irq, level);
    //pic_update_irq(s);
}

/* acknowledge interrupt 'irq' */
static inline void
pic_intack(PicState *s, int irq)
{
	if (s->auto_eoi) {
		if (s->rotate_on_auto_eoi)
			s->priority_add = (irq + 1) & 7;
	} else {
		s->isr |= (1 << irq);
	}
	// We don't clear a level sensitive interrupt here
	if (!(s->elcr & (1 << irq)) /*sorav*/ || 1)
		s->irr &= ~(1 << irq);
}

int
pic_read_irq(PicState2 *s)
{
    int irq, irq2, intno;

    irq = pic_get_irq(&s->pics[0]);
    if (irq >= 0) {
        pic_intack(&s->pics[0], irq);
        if (irq == 2) {
            irq2 = pic_get_irq(&s->pics[1]);
            if (irq2 >= 0) {
                pic_intack(&s->pics[1], irq2);
            } else {
                /* spurious IRQ on slave controller */
                irq2 = 7;
            }
            intno = s->pics[1].irq_base + irq2;
            irq = irq2 + 8;
        } else {
            intno = s->pics[0].irq_base + irq;
        }
    } else {
        /* spurious IRQ on host controller */
        irq = 7;
        intno = s->pics[0].irq_base + irq;
    }
    pic_update_irq(s);
        
#ifdef DEBUG_IRQ_LATENCY
    printf("IRQ%d latency=%0.3fus\n", 
           irq, 
           (double)(qemu_get_clock(vm_clock) - irq_time[irq]) * 1000000.0 / ticks_per_sec);
#endif
#if defined(DEBUG_PIC)
    printf("pic_interrupt: irq=%d\n", irq);
#endif
    return intno;
}

bool
pic_is_spurious_interrupt(struct PicState2 *s, int intno)
{
  if (intno == s->pics[0].irq_base + 7) {
    return true;
  }
  return false;
}

static void
pic_reset(void *opaque)
{
	PicState *s = opaque;

	s->last_irr = 0;
	s->irr = 0;
	s->imr = 0;
	s->isr = 0;
	s->priority_add = 0;
	s->irq_base = 0;
	s->read_reg_select = 0;
	s->poll = 0;
	s->special_mask = 0;
	s->init_state = 0;
	s->auto_eoi = 0;
	s->rotate_on_auto_eoi = 0;
	s->special_fully_nested_mode = 0;
	s->init4 = 0;
	/* Note: ELCR is not reset */
}

static void
pic_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
	PicState *s = opaque;
	int priority, cmd, irq;

#ifdef DEBUG_PIC
	printf("pic_write: addr=0x%02x val=0x%02x\n", addr, val);
#endif
	addr &= 1;
	if (addr == 0) {
		if (val & 0x10) {
			/* init */
			pic_reset(s);
			/* deassert a pending interrupt */
			//printf("%#x: deasserting a pending interrupt.\n", vcpu.eip);
			//s->pics_state->irq_request(s->pics_state->irq_request_opaque, 0);
			s->init_state = 1;
			s->init4 = val & 1;
			if (val & 0x02)
				PANIC("single mode not supported");
			if (val & 0x08)
				PANIC("level sensitive irq not supported");
		} else if (val & 0x08) {
			if (val & 0x04)
				s->poll = 1;
			if (val & 0x02)
				s->read_reg_select = val & 1;
			if (val & 0x40)
				s->special_mask = (val >> 5) & 1;
		} else {
			cmd = val >> 5;
			switch(cmd) {
				case 0:
				case 4:
					s->rotate_on_auto_eoi = cmd >> 2;
					break;
				case 1: /* end of interrupt */
				case 5:
					priority = get_priority(s, s->isr);
					if (priority != 8) {
						irq = (priority + s->priority_add) & 7;
						s->isr &= ~(1 << irq);
						if (cmd == 5)
							s->priority_add = (irq + 1) & 7;
						pic_update_irq(s->pics_state);
					}
					break;
				case 3:
					irq = val & 7;
					s->isr &= ~(1 << irq);
					pic_update_irq(s->pics_state);
					break;
				case 6:
					s->priority_add = (val + 1) & 7;
					pic_update_irq(s->pics_state);
					break;
				case 7:
					irq = val & 7;
					s->isr &= ~(1 << irq);
					s->priority_add = (irq + 1) & 7;
					pic_update_irq(s->pics_state);
					break;
				default:
					/* no operation */
					break;
			}
		}
	} else {
		switch(s->init_state) {
			case 0:
				/* normal mode */
				s->imr = val;
				pic_update_irq(s->pics_state);
				break;
			case 1:
				s->irq_base = val & 0xf8;
				//printf("%s() %d: irq_base=%d\n", __func__, __LINE__, s->irq_base);
				s->init_state = 2;
				break;
			case 2:
				if (s->init4) {
					s->init_state = 3;
				} else {
					s->init_state = 0;
				}
				break;
			case 3:
				s->special_fully_nested_mode = (val >> 4) & 1;
				s->auto_eoi = (val >> 1) & 1;
				s->init_state = 0;
				break;
		}
	}
}

static uint32_t
pic_poll_read (PicState *s, uint32_t addr1)
{
    int ret;

    ret = pic_get_irq(s);
    if (ret >= 0) {
        if (addr1 >> 7) {
            s->pics_state->pics[0].isr &= ~(1 << 2);
            s->pics_state->pics[0].irr &= ~(1 << 2);
        }
        s->irr &= ~(1 << ret);
        s->isr &= ~(1 << ret);
        if (addr1 >> 7 || ret != 2)
            pic_update_irq(s->pics_state);
    } else {
        ret = 0x07;
        pic_update_irq(s->pics_state);
    }

    return ret;
}

static uint32_t
pic_ioport_read(void *opaque, uint32_t addr1)
{
	PicState *s = opaque;
	unsigned int addr;
	int ret;

	addr = addr1;
	addr &= 1;
	if (s->poll) {
		ret = pic_poll_read(s, addr1);
		s->poll = 0;
	} else {
		if (addr == 0) {
			if (s->read_reg_select)
				ret = s->isr;
			else
				ret = s->irr;
		} else {
			ret = s->imr;
		}
	}
#ifdef DEBUG_PIC
	printf("pic_read: addr=0x%02x val=0x%02x\n", addr1, ret);
#endif
	return ret;
}

/* memory mapped interrupt status */
/* XXX: may be the same than pic_read_irq() */
static uint32_t pic_intack_read(PicState2 *s)
{
    int ret;

    //printf("%s() called.\n", __func__);
    ret = pic_poll_read(&s->pics[0], 0x00);
    if (ret == 2)
        ret = pic_poll_read(&s->pics[1], 0x80) + 8;
    /* Prepare for ISR read */
    s->pics[0].read_reg_select = 1;
    
    return ret;
}

static void elcr_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    PicState *s = opaque;
    printf("elcr_ioport_write: addr=0x%02x val=0x%02x\n", addr, val);
    s->elcr = val & s->elcr_mask;
}

static uint32_t elcr_ioport_read(void *opaque, uint32_t addr1)
{
    PicState *s = opaque;
    //printf("elcr_ioport_read: addr=0x%02x val=0x%02x\n", addr1, s->elcr);
    return s->elcr;
}

/* XXX: add generic master/slave system */
static void pic_init1(int io_addr, int elcr_addr, PicState *s)
{
  if (vcpu.replay_log) {
    return;
  }
  register_ioport_write(io_addr, 2, 1, pic_ioport_write, s, true);
  register_ioport_read(io_addr, 2, 1, pic_ioport_read, s, true);
  if (elcr_addr >= 0) {
    register_ioport_write(elcr_addr, 1, 1, elcr_ioport_write, s, true);
    register_ioport_read(elcr_addr, 1, 1, elcr_ioport_read, s, true);
  }
  //qemu_register_reset(pic_reset, s);
}

static void pic_info(struct PicState2 *s2)
{
	PicState *s;
	int i;
	/*
		 if (!vcpu.isa_pic)
		 return;
		 */
	for (i = 0; i < 2; i++) {
		s = &s2->pics[i];
		LOG(HW, "pic%d: irr=%02x imr=%02x isr=%02x hprio=%d irq_base=%02x "
				"rr_sel=%d elcr=%02x fnm=%d\n", i, s->irr, s->imr, s->isr,
				s->priority_add, s->irq_base, s->read_reg_select, s->elcr, 
				s->special_fully_nested_mode);
	}
}

static void irq_info(void)
{
#ifndef DEBUG_IRQ_COUNT
    LOG(HW, "irq statistic code not compiled.\n");
#else
    int i;
    int64_t count;

    LOG(HW, "IRQ statistics:\n");
    for (i = 0; i < 16; i++) {
        count = irq_count[i];
        if (count > 0)
            LOG(HW, "%2d: %" PRId64 "\n", i, count);
    }
#endif
}

static void
__pic_init(struct PicState2 *s)
{
	ASSERT(s);
	pic_init1(0x20, 0x4d0, &s->pics[0]);
	pic_init1(0xa0, 0x4d1, &s->pics[1]);
	s->pics[0].elcr_mask = 0xf8;
	s->pics[1].elcr_mask = 0xde;
	//s->irq_request = irq_request;
	//s->irq_request_opaque = irq_request_opaque;
	s->pics[0].pics_state = s;
	s->pics[1].pics_state = s;
	//return s;
}

#if 0
static void
pic_irq_request(void *opaque, int level)
{
  ASSERT(opaque == NULL);
  if (level) {
    //cpu_interrupt(CPU_INTERRUPT_HARD);
  } else {
    //printf("%s(): calling cpu_reset_interrupt.\n", __func__);
    //cpu_reset_interrupt(CPU_INTERRUPT_HARD);
  }
}
#endif

void
pic_init(struct PicState2 *s)
{
  __pic_init(s);
  pic_reset(&s->pics[0]);
  pic_reset(&s->pics[1]);
  //XXX: make everything level triggered. is it correct (sorav)?
  s->pics[0].elcr = 0xff;
  s->pics[1].elcr = 0xff;
}

static void
pic_set_alt_irq_func(PicState2 *s, SetIRQFunc *alt_irq_func,
                     void *alt_irq_opaque)
{
    s->alt_irq_func = alt_irq_func;
    s->alt_irq_opaque = alt_irq_opaque;
}

bool
pic_is_external_interrupt(struct PicState2 *s, int intno)
{
  if (   intno >= s->pics[0].irq_base
      && intno < s->pics[0].irq_base + 0x10) {
    return true;
  }
  return false;
}

#define pic_rw_state(func, prefix) do {					      							\
	int i;                                                            \
	for (i = 0; i < 2; i++) {                                         \
		func("pic:");                                                   \
		func(" %02hhx", prefix s->pics[i].last_irr);                    \
		func(" %02hhx", prefix s->pics[i].irr);                         \
		func(" %02hhx", prefix s->pics[i].imr);                         \
		func(" %02hhx", prefix s->pics[i].isr);                         \
		func(" %02hhx", prefix s->pics[i].priority_add);                \
		func(" %02hhx", prefix s->pics[i].irq_base);                    \
		func(" %02hhx", prefix s->pics[i].read_reg_select);             \
		func(" %02hhx", prefix s->pics[i].poll);                        \
		func(" %02hhx", prefix s->pics[i].special_mask);                \
		func(" %02hhx", prefix s->pics[i].init_state);                  \
		func(" %02hhx", prefix s->pics[i].auto_eoi);                    \
		func(" %02hhx", prefix s->pics[i].rotate_on_auto_eoi);          \
		func(" %02hhx", prefix s->pics[i].special_fully_nested_mode);   \
		func(" %02hhx", prefix s->pics[i].init4);                       \
		func(" %02hhx", prefix s->pics[i].elcr);                        \
		func(" %02hhx", prefix s->pics[i].elcr_mask);                   \
		func("\n");                                                     \
	}                                                                 \
} while (0)

void
pic_load_state(struct PicState2 *s)
{
	//pic_rw_state(replay_log_scanf, &);
}

void
pic_save_state(struct PicState2 *s)
{
	//pic_rw_state(record_log_printf,);
}

static bool
pic1_states_equal(struct PicState *s1, struct PicState *s2)
{
#define check_field(f) do {																	\
	if (s1->f != s2->f) {                                     \
		return false;                                           \
	}                                                         \
} while(0)
  check_field(last_irr);
	check_field(irr);
	check_field(imr);
	check_field(isr);
	check_field(priority_add);
	check_field(irq_base);
	check_field(read_reg_select);
	check_field(poll);
	check_field(special_mask);
	check_field(init_state);
	check_field(auto_eoi);
	check_field(rotate_on_auto_eoi);
	check_field(special_fully_nested_mode);
	check_field(init4);
	check_field(elcr);
	check_field(elcr_mask);
	return true;
}

bool
pic_states_equal(struct PicState2 *s1, struct PicState2 *s2)
{
	return true;
	/*
	return pic1_states_equal(&s1->pics[0], &s2->pics[0])
		&& pic1_states_equal(&s1->pics[1], &s2->pics[1]);
		*/
}
