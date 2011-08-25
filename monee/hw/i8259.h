#ifndef SYS_I8259_H
#define SYS_I8259_H
#include <stdint.h>
#include <stdbool.h>

typedef void SetIRQFunc(void *opaque, int irq_num, int level);
typedef void IRQRequestFunc(void *opaque, int level);

typedef struct PicState {
    uint8_t last_irr; /* edge detection */
    uint8_t irr; /* interrupt request register */
    uint8_t imr; /* interrupt mask register */
    uint8_t isr; /* interrupt service register */
    uint8_t priority_add; /* highest irq priority */
    uint8_t irq_base;
    uint8_t read_reg_select;
    uint8_t poll;
    uint8_t special_mask;
    uint8_t init_state;
    uint8_t auto_eoi;
    uint8_t rotate_on_auto_eoi;
    uint8_t special_fully_nested_mode;
    uint8_t init4; /* true if 4 byte init */
    uint8_t elcr; /* PIIX edge/trigger selection*/
    uint8_t elcr_mask;
    struct PicState2 *pics_state;
} PicState;

struct PicState2 {
    /* 0 is master pic, 1 is slave pic */
    /* XXX: better separation between the two pics */
    PicState pics[2];
    //IRQRequestFunc *irq_request;
    //void *irq_request_opaque;
    /* IOAPIC callback support */
    SetIRQFunc *alt_irq_func;
    void *alt_irq_opaque;
};


void pic_init(struct PicState2 *s);
void pic_set_irq(struct PicState2 *s, int irq, int level);
int pic_read_irq(struct PicState2 *s);
bool pic_is_spurious_interrupt(struct PicState2 *s, int intno);
bool pic_is_external_interrupt(struct PicState2 *s, int intno);

void pic_load_state(struct PicState2 *s);
void pic_save_state(struct PicState2 *s);
bool pic_states_equal(struct PicState2 *s1, struct PicState2 *s2);

#endif    /* sys/i8259.h */
