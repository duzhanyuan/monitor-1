#include "mem/simulate_insn.h"
#include <debug.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mem/paging.h"
#include "mem/palloc.h"
#include "mem/vaddr.h"
#include "peep/cpu_constraints.h"
#include "peep/insn.h"
#include "sys/flags.h"
#include "sys/vcpu.h"

#define SIM_INSN 2

static target_ulong
intr_frame_get_regval(struct intr_frame const *f, int reg)
{
	switch (reg) {
		case R_EAX: return f->eax;
		case R_ECX: return f->ecx;
		case R_EDX: return f->edx;
		case R_EBX: return f->ebx;
		case R_ESP: return (target_ulong)f->esp;
		case R_EBP: return f->ebp;
		case R_ESI: return f->esi;
		case R_EDI: return f->edi;
		default: ABORT();
	}
}

static uint16_t
intr_frame_get_segval(struct intr_frame const *f, int segno)
{
	ASSERT(segno >= 0 && segno < NUM_SEGS);
	switch (segno) {
		case R_ES: return f->es;
		case R_CS: return f->cs;
		case R_SS: return f->ss;
		case R_DS: return f->ds;
		case R_FS: return f->fs;
		case R_GS: return f->gs;
		default: ABORT();
	}
}

static target_ulong
operand_evaluate_on_intr_frame(operand_t const *op, struct intr_frame const *f)
{
	target_ulong ret = 0;
	uint32_t e1, e2;
	uint16_t segsel;

	switch(op->type) {
		case op_mem:
			ASSERT(op->tag.all == tag_const);
			ASSERT(op->tag.mem.seg.all == tag_const);
			ASSERT(op->tag.mem.base == tag_const);
			ASSERT(op->tag.mem.index == tag_const);
			ASSERT(op->tag.mem.disp == tag_const);
			if (op->val.mem.segtype == segtype_desc) {
				NOT_REACHED();
			}
			if (op->val.mem.base != -1) {
				ret += intr_frame_get_regval(f, op->val.mem.base);
			}
			if (op->val.mem.index != -1) {
				ret += intr_frame_get_regval(f, op->val.mem.index) * op->val.mem.scale;
			}
			ret += op->val.mem.disp;
			segsel = intr_frame_get_segval(f, op->val.mem.seg.sel);
			read_segment(&e1, &e2, segsel, true, false);
			if (ret >= (unsigned)get_seg_limit(e1, e2)) {
				NOT_REACHED();
			}
			ret += get_seg_base(e1, e2);
			break;
		default:
			NOT_REACHED();
	}
	return ret;
}

static void
insn_advance_eip_on_intr_frame(struct insn_t const *insn, size_t ilen,
		struct intr_frame *f)
{
	bool string_op = false;
	bool string_termination_condition_reached = false;
	if (insn_is_string_op(insn)) {
		operand_t const *prefixop;
		size_t size;
		bool uses_esi;
		unsigned addrsize;
		addrsize = insn_get_addr_size(insn);

		if (addrsize == 2) {
			/* XXX: handle the case when the addrsize is
			 * 16-bit (use si, di, cx instead). */
			NOT_IMPLEMENTED();
		}
		ASSERT(addrsize == 4);

		size = insn_get_operand_size(insn);
		string_op = true;
		uses_esi = insn_is_movs_or_cmps(insn);
		ASSERT(size == 1 || size == 2 || size == 4);
		if (f->eflags & FLAG_DF) {
			if (uses_esi) {
				f->esi -= size;
			}
			f->edi -= size;
		} else {
			if (uses_esi) {
				f->esi += size;
			}
			f->edi += size;
		}
		if  (prefixop = insn_has_prefix(insn)) {
			ASSERT(prefixop->type == op_prefix);
			ASSERT(prefixop->tag.prefix == tag_const);
			ASSERT(   (prefixop->val.prefix & PREFIX_REPZ)
					|| (prefixop->val.prefix & PREFIX_REPNZ));
			f->ecx--;
			if (   f->ecx == 0) {
				string_termination_condition_reached = true;
			}
		  if (   insn_is_cmps_or_scas(insn)
					&& (  ((prefixop->val.prefix & PREFIX_REPZ)  && !(f->eflags&FLAG_ZF))
					    ||((prefixop->val.prefix&PREFIX_REPNZ)& (f->eflags & FLAG_ZF)))) {
				string_termination_condition_reached = true;
			}
		}
		/*
		printf("%s(): insn_is_string_op=true, uses_esi=%d, f->esi=%x, "
				"f->edi=%x, f->ecx=%x, prefixop=%p, ilen=%d\n", __func__, uses_esi,
				f->esi, f->edi, f->ecx, prefixop, ilen);
				*/
	}
	
