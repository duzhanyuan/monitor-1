#include "sys/exception.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <random.h>
#include <string.h>
#include "devices/disk.h"
#include "sys/loader.h"
#include "sys/gdt.h"
#include "sys/interrupt.h"
#include "sys/vcpu.h"
#include "sys/rr_log.h"
#include "threads/thread.h"
#include "peep/cpu_constraints.h"
#include "peep/callouts.h"
#include "peep/tb.h"
#include "mem/mtrace.h"
#include "mem/paging.h"
#include "mem/palloc.h"
#include "mem/pte.h"
#include "mem/pt_mode.h"
#include "mem/vaddr.h"

/* Number of page faults processed. */
static long long num_phys_map_faults = 0, num_true_faults = 0;
static long long num_mtraced_faults = 0, num_shadow_faults = 0;

static void passthrough (struct intr_frame *);
static void page_fault (struct intr_frame *);
static void gpf_handler (struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply passthrough to the guest.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */

void
exception_init (void) 
{
  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
  intr_register_int (13, 0, INTR_ON, gpf_handler,
      "#GP General Protection Exception");
  //intr_register_int (FORCED_CALLOUT, 3, INTR_ON, forced_callout, "#Handler for "
  intr_register_int (FORCED_CALLOUT, 3, INTR_OFF, forced_callout, "#Handler for "
      "patched instruction to force a callout.");
}

/* Handler for an exception (probably) caused by a user process. */
static void
passthrough (struct intr_frame *f) 
{
  bool h;
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs) {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception. Use the guest interrupt
       * handler. */
      LOG (EXCP, "Passthru'ing interrupt %#04x (%s) to the guest. "
          "f->eip=%p. n_exec=%llx\n", f->vec_no, intr_name(f->vec_no), f->eip,
					vcpu.n_exec);
      h = guest_intr_handler(f);
      ASSERT(h);
      return;

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a monitor bug.
         Monitor code shouldn't throw exceptions.  (Page faults
         may cause monitor exceptions--but they shouldn't arrive
         here.)  Panic to make the point.  */
      intr_dump_frame (f);
      PANIC ("Monitor bug - unexpected interrupt in kernel mode"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      PANIC ("Interrupt %#04x (%s) at %#04hx:%p.\n",
          f->vec_no, intr_name (f->vec_no), f->cs, f->eip);
  }
  NOT_REACHED();
}

static void
gpf_handler (struct intr_frame *f)
{
  ASSERT(read_cpl() != 3);

	//printf("%s() %d: f->eip=%p\n", __func__, __LINE__, f->eip);
  if (guest_handle_exception(f, CPU_CONSTRAINT_GPF)) {
    ASSERT(read_cpl() != 3);
    return;
  }
  passthrough(f);
}

static inline void
page_fault_log(struct intr_frame *f, target_ulong fault_addr, char const *type)
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

	LOG(EXCP, "Page fault of type %s at %x (eip %p, esp %p): %s error %s "
			"page in %s context.\n", type, fault_addr, f->eip, f->esp,
			not_present ? "not present" : "rights violation",
			write ? "writing" : "reading",
			user ? "user" : "kernel");
}

static void
page_fault (struct intr_frame *f) 
{
  enum ptwalk_flags_t ptwalk_flags, shadow_ptwalk_flags;
	target_phys_addr_t shadow_paddr;
	uint32_t *pde_entry, *pte_entry, *pde_shadow, *pte_shadow;
  target_ulong fault_addr;  				/* Fault address. */
	target_phys_addr_t paddr;					/* Physical address of fault_addr. */
	bool write_fault;	 /* The fault is due to rights violation during write. */
	bool user_fault;	 /* Present and writable, but fault due to user mode.*/
	bool guest_cr3;		 /* Guest is in protected mode, and is using cr3. */
	bool guest_user;	 /* Guest was in user mode at time of fault. */
	void *cr3;

  asm ("movl %%cr2, %0" : "=r" (fault_addr));
  cr3 = ptov_mon(read_cr3());
	ASSERT(fault_addr < LOADER_MONITOR_VIRT_BASE);

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  write_fault = (f->error_code & PF_P) && (f->error_code & PF_W) != 0;
	user_fault = ((f->error_code & PF_P) && !(f->error_code & PF_W)
			&& (f->error_code & PF_U));

	guest_cr3 = using_cr3_page_table;
	/* Use the current shadow_page_dir to check if it is a user access or a kernel
	 * access. Do not use vcpu_get_privilege_level() because it is possible to
	 * ldl_kernel() when vcpu is in user mode (e.g., interrupts).
	 */
	guest_user = vcpu.shadow_page_dir[1] && cr3 == vcpu.shadow_page_dir[1];

	ASSERT(!user_fault);	/* The shadow pagedirs/phys_map never have a kernel
													 entry. */

  if (cr3 == phys_map) {
		/* The fault occurred on phys map. Handle it here and get done wit it. */
		//page_fault_log(f, fault_addr, "PHYS-MAP");
		num_phys_map_faults++;
    phys_map_install_page(fault_addr & ~PGMASK);
    return;
  }

  ptwalk_flags = PTWALK_SET_A;
  if ((f->error_code & PF_P) && (f->error_code & PF_W)) {
		/* present and write access. */
    ptwalk_flags |= PTWALK_SET_D;
  }
	if (guest_user) {
		ptwalk_flags |= PTWALK_U;
	}
	ASSERT(cr3 == vcpu.shadow_page_dir[0] || cr3 == vcpu.shadow_page_dir[1]);
	paddr = pt_walk((void *)vcpu.cr[3], fault_addr,
			&pde_entry, &pte_entry, ptwalk_flags);
	if (   pde_error(paddr, pde_entry, ptwalk_flags)
			|| pte_error(paddr, pte_entry, ptwalk_flags)) {
		/* true page fault; passthrough to guest. */
		num_true_faults++;
		page_fault_log(f, fault_addr, "TRUE");
		vcpu.exception_cr2 = fault_addr;
		passthrough(f);
		return;
	}

	/* This is a hidden page fault that should not have occurred. Find it's
	 * cause and handle it. */

	shadow_ptwalk_flags = (ptwalk_flags | PTWALK_SHADOW) & ~PTWALK_U;
	shadow_paddr = pt_walk(cr3, fault_addr, &pde_shadow, &pte_shadow,
			shadow_ptwalk_flags);
	ASSERT(   pde_error(shadow_paddr, pde_shadow, shadow_ptwalk_flags)
			   || pte_error(shadow_paddr, pte_shadow, shadow_ptwalk_flags));

	if (   write_fault
			&& pte_error(shadow_paddr, pte_shadow, shadow_ptwalk_flags)
			&& mtraces_handle_page_fault(pte_shadow, fault_addr, paddr, f)) {
		page_fault_log(f, fault_addr, "MTRACED");
		num_mtraced_faults++;
		return;
	}

	num_shadow_faults++;
	page_fault_log(f, fault_addr, "SHADOW");
  shadow_handle_page_fault(fault_addr, pde_entry, pte_entry, shadow_paddr,
			pde_shadow, pte_shadow, shadow_ptwalk_flags, guest_cr3, guest_user);
}

