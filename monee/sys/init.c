#include <stdint.h>
#include <lib/stdio.h>
#include <debug.h>
#include <bxdebug.h>
#include <string.h>
#include "app/micro_replay.h"
#include "hw/bdrv.h"
#include "hw/chr_driver.h"
#include "hw/uart.h"
#include "mem/mtrace.h"
#include "mem/vaddr.h"
#include "mem/palloc.h"
#include "mem/malloc.h"
#include "mem/pte.h"
#include "mem/paging.h"
#include "mem/swap.h"
#include "peepgen_offsets.h"
#include "peep/peeptab_defs.h"
#include "peep/opctable.h"
#include "peep/peep.h"
#include "peep/jumptable2.h"
#include "peep/cpu_constraints.h"
#include "peep/funcs.h"
#include "peep/tb.h"
#include "peep/tb_exit_callbacks.h"
#include "devices/disk.h"
#include "devices/pci.h"
#include "devices/usb/usb.h"
#include "sys/init.h"
#include "sys/gdt.h"
#include "sys/rr_log.h"
#include "sys/tss.h"
#include "sys/flags.h"
#include "sys/intr-stubs.h"
#include "sys/interrupt.h"
#include "sys/vcpu.h"
#include "sys/monitor.h"
#include "threads/thread.h"
#include "threads/synch.h"

bool micro_replay_on = false;

/* Helper functions. */
static void guest_loop(void);