	if (!string_op || string_termination_condition_reached) {
		f->eip = (void *)((uint8_t *)f->eip + ilen);
		/*
		if (string_termination_condition_reached) {
			printf("f->eip=%p\n", f->eip);
			serial_flush();
			vcpu_set_log(VCPU_LOG_MTRACE | VCPU_LOG_TRANSLATE |
					VCPU_LOG_EXCP | VCPU_LOG_INT);
		}
		*/
	}
}

static bool
operand_is_monitor_memaddr(operand_t const *op, struct intr_frame const *f)
{
	uint16_t segsel;
	if (op->type != op_mem) {
		return false;
	}
	if (op->val.mem.segtype != segtype_sel) {
		return false;
	}
	if (op->val.mem.seg.sel != R_GS) {
		return false;
	}
	segsel = intr_frame_get_segval(f, op->val.mem.seg.sel);
	if (segsel != SEL_UDSEG) {
		return false;
	}
	ASSERT(operand_evaluate_on_intr_frame(op, f) >= LOADER_MONITOR_VIRT_BASE);
	return true;
}

static bool
ldub_simulate(target_ulong vaddr, bool guest, uint8_t *val)
{
	enum ptwalk_flags_t ptwalk_flags;
	uint32_t *pde = NULL, *pte = NULL;
	bool pde_err, pte_err;
	target_phys_addr_t paddr;

	if (!guest) {
		ASSERT(is_monitor_vaddr((void *)vaddr));
		*val = *(uint8_t *)vaddr;
		return true;
	}

	ptwalk_flags = PTWALK_SET_A;
	if (vcpu_get_privilege_level() == 1) {
		ptwalk_flags |= PTWALK_U;
	}
	paddr = pt_walk((void *)vcpu.cr[3], vaddr, &pde, &pte, ptwalk_flags);
	pde_err = pde_error(paddr, pde, ptwalk_flags);
	pte_err = pde_error(paddr, pte, ptwalk_flags);
	if (pde_err || pte_err) {
		void *fault_addr;
		NOT_TESTED();
		asm ("movl %%cr2, %0" : "=r" (fault_addr));
		printf("%s(): fault_addr=%p, vaddr=%x\n", __func__, fault_addr, vaddr);
		return false;
	}
	*val = ldub_phys(paddr);
	return true;
}

static bool
stb_simulate(target_ulong vaddr, bool guest, uint8_t val)
{
	enum ptwalk_flags_t ptwalk_flags;
	uint32_t *pde = NULL, *pte = NULL;
  target_phys_addr_t paddr;
	bool pde_err, pte_err;

	if (!guest) {
		ASSERT(is_monitor_vaddr((void *)vaddr));
		*(uint8_t *)vaddr = val;
		return true;
	}
	ptwalk_flags = PTWALK_SET_A | PTWALK_SET_D;
	if (vcpu_get_privilege_level() == 1) {
		ptwalk_flags |= PTWALK_U;
	}
  paddr = pt_walk((void *)vcpu.cr[3], vaddr, &pde, &pte, ptwalk_flags);
	pde_err = pde_error(paddr, pde, ptwalk_flags);
	pte_err = pde_error(paddr, pte, ptwalk_flags);
	if (pde_err || pte_err) {
		NOT_TESTED();
		return false;
	}
	stb_phys(paddr, val);
	return true;
}