#if 0
  if (!tb_find(f->eip)) {
    target_phys_addr_t paddr, cr3;
    uint32_t *pd, *pde_p = NULL, *pte_p = NULL;

    asm volatile ("movl %%cr3, %0" : "=r" (cr3));
    pd = ptov_mon(cr3);
    if (pd == phys_map) {
      printf("Page fault on phys_map.\n");
    }
    paddr = pt_walk(pd, (target_ulong)fault_addr, &pde_p,
        &pte_p, PTWALK_SHADOW);
    if (paddr == PDE_ERR) {
      printf("%p: pde error: cr3=%x, pde_p=%p, "
          "*pde_p=0x%x\n", fault_addr, cr3, pde_p, *pde_p);
    } else if (paddr == PTE_ERR) {
      printf("pte error: *pde_p=0x%x\n", *pde_p);
      printf("pte error: *pte_p=0x%x\n", *pte_p);
    } else {
      printf("no error: paddr=0x%x, pde_p=%p, *pde_p=0x%x, "
          "pte_p=%p, *pte_p=0x%x.\n", paddr, pde_p, *pde_p,
          pte_p, *pte_p);
    }
		ASSERT(f->vec_no == EXCP0E_PAGE);
		PANIC("invalid page-fault.\n");
  }


  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */

  /* lookup page tables to convert fault_addr to a physical address. */
  if (   (   fault_addr >= LOADER_MONITOR_VIRT_BASE
          && fault_addr < LOADER_MONITOR_VIRT_BASE + MONITOR_SIZE)
      || (   phys_addr >= LOADER_MONITOR_BASE
          && phys_addr < LOADER_MONITOR_BASE + MONITOR_SIZE)) {
    /* ASSERT(faulting instruction != control flow transfer). */
    /* read the page containing the physical address into memory and convert
       the physical address to a new virtual address. */
    /* construct a new instruction sequence containing the faulting insn but
       with the new virtual address (corresponding to the machine address). The
       temporary register used (if any) should be properly saved and restored.
       If GS prefix is used, then GS should be properly saved and restored. If
       a REP prefix is present, it should be removed and returned to the caller.
       The new instruction sequence should be terminated with a jump back to
       sequence_done. */
    /* load cpu from frame f, except %gs. */
    /* jmp %gs:*new_instruction_sequence. */
    /* sequence_done: store cpu back to frame f. */
    /* update f->eip (if REP prefix was seen, update ecx and flags, before
       returning). */
    /* return. */
  }
  /*
  if (write && is_writable(orig_pt, fault_addr)) {
    ASSERT(!is_writable(mon_pt, fault_addr));
    ASSERT(is_page_table_access(fault_addr));
    update orig and shadow page tables.
  }
  */
#endif

void
exception_print_stats(void)
{
	long long num_page_faults;
	num_page_faults = num_phys_map_faults + num_true_faults + num_mtraced_faults 
		+ num_shadow_faults;
	printf("MON-STATS: exception: %lld page-faults (%lld t, %lld m, %lld s, "
			"%lld p).\n",
			num_page_faults, num_true_faults, num_mtraced_faults,
			num_shadow_faults, num_phys_map_faults);
}