static void
guest_loop(void)
{
  /* These variables need to be static so that they are not optimized by the
   * compiler into registers; as register values get overwritten.
   */
  static rollbacks_t rollbacks[MAX_TU_SIZE];
  static cpu_constraints_t cpu_constraints;
  static target_phys_addr_t eip_phys;
  static size_t tlen, num_insns;
  //static target_ulong saved_eip;
  static void *gen_func = NULL;
  static void *tpage = NULL;
  static target_ulong eip_virt;
  static struct tb_t *tb;
	static void *prev_eip;
  static int sjmp;
  static char chr;

  //mdebug_start(1);
  vcpu.callout_label = &&callout_label;
  tpage = palloc_get_multiple(PAL_ASSERT, 2);

  sjmp = setjmp(vcpu.jmp_env);
  if (sjmp == 1) {
    /* Interrupt. */
    ASSERT(read_cpl() == 3);
    /*LOG(INT, "Interrupt %#02x seen at %p.\n", vcpu.exception_index,
       vcpu.eip);*/
    ASSERT(vcpu.exception_index >= 0);
		vcpu.callout_next = NULL;
		if (vcpu.record_log || vcpu.replay_log) {
			/*
			if (vcpu.exception_index == EXCP0E_PAGE) {
				rr_log_vcpu_state(0);
			}
			*/
		}
		if (vcpu.exception_index == EXCP0E_PAGE) {
			vcpu.cr[2] = vcpu.exception_cr2;
		}
    do_interrupt(vcpu.exception_index,
        vcpu.exception_is_int,
        vcpu.error_code,
        vcpu.exception_next_eip, 0);
		if (vcpu.record_log || vcpu.replay_log) {
			vcpu.n_exec++;
			/*
			if (vcpu.exception_index == EXCP0E_PAGE) {
				rr_log_vcpu_state(0);
			}
			*/
			rr_log_vcpu_state(1);
		}
    vcpu.exception_index = -1;
    gen_func = NULL;
    sjmp = 0;
    //MSG("Interrupt.\n");
  } else if (sjmp == 2) {
    /* Jump from forced_callout(). */
    ASSERT(read_cpl() == 3);
    gen_func = vcpu.callout_next;
    sjmp = 0;
    ASSERT(gen_func);
    //MSG("Forced callout.\n");
	} else if (sjmp == 3) {
		MSG("Micro-replay.\n");
		micro_replay_on = true;
		gen_func = NULL;
  } else {
    //MSG("Normal.\n");
  }
	vcpu.edge = 2;
	vcpu.prev_tb = 0;

  while (1) {
    ASSERT(read_cpl() == 3);
		ASSERT(read_cr3() == vtop_mon(vcpu.shadow_page_dir[0])
				|| read_cr3() == vtop_mon(vcpu.shadow_page_dir[1]));
		ASSERT({
				int user = (vcpu.shadow_page_dir[1]
				    && read_cr3() == vtop_mon(vcpu.shadow_page_dir[1]))?1:0;
				user == vcpu_get_privilege_level();
				});

		if (vcpu.eip_executing != vcpu.eip) {
			prev_eip = vcpu.eip_executing;
		}
		vcpu.eip_executing = vcpu.eip;
		//check_micro_replay();
    if (!gen_func) {
      static target_ulong ptb_eip_phys, ptb_eip_virt;
      static tb_t const *ptb;

      ptb_eip_phys = 0;
      ptb_eip_virt = 0;
      ptb = NULL;
      if (vcpu.edge != 2 && (ptb = tb_find(vcpu.prev_tb))) {
        ptb_eip_phys = ptb->eip_phys;
        ptb_eip_virt = ptb->eip_virt;
      }

			process_tb_exit_callbacks();

      /* Get gen_func from vcpu.eip. */
      eip_virt = vcpu_get_eip();

			if (vcpu.record_log && eip_virt == rr_log_panic_eip) {
				ASSERT(vcpu.n_exec == get_n_exec(vcpu.callout_next));
				printf("%llx: Hit Panic function -- Micro-replaying.\n", vcpu.n_exec);
				micro_replay_switch_mode();
			}

      /* XXX: vcpu_get_eip should also check against cs limit. */
      if ((tb = jumptable2_find(eip_virt, (target_ulong)vcpu.eip)) == NULL) {
				int user = vcpu_get_privilege_level();
				enum ptwalk_flags_t ptwalk_flags;
				uint32_t *pde_entry = NULL, *pte_entry = NULL;
				bool pde_err, pte_err;

        cpu_constraints = CPU_CONSTRAINT_NO_EXCP;
        if (vcpu.cr[0] & CR0_PE_MASK) {
          cpu_constraints |= CPU_CONSTRAINT_PROTECTED;
        } else {
          cpu_constraints |= CPU_CONSTRAINT_REAL;
        }

				ptwalk_flags = PTWALK_SET_A;
				if (user) {
					ptwalk_flags |= PTWALK_U;
				}
        eip_phys = pt_walk((void *)vcpu.cr[3], eip_virt, &pde_entry, &pte_entry,
						ptwalk_flags);
				pde_err = pde_error(eip_phys, pde_entry, ptwalk_flags);
				pte_err = pte_error(eip_phys, pte_entry, ptwalk_flags);

				if (pde_err || pte_err) {
					/* Set tb to null, so that it gets translated. It will then fault
					 * during disas_insn(). */
					tb = NULL;
					ASSERT({
							target_phys_addr_t phys;
							uint32_t *pde_shadow = NULL, *pte_shadow = NULL;
							bool pde_shadow_err, pte_shadow_err;
							enum ptwalk_flags_t ptwalk_flags_shadow;
							void *cr3;
							
							cr3 = ptov_mon(read_cr3());
							ptwalk_flags_shadow = ptwalk_flags | PTWALK_SHADOW;
							ptwalk_flags_shadow &= ~PTWALK_U;
							phys = pt_walk(cr3, eip_virt, &pde_shadow, &pte_shadow,
								ptwalk_flags_shadow);
							pde_shadow_err = pde_error(phys, pde_shadow, ptwalk_flags_shadow);
							pte_shadow_err = pte_error(phys, pte_shadow, ptwalk_flags_shadow);
							if (!pde_shadow_err && !pte_shadow_err) {
							  printf("eip_virt=0x%x, ptwalk_flags_shadow=0x%x, ptwalk_flags=0x%x, eip_phys=%x, pde_entry=%p, pte_entry=%p, phys=%x, pde_shadow=%p, pte_shadow=%p, cr3=%p, vcpu.shadow_pagedir0,1=%p,%p\n", eip_virt, ptwalk_flags_shadow,
									ptwalk_flags, eip_phys, pde_entry, pte_entry, phys, pde_shadow, pte_shadow, cr3, vcpu.shadow_page_dir[0], vcpu.shadow_page_dir[1]);
								if (is_monitor_vaddr(pde_shadow)) {
								  printf("*pde_shadow=%x\n", *pde_shadow);
								}
								if (is_monitor_vaddr(pte_shadow)) {
								  printf("*pte_shadow=%x\n", *pte_shadow);
								}
							}
							pde_shadow_err || pte_shadow_err;
							});
				} else {
					target_phys_addr_t eip_phys_page;
					eip_phys_page = eip_phys & ~PGMASK;
					tb = tb_find_pc(eip_phys, eip_phys_page, eip_virt,
							(target_ulong)vcpu.eip);
				}
        if (!tb) {
					static target_phys_addr_t eip_phys_end_page;
					static size_t tb_len;
          tlen = translate((uint8_t *)eip_virt, (target_ulong)vcpu.eip, tpage,
							2*PGSIZE, rollbacks, &tb_len, NULL, NULL, NULL, NULL, NULL,
							&num_insns, &cpu_constraints);
					eip_phys_end_page = pt_walk((void *)vcpu.cr[3], eip_virt + tb_len - 1,
							&pde_entry, &pte_entry, ptwalk_flags);
					pde_err = pde_error(eip_phys_end_page, pde_entry, ptwalk_flags);
					pte_err = pte_error(eip_phys_end_page, pte_entry, ptwalk_flags);
					ASSERT(!pde_err && !pte_err);
					eip_phys_end_page &= ~PGMASK;
          tb = tb_malloc((target_ulong)vcpu.eip, eip_virt, eip_phys,
						eip_phys_end_page, num_insns, tlen, rollbacks);
          translate((uint8_t *)eip_virt, (target_ulong)vcpu.eip, tb->tc_ptr,
						tlen, tb->rollbacks, &tb->tb_len, tb->edge_offset, tb->jmp_offset,
						tb->eip_boundaries, tb->tc_boundaries,
#ifndef NDEBUG
							//tb->peep_string,		//malloc can fail if peep_string specified.
							NULL,
#else
							NULL,
#endif
							&tb->num_insns,
              &cpu_constraints);
          if (loglevel & VCPU_LOG_TRANSLATE) {
            static unsigned size;
            size = (vcpu.segs[R_CS].flags & DESC_B_MASK)?4:2;
            tb_print_in_asm(tb, size);
            tb_print_out_asm(tb);
            tb_print_rb_asm(tb);
          }
          tb_add(tb);
          if (tb->tb_len <= 0) {
            /*
            printf("tb=%p, tb->eip_phys=0x%x, tb->num_insns=%d, "
                "tb->tc_ptr=%p, tb->tb_len=%d\n", tb, tb->eip_phys,
                tb->num_insns, tb->tc_ptr, tb->tb_len);
                */
          }
          ASSERT(tb->tb_len > 0);   ASSERT(tb->num_insns > 0);
          ASSERT(tb->tc_ptr);

          /* If a callback is registered for this tb, call it. */
          tb_trace_malloced(tb);
          //callout_patches_tb_malloc(tb);
        }
        //pt_mark_accessed((void *)vcpu.cr[3], eip_virt);
        //pt_mark_accessed((void *)vcpu.cr[3], eip_virt + tb->tb_len);
				//printf("%s() %d:\n", __func__, __LINE__);
        jumptable2_add(tb);
				//printf("%s() %d:\n", __func__, __LINE__);
      }
      jumptable1_add((uint32_t)vcpu.eip, (uint32_t)tb->tc_ptr);
      gen_func = tb->tc_ptr;
      if (vcpu.edge != 2) {
        static tb_t *ptb;
				ptb = tb_find(vcpu.prev_tb);
        /* Check if ptb has not been replaced. */
        if (ptb && ptb->eip_phys == ptb_eip_phys
            && ptb->eip_virt == ptb_eip_virt) {
          ASSERT(((unsigned long)ptb & 3) == 0);
          tb_add_jump((void *)ptb, vcpu.edge, tb);
          /* Re-setting the accessed bit while Re-chaining the tb
           * which was unchained by clock or some other program. */
          tb->accessed_bit = true;
        }
        vcpu.prev_tb = 0;
        vcpu.edge = 2;
      }
    }

    if (loglevel & VCPU_LOG_IN_ASM) {
      static unsigned size;
      size = (vcpu.segs[R_CS].flags & DESC_B_MASK)?4:2;
      tb_print_in_asm(tb, size);
    }
    if (loglevel & VCPU_LOG_OUT_ASM) {
      tb_print_out_asm(tb);
    }

    vcpu.callout_next = gen_func;

		if (!vcpu.replay_log) {
			static int orig_interrupt_request; /* needed, only for micro-replays. */
			/* printf("Entering TC: vcpu.eip=%p, gen_func = %p. eax=%#x\n", vcpu.eip,
				 gen_func, vcpu.regs[R_EAX]); */
			if (orig_interrupt_request != -1) {
				vcpu.interrupt_request = orig_interrupt_request;
			}

			if (interrupts_black_listed_eip((target_ulong)vcpu.eip)) {
				orig_interrupt_request = vcpu.interrupt_request;
				vcpu.interrupt_request = 0;
			} else {
				orig_interrupt_request = -1;
			}
			handle_pending_interrupts(gen_func);
		}

    /* There should be no computationally expensive operations between
     * handle_pending_interrupts() and jump_to_tc_if_no_pending_interrupts(). */
    //saved_eip = (target_ulong)vcpu.eip;
    vcpu.callout = NULL;
    //vcpu.eip = NULL;
    vcpu.next_eip_is_set = 0;
    vcpu.tc_ptr = gen_func;
    monitor.eip = vcpu.callout_label;
    barrier();

		last_monitor_context = &monitor;
    /*if (saved_eip == 0x36dd) {
      mdebug_start(10);
    }*/
    //bxdebug_start();
    save_monitor();
    restore_guest();
		save_monitor_flags();
		restore_guest_flags();
    /* check (vcpu.interrupts.pending && vcpu.IF). If true, jump back to
     * monitor. Else jump to translation cache.
     */
    jump_to_tc_if_no_pending_interrupts();
callout_label:
		save_guest_flags();
		restore_monitor_flags();
    save_guest();
    restore_monitor();
    //bxdebug_stop();
    /*if (saved_eip == 0x36dd) {
      mdebug_stop();
    }*/
    barrier();

    //printf("Exited from TC to monitor... vcpu.eip=%p.\n", vcpu.eip);
    ASSERT(intr_get_level() == INTR_ON);
		
		//if (micro_replay_on) printf("%s() %d:\n", __func__, __LINE__);
    if (vcpu.callout) {
      static void *callout_eip;
			static unsigned i;

			//if (micro_replay_on) printf("%s() %d:\n", __func__, __LINE__);
      /* vcpu.eip and vcpu.eip_executing must have been set by the call to
			 * callout function. */
      ASSERT(vcpu.next_eip_is_set);
      callout_eip = vcpu.eip;

      /* Avoid switch statement. For some reason, jumptables do not play
       * well with monitor. */
      if (vcpu.callout_n_args == 0) {
          /*printf("%s() %d: callout0. callout_n_args=%d\n", __func__, __LINE__,
              vcpu.callout_n_args);*/
          ((void (*)(void))vcpu.callout)();
      } else if (vcpu.callout_n_args == 1) {
          //printf("%s() %d: callout1\n", __func__, __LINE__);
          ((void (*)(long))vcpu.callout)(vcpu.callout_args[0]);
      } else if (vcpu.callout_n_args == 2) {
          //printf("%s() %d: callout2\n", __func__, __LINE__);
          ((void (*)(long,long))vcpu.callout)(vcpu.callout_args[0],
            vcpu.callout_args[1]);
      } else if (vcpu.callout_n_args == 3) {
        //printf("%s() %d: callout3\n", __func__, __LINE__);
        ((void (*)(long,long,long))vcpu.callout)(vcpu.callout_args[0],
          vcpu.callout_args[1], vcpu.callout_args[2]);
      } else if (vcpu.callout_n_args == 4) {
        //printf("%s() %d: callout4\n", __func__, __LINE__);
        ((void (*)(long,long,long,long))vcpu.callout)(vcpu.callout_args[0],
          vcpu.callout_args[1], vcpu.callout_args[2], vcpu.callout_args[3]);
      } else {
          ASSERT(0);
      }
      if (callout_eip != vcpu.eip) {
        if (vcpu.callout_next != NULL) {
          printf("vcpu.eip=%p, callout_eip=%p\n", vcpu.eip, callout_eip);
        }
        ASSERT(vcpu.callout_next == NULL);
      }
      gen_func = vcpu.callout_next;
			if (micro_replay_on) {
				/*
				printf("%s() %d: setting micro_replay_on to false.\n", __func__,
						__LINE__);
						*/
				micro_replay_on = false;
			}
    } else {
      /* No callout. */
      if (!vcpu.next_eip_is_set) {
        /* This can happen iff there were pending interrupts at the
         * time of jumping to TC. So restore vcpu.eip.
         */
        ASSERT(gen_func == vcpu.tc_ptr);
				vcpu.edge = 2;
        //ASSERT(vcpu.interrupts.pending);
        //vcpu.eip = (void *)saved_eip;
      } else {
        /* We have reached the end of the tb. */
        gen_func = NULL;
        vcpu.callout_next = NULL;
				if (micro_replay_on) {
					/*
					printf("%s() %d: setting micro_replay_on to false.\n", __func__,
							__LINE__);
							*/
					micro_replay_on = false;
				}
      }
    }
		vcpu.callout_cur = vcpu.callout_next;
  }
}