bool
simulate_faulting_instruction(struct intr_frame *f, size_t *memaccess_size,
		target_ulong *fault_addr)
{
	static uint8_t *scratch = NULL;
	if (!scratch) {
		scratch = palloc_get_page(PAL_ASSERT|PAL_ZERO);
	}
	uint8_t *outptr, *memptr0, *memptr0_copy, *memptr1;
	uint8_t *stack_ptr, *stack_ptr_copy;
	uint8_t *scratch_end = scratch + PGSIZE;
	operand_t const *memop0 = NULL, *memop1 = NULL;
	bool insn_has_memop, insn_touches_stack;
	target_ulong vaddr0 = 0, vaddr1 = 0, stack_addr = 0;
	bool guest0 = false, guest1 = false;
	cpu_constraints_t cpu_constraints;
	bool peep_translated = false;
	unsigned memop_size = 0;
	void *saved_esp = NULL;
	uint16_t saved_ss = 0;
	static insn_t insn;
	void (*eip)(void);
	size_t ilen, tlen;
	unsigned i;

	/* The exception should only occur in user/guest mode. */
	ASSERT((f->cs & 3) == 3);

  ilen = disas_insn((uint8_t *)f->eip, (target_ulong)f->eip, &insn, 4, false);
	cpu_constraints = CPU_CONSTRAINT_SIMULATE;
	if (tlen = peep_translate(scratch, scratch_end - scratch, &insn, 1, NULL,
				NULL, NULL, NULL, NULL, NULL, (target_ulong)f->eip,
				(target_ulong)f->eip + ilen, false, &cpu_constraints, NULL)) {
		printf("SIM IN:\n");
		print_asm(f->eip, 1, 4);
		NOT_IMPLEMENTED();
		peep_translated = true;
		*memaccess_size = 4;
		outptr = (uint8_t *)scratch + tlen;
	} else {
		outptr = scratch;
		memptr0 = scratch + PGSIZE/2;
		memptr0_copy = memptr0 + 8;
		memptr1 = memptr0_copy + 8;
		stack_ptr = memptr1 + 128;
		stack_ptr_copy = stack_ptr + 128;
		ASSERT(stack_ptr_copy <= (uint8_t *)scratch_end);

		insn_has_memop = insn_accesses_mem(&insn, &memop0, &memop1);
		insn_touches_stack = insn_accesses_stack(&insn);
		if (insn_has_memop) {
			ASSERT(memop0);
			memop_size = insn_get_operand_size(&insn);
			ASSERT(memop_size > 0 && memop_size <= 4);
			outptr += rename_mem_operands_to_disps(outptr, PGSIZE/2, (uint8_t*)f->eip,
					(target_ulong)&memptr0, (target_ulong)&memptr1, false);

			if (loglevel & VCPU_LOG_MTRACE) {
				static void *last_eip = NULL;
				if (last_eip != f->eip) {
					printf("SIM IN:\n");
					print_asm(f->eip, 1, 4);
					printf("SIM OUT:\n");
					print_asm(scratch, outptr - scratch, 4);
					last_eip = f->eip;
				}
			}
			vaddr0 = operand_evaluate_on_intr_frame(memop0, f);
			guest0 = !operand_is_monitor_memaddr(memop0, f);
			if (memop1) {
				vaddr1 = operand_evaluate_on_intr_frame(memop1, f);
				guest1 = !operand_is_monitor_memaddr(memop1, f);
			}

			for (i = 0; i < memop_size; i++) {
				if (!ldub_simulate(vaddr0 + i, guest0, &memptr0[i])) {
					//printf("%s(): vaddr0=%x, i=%d\n", __func__, vaddr0, i);
					*fault_addr = vaddr0 + i;
					return false;
				}
				if (memop1) {
					if (!ldub_simulate(vaddr1 + i, guest1, &memptr1[i])) {
						//printf("%s(): vaddr1=%x, i=%d\n", __func__, vaddr1, i);
						*fault_addr = vaddr1 + i;
						return false;
					}
				}
			}
			memcpy(memptr0_copy, memptr0, memop_size);
		}
		if (insn_touches_stack) {
			uint32_t e1, e2;
			int i;

			NOT_TESTED();
			//insn must be push or pop, because call/ret are translated to jumps
			ASSERT(insn_is_push(&insn) || insn_is_pop(&insn));

			if (!insn_has_memop) {
				memcpy(outptr, f->eip, ilen);
				outptr += ilen;
			}
			read_segment(&e1, &e2, f->ss, true, false);
			stack_addr = (target_ulong)f->esp;
			ASSERT((target_ulong)f->esp < (target_ulong)get_seg_limit(e1, e2));
			stack_addr += get_seg_base(e1, e2);
			for (i = 0; i < 32; i++) {
				if (!ldub_simulate(stack_addr + i, true, &stack_ptr[i])) {
					//printf("%s(): stack_addr=%x, i=%d\n", __func__, stack_addr, i);
					*fault_addr = stack_addr + i;
					return false;
				}
				stack_ptr_copy[i] = stack_ptr[i];
			}
			saved_esp = f->esp;
			saved_ss = f->ss;
			f->esp = (void *)stack_ptr;
			f->ss = SEL_UDSEG;
		}
		/*
		if (insn_touches_stack) {
			printf("MTRACE IN:\n");
			print_asm(f->eip, 1, 4);
			printf("MTRACE OUT:\n");
			print_asm(scratch, outptr - scratch, 4);
		}
		*/
		ASSERT(guest0 || guest1 || insn_touches_stack);
		ASSERT(insn_has_memop || insn_touches_stack);
	}
	execute_code_in_intr_frame_context(f, scratch, outptr - scratch);
	insn_advance_eip_on_intr_frame(&insn, ilen, f);
	if (!peep_translated) {
		if (insn_has_memop) {
			bool changed = false;		//to check if memop0 was a write operand
			for (i = 0; i < memop_size; i++) {
				if (memptr0[i] != memptr0_copy[i]) {
					DBGn(SIM_INSN, "%x: %hhx->%hhx [memptr1: %hhx]\n", vaddr0 + i,
							memptr0_copy[i], memptr0[i], memptr1[i]);
				}
				changed = true;
			}

			for (i = 0; changed && i < memop_size; i++) {
				if (!stb_simulate(vaddr0 + i, guest0, memptr0[i])) {
					//printf("%s() %d: vaddr0=%x, i=%d\n", __func__, __LINE__, vaddr0, i);
					*fault_addr = vaddr0 + i;
					return false;
				}
			}
			*memaccess_size = memop_size;
		}
		if (insn_touches_stack) {
			int i;
			for (i = (uint8_t *)f->esp - stack_ptr; i < 0; i++) {
				if (!stb_simulate(stack_addr + i, true, stack_ptr[i])) {
					/* printf("%s() %d: stack_addr=%x, i=%d\n", __func__, __LINE__,
							stack_addr, i); */
					*fault_addr = stack_addr + i;
					return false;
				}
				memop_size = stack_ptr - (uint8_t *)f->esp;
				/* printf("%x: esp %p: %hhx->%hhx\n", stack_addr + i, saved_esp,
						stack_ptr_copy[i], stack_ptr[i]); */
			}
			f->esp = (uint8_t *)saved_esp + ((uint8_t *)f->esp - stack_ptr);
			f->ss = saved_ss;
			*memaccess_size = stack_ptr - (uint8_t *)f->esp;
		}
	} else {
		NOT_REACHED();
	}
	ASSERT(*memaccess_size);
	//rr_log_force_dump_on_next_tb_entry = true;
	return true;
}
