#include "sys/vcpu.h"
#include "sys/monitor.h"
/* dummy, so that linker does not complain */
struct vcpu_t vcpu;					/* vcpu should be at least this big. */
struct monitor_t monitor;	  /* same for monitor. */
struct monitor_t *last_monitor_context;
char rr_interrupt;
char replay_handle_pending_interrupts;
char guest_intr_handler;
struct PicState2;
int pic_read_irq(struct PicState2 *s);
char forced_callout;
char guest_handle_exception;
char shadow_handle_page_fault;
char mtraces_handle_page_fault;
uint32_t ldl_kernel_dont_set_access_bit(target_ulong vaddr) {return 0;}
char tb_find;
char pt_walk;
uint32_t *phys_map;
char read_cr3;
char chr_driver_open;
char uart_init;
char shadow_pagedir_sync;
int vcpu_get_privilege_level(void) {return 0;}
char rr_log_vcpu_state;
char pde_error, pte_error, phys_map_install_page;