static void
guest_init(void)
{
  uint32_t e1, e2;
  int i;

  memset(vcpu.regs, 0, sizeof vcpu.regs);
  vcpu.record_log = vcpu.replay_log = NULL;
  vcpu.cr[0] = 0x60000010;
  vcpu.cr[3] = CR3_INVALID;

  vcpu.prev_tb = 0;
  vcpu.edge = 2;
  vcpu.eip = (void *)0x7c00;
  vcpu.n_exec = 0;
  vcpu.eflags = IF_MASK | FLAG_MBS | IOPL_MASK;

  vcpu.segs[R_ES].selector = SEL_GDSEG;
  vcpu.segs[R_FS].selector = SEL_GDSEG;
  vcpu.segs[R_DS].selector = SEL_GDSEG;
  vcpu.segs[R_SS].selector = SEL_GDSEG;

  vcpu.segs[R_GS].selector = SEL_BASE;    /* this value is meaningless. However
                                             it should be >= SEL_BASE for
                                             rr_log. */
  vcpu.segs[R_CS].selector = SEL_GCSEG;

  vcpu.idt.base = 0;
  vcpu.idt.limit = 0xffff;

#define init_seg(seg) do {       \
  vcpu.seg.base = 0;             \
  vcpu.seg.limit = 0xffff;       \
  vcpu.seg.flags = 0;            \
} while(0)
  init_seg(tr);  init_seg(ldt);
  for (i = 0; i < 6; i++) {
    init_seg(segs[i]);
  }
