#ifndef PEEP_FORCED_CALLOUTS_H
#define PEEP_FORCED_CALLOUTS_H
#include <stdbool.h>
#include <stdint.h>
#include <types.h>
struct tb_t;

void clear_fcallout_patches(void);
void scan_next_insn(uint8_t const *tc_ptr, uint8_t const **ptr1,
    uint8_t const **ptr2);
void apply_fcallout_patch(uint8_t *ptr1, uint8_t *ptr2);
//void fcallout_patch_pc(target_ulong eip_phys, target_ulong eip_virt);
void handle_pending_interrupts(void);

bool fcallout_already_patched(uint8_t const *tc_ptr);
bool fcallout_patch_exists(void);

void fcallouts_tc_write(uint8_t *tc_ptr, uint8_t val);
bool fcallouts_tb_active(struct tb_t const *tb);

#endif