#undef init_seg
  vcpu.default_user_gs = SEL_UDSEG;

  vcpu.tr.base = 0;
  vcpu.ldt.limit = 0xffff;
  vcpu.tr.limit = 0xffff;
  //vcpu.a20_mask = 0xffffffff;
  vcpu.IF = 0;
  vcpu.IOPL = 0;
  //vcpu.sti_fallthrough = 0;

  vcpu.interrupt_request = 0;
  memset(vcpu.fxstate, 0, sizeof vcpu.fxstate);
	vcpu.func_depth = 0;
  //asm("fxsave %0" : "=m"(vcpu.fxstate));
	//
  pic_init(&vcpu.isa_pic);
}

extern size_t ram_pages;

static void
disk_read_test(void)
{
	static char sector[512];
	struct disk *disk;
	int i;

	disk = identify_boot_disk();
	printf("%s() begin: %lld monitor ticks, %lld guest ticks\n", __func__,
			monitor_ticks, guest_ticks);
	for (i = 0; i < 1000; i++) {
		disk_read(disk, 0, 1, sector);
	}
	printf("%s() end: %lld monitor ticks, %lld guest ticks\n", __func__,
			monitor_ticks, guest_ticks);
}

/* Monitor main program. */
int
main(void)
{
  /* Clear BSS and get machine's RAM size. */
  ram_init();

  /* Initialize ourselves as a thread. */
  thread_init();
  synch_init();
  console_init();

  /* Greet user. */
  printf("Monitor booting with %'zu kB RAM...\n", ram_pages*PGSIZE/1024);

  /* Initialize memory system. */
  palloc_init((void *)0xfffff000);    /* last but one page. the last page is
                                         reserved for the stack. */
  malloc_init();

  tss_init();
  gdt_init();

  intr_init();
  timer_init();
  kbd_init();
  input_init();
  exception_init();

  /* Start thread scheduler and enable interrupts. */
  thread_start();
  serial_init_queue();
  timer_calibrate();
  random_init();
  io_init();
  pci_init();
	shutdown_init();

  vcpu_set_log(VCPU_LOG_USB);

  paging_init();

  /* Initialize disks. */
  usb_init();
  disk_init();
  bdrv_init();
  ide_init();
  uart_init();

  swap_init();
	mtrace_init();

	//disk_read_test();

  guest_init();

  /* Log everything. */
	/*
  vcpu_set_log(VCPU_LOG_TRANSLATE | VCPU_LOG_IN_ASM
      | VCPU_LOG_OUT_ASM | VCPU_LOG_INT
      | VCPU_LOG_PCALL | VCPU_LOG_HW | VCPU_LOG_IOPORT);
			*/
	//vcpu_set_log(VCPU_LOG_TRANSLATE);
	//vcpu_set_log(VCPU_LOG_MTRACE);
	//vcpu_set_log(VCPU_LOG_EXCP|VCPU_LOG_MTRACE|VCPU_LOG_PAGING|VCPU_LOG_TB);
	//vcpu_set_log(VCPU_LOG_EXCP);

  MSG("Loading guest...\n");
  ASSERT(hda_bdrv);
  bdrv_read(hda_bdrv, 0, ptov_phy(0x7c00), 1);

  syscall_init();
  opc_init();
  peep_init();
  tb_init();
  jumptable1_init();
  jumptable2_init();

  /* Reset the stack. We leave space for 2*intr_frame_size so that an
   * interrupt received inside a trap-handler does not corrupt the kernel
   * stack. Also, initialize the top of stack with zero so that stack
   * traces get terminated.
   */
  reset_stack();
  switch_to_user(); /* can only be called after reset_stack(). */
  rr_log_init();
	micro_replay_init();
	serial_flush();
  intr_guest_init();
  funcs_init();

  start_tsc = rdtsc();
  guest_loop();
  NOT_REACHED();
}
