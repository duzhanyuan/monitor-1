#include "peep/peep.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <types.h>
#include "threads/thread.h"
#include "mem/palloc.h"
#include "mem/malloc.h"
#include "mem/vaddr.h"
#include "peep/i386-dis.h"
#include "peep/insntypes.h"
#include "peep/insn.h"
#include "peep/peeptab.h"
#include "peep/assignments.h"
#include "peep/regset.h"
#include "peep/tb.h"
#include "peep/cpu_constraints.h"
#include "peep/debug.h"
#include "peep/sti_fallthrough.h"
#include "sys/bootsector.h"
#include "sys/gdt.h"
#include "sys/monitor.h"
#include "sys/vcpu.h"

/* For debugging purposes. */
static peep_entry_t const *cur_peep_entry = NULL;

typedef enum {
  peep_null = 0,
#define CONCAT(a,b) a ## b
#define _DEF(p, x) CONCAT(p, x),
#define DEF(x) _DEF(PEEP_PREFIX, x)
#include "peepgen_defs.h"
#undef DEF
#undef _DEF
#undef CONCAT
} peepgen_label_t;

char const *peep_label_str[] = {
  "null",
#define DEF(x) #x,
#include "peepgen_defs.h"
};

size_t peepgen_code(peepgen_label_t label, long *peep_param_buf,
		uint8_t *gen_code_buf, uint16_t *edge_offset, uint16_t *jmp_offset,
		rollbacks_t *rollbacks, long cur_addr, long fallthrough_addr, int is_terminating);

#include "peepgen_gencode.h"

peep_entry_t peep_tab_entries[] = {
#include "peepgen_entries.h"
};

#include "gen_snippets.h"

struct hash peep_tab;
int max_tu_size;

static inline unsigned
hash_insns(size_t n_insns, insn_t const *insns)
{
  unsigned ret;
  unsigned i, j;

  ret = n_insns*313;
  for (i = 0; i < n_insns; i++) {
    ret += insns[i].opc*1601;
    for (j = 0; j < 3; j++) {
      ret += ((int)insns[i].op[j].type)*487;
    }
  }
  return ret;
}

static unsigned
peep_entry_hash(struct hash_elem const *elem, void *aux)
{
  struct peep_entry_t const *entry;

  entry = hash_entry(elem, struct peep_entry_t, peeptab_elem);
  return hash_insns(entry->n_tmpl, entry->tmpl);
}

static bool
peep_entry_equal(struct hash_elem const *ea, struct hash_elem const *eb,
    void *aux)
{
  /* We always insert unique entries. */
  return false;
}



/* Matching functions. */

/* macros to be used for disp and seg.desc fields of memory operands. */
#define mem_match(templ, op, memfield, fmem) do {                             \
  if (templ->tag.mem.memfield == tag_const) {                                 \
    if (op->val.mem.memfield != templ->val.mem.memfield) {                    \
      DBE(MATCH, printf("      const " #memfield " mismatch.\n"));        \
      return false;                                                           \
    }                                                                         \
  } else {                                                                    \
    operand_t _op, _templ, tmp_op;                                            \
                                                                              \
    _op.type = _templ.type = op_##fmem;                                       \
    _op.size = _templ.size = (_op.type == op_imm)?0:op->val.mem.addrsize;     \
                                                                              \
    _op.val.fmem = op->val.mem.memfield;                                      \
    _op.tag.fmem = op->tag.mem.memfield;                                      \
    _templ.val.fmem = templ->val.mem.memfield;                                \
    _templ.tag.fmem = templ->tag.mem.memfield;                                \
                                                                              \
    if (   _op.type == op_reg                                                 \
        && (uint16_t)op->val.mem.memfield == (uint16_t)-1) {                  \
      return false;                                                           \
    }                                                                         \
                                                                              \
    if (assignments_getval(assignments, &_templ, &tmp_op)) {                  \
      if (!operands_equal(&tmp_op, &_op)) {                                   \
        DBE(MATCH, printf("      " #memfield " var!=const mismatch\n"));  \
        return false;                                                         \
      }                                                                       \
    } else {                                                                  \
      if (!assignment_is_coherent(&_templ, &_op)) {                           \
        DBE(MATCH, printf("      " #memfield " constraints not "          \
              "satisfied.\n"));                                               \
        return false;                                                         \
      }                                                                       \
      assignments_add(&tmp_assignments, &_templ, &_op);                       \
    }                                                                         \
  }                                                                           \
} while (0)

static bool
mem_operands_match(operand_t const *op, operand_t const *templ,
    assignments_t *assignments)
{
  static assignments_t tmp_assignments;

  ASSERT(op->type == op_mem);
  ASSERT(templ->type == op_mem);
  ASSERT(templ->tag.all == tag_const);

  if (op->val.mem.addrsize != templ->val.mem.addrsize) {
    DBE(MATCH, printf("    mem operands : addrsize mismatch.\n"));
    return false;
  }
  if (op->val.mem.segtype != templ->val.mem.segtype) {
    DBE(MATCH, printf("    mem operands : segtype mismatch.\n"));
    return false;
  }

  assignments_init(&tmp_assignments);

  if (op->val.mem.segtype == segtype_sel) {
    mem_match(templ, op, seg.sel, seg);
  } else {
    mem_match(templ, op, seg.desc, imm);
  }

  mem_match(templ, op, base, reg);
  mem_match(templ, op, index, reg);
  if (templ->val.mem.scale != op->val.mem.scale) {
    return false;
  }
  mem_match(templ, op, disp, imm);

  append_assignments(assignments, &tmp_assignments);
  return true;
}

static bool
operands_match(operand_t const *op, operand_t const *templ,
  assignments_t *assignments)
{
  if (op->type != templ->type) {
    DBE(MATCH, printf("    type mismatch (%d <=> %d).\n", op->type,
            templ->type));
    return false;
  }
  if (op->size != templ->size) {
    DBE(MATCH, printf("    op->size != templ->size.\n"));
    if (op->size != 0) {
      DBE(MATCH, printf("    size mismatch (%d <=> %d).\n", op->size,
              templ->size));
      return false;
    }
    /* op->size == 0 means that the constant is size-agnostic, while the
     * variable might be size-sensitive.
     */
    switch (op->type) {
      case op_imm:
        if (op->val.imm < (1ULL << (templ->size*8))) {
          assignments_add(assignments, templ, op);
          return true;
        } else {
          DBE(MATCH, printf("    imm size mismatch (templ size %d, "
                  "constant %llu).\n", templ->size, op->val.imm));
          return false;
        }
      default:
        printf("\nop->size = %d. templ->size = %d. op->type = %d\n",
            op->size, templ->size, op->type);
        ASSERT(0);
    }
  }
  if (op->type == invalid) {
    return true;
  }

  if (templ->tag.all == tag_const) {
    if (op->type != op_mem) {
      if (!operands_equal(templ, op)) {
        static char templ_buf[64], op_buf[64];
        operand2str(templ, templ_buf, sizeof templ_buf);
        operand2str(op, op_buf, sizeof op_buf);
        DBE(MATCH, printf("    cons operands mismatch (%s <=> %s).\n",
              templ_buf, op_buf));
        return false;
      }
      return true;
    } else {
      DBE(MATCH, printf("    calling mem_operands_match().\n"));
      if (!mem_operands_match(op, templ, assignments)) {
        DBE(MATCH, printf("    mem operands mismatch.\n"));
        return false;
      }
      DBE(MATCH, printf("    mem_operands_match() returned true.\n"));
      return true;
    }
  } else {
    operand_t tmp_op;

    if (assignments_getval(assignments, templ, &tmp_op)) {
      if (!operands_equal(&tmp_op, op)) {
        DBE(MATCH, printf("    template mismatch for variable value.\n"));
        return false;
      }
    } else {
      if (assignment_is_coherent(templ, op)) {
        assignments_add(assignments, templ, op);
        return true;
      } else {
        DBE(MATCH, printf("    constant not coherent with variable.\n"));
        return false;
      }
    }
  }
	NOT_REACHED();
}

static bool
templ_matches_insn(insn_t *insn, insn_t *tmpl, assignments_t *assignments)
{
  static char insn_buf[128];
  int i;

  if (insn->opc != tmpl->opc) {
    DBE(MATCH_ALL, printf("    opcodes mismatch (%s <=> %s).\n",
          opctable_name(insn->opc), opctable_name(tmpl->opc)));
    return false;
  }
  DBE(cur_peep_entry_is_debug(), insn2str(insn, insn_buf, sizeof insn_buf));
  DBE(cur_peep_entry_is_debug(), printf("Translating %s:\n", insn_buf));
  DBE(cur_peep_entry_is_debug(), printf("  checking with %s:\n",
        cur_peep_entry->name));

  DBE(MATCH, printf("    opcodes match. checking operands.\n"));
  for (i = 0; i < 3; i++) {
    if (!operands_match(&insn->op[i], &tmpl->op[i], assignments)) {
      DBE(MATCH, printf("    operand #%d mismatch.\n", i));
      return false;
    }
  }
  return true;
}

static bool
templ_matches_insns(insn_t *insns, int n_insns, insn_t *templ, int n_templ,
    assignments_t *assignments)
{
  int i;
  assignments_init(assignments);     /* start with a clean assignments array. */

  if (n_insns != n_templ) {
    DBE(MATCH_ALL, printf("    n_insns != n_templ.\n"));
    return false;
  }
  for (i = 0; i < n_insns; i++) {
    if (!templ_matches_insn(&insns[i], &templ[i], assignments)) {
      DBE(MATCH_ALL, printf("    failed on insn #%d\n", i));
      return false;
    }
  }
  return true;
}

static bool
is_prefix_byte(unsigned char b)
{
  switch (b) {
    case 0xf0:  /* lock  */
    case 0xf2:  /* repne */
    case 0xf3:  /* repe  */
    case 0x2e:  /* cs    */
    case 0x36:  /* ss    */
    case 0x3e:  /* ds    */
    case 0x26:  /* es    */
    case 0x64:  /* fs    */
    case 0x65:  /* gs    */
    case 0x66:  /* osize */
    case 0x67:  /* asize */
      return true;
    default:
      return false;
  }
}

static size_t
vpeepgen_emit_code(uint8_t *buf, size_t buf_size, peepgen_label_t label,
    int n_args, va_list args)
{
  long params[n_args];
  char *ptr = buf, *end = buf + buf_size;
  int i;

  for (i = 0; i < n_args; i++) {
    params[i] = va_arg(args, long);
  }
  ptr += peepgen_code(label, params, ptr, NULL, NULL, NULL, 0, 0, 0);
  ASSERT(ptr <= end);

  return (ptr - (char *)buf);
}

static size_t
peepgen_emit_code(uint8_t *buf, size_t buf_size, peepgen_label_t label,
    int n_args, ...)
{
  uint8_t *ptr = buf, *end = buf + buf_size;
  va_list args;

  va_start(args, n_args);
  ptr += vpeepgen_emit_code(ptr, end - ptr, label, n_args, args);
  va_end(args);
  return ptr - buf;
}

static size_t
convert_insn_prefixes(char *out_buf, size_t out_buf_size,
    char const *in_buf, size_t in_buf_size, bool *addr16,
		bool output_addr_is_monitor_addr, size_t *n_prefix_bytes)
{
#define CS_PREFIX_OPCODE 0x2e
#define DS_PREFIX_OPCODE 0x3e
#define ES_PREFIX_OPCODE 0x26
#define FS_PREFIX_OPCODE 0x64
#define GS_PREFIX_OPCODE 0x65
#define SS_PREFIX_OPCODE 0x36
  char prefix_buf[16];
  bool operand_size_prefix_seen = false, address_size_prefix_seen = false;
  char *prefix_ptr = prefix_buf, *prefix_end = prefix_ptr + sizeof prefix_buf;
  char const *ptr = in_buf, *end = in_buf + in_buf_size;
  char *optr = out_buf, *oend = out_buf + out_buf_size;

  while (ptr < end && is_prefix_byte(*ptr)) {
    if (*ptr == 0x66) {
      operand_size_prefix_seen = true;
    } else if (*ptr == 0x67) {
      address_size_prefix_seen = true;
    } else if (   output_addr_is_monitor_addr
               &&
                  (   *ptr == CS_PREFIX_OPCODE
                   || *ptr == DS_PREFIX_OPCODE
                   || *ptr == ES_PREFIX_OPCODE
                   || *ptr == FS_PREFIX_OPCODE
                   || *ptr == GS_PREFIX_OPCODE
                   || *ptr == SS_PREFIX_OPCODE)) {
      /* ignore this byte. */
    } else {
      *prefix_ptr++ = *ptr;
    }
    ptr++;
  }
  ASSERT(prefix_ptr < prefix_end);
	if (*addr16 != operand_size_prefix_seen) {
    *optr++ = 0x66;
  }
  if (!output_addr_is_monitor_addr && *addr16 != address_size_prefix_seen) {
		/* The output should also access 16-bit memory address. */
    *optr++ = 0x67;
  }
  if (output_addr_is_monitor_addr) {
    *optr ++ = 0x65;    /* gs prefix. */
  }
	if (address_size_prefix_seen) {
		*addr16 = !*addr16;
	}

  memcpy(optr, prefix_buf, prefix_ptr - (char *)prefix_buf);
  optr += prefix_ptr - (char *)prefix_buf;

  *n_prefix_bytes = ptr - in_buf;
  return optr - out_buf;
}

static size_t
get_opcode(uint8_t const *buf, int *opcode)
{
  if (*buf == 0x0f) {
    *opcode = (0x1 << 8) | *(buf + 1);
    return 2;
  } else {
    *opcode = *buf;
    return 1;
  }
}

static bool
is_accumulator_opcode(int opc)
{
  if (opc >= 0xa0 && opc < 0xa4) {
    return true;
  }
  return false;
}

#if 0
static bool
is_string_opcode(int opc)
{
	if (opc >= 0xa4 && opc < 0xa8) {
		return true;
	}
	if (opc >= 0xaa && opc < 0xb0) {
		return true;
	}
	return false;
}
#endif

static bool
temp_assignment_violates_nomatch_pairs(int tempno, int reg,
    assignments_t const *assignments, nomatch_pair_t const *nomatch_pairs,
    int num_nomatch_pairs)
{
  int reg_indices[assignments->num_assignments];
  int i, num_regs, n;
  //printf("checking temp%d with %d\n", tempno, reg);
  for (n = 0; n < num_nomatch_pairs; n++) {
    int otherval = -1;
    tag_t othertag = tag_const;
    if (   nomatch_pairs[n].reg1 == TEMP_REG0 + tempno
        && nomatch_pairs[n].tag1 == tag_var) {
      otherval = nomatch_pairs[n].reg2;
      othertag = nomatch_pairs[n].tag2;
    }
    if (   nomatch_pairs[n].reg2 == TEMP_REG0 + tempno
        && nomatch_pairs[n].tag2 == tag_var) {
      otherval = nomatch_pairs[n].reg1;
      othertag = nomatch_pairs[n].tag1;
    }
    if (otherval == -1) {
      continue;
    }
    if (othertag == tag_const) {
      if (otherval == reg) {
        return true;
      }
    } else {
      if (otherval < TEMP_REG0) { /* ignore temps. they are checked earlier. */
        //printf("checking for vreg %d\n", otherval);
        num_regs = assignments_get_regs(reg_indices, assignments);
        for (i = 0; i < num_regs; i++) {
          int r = reg_indices[i];
          ASSERT(assignments->arr[r].var.type == op_reg);
          ASSERT(assignments->arr[r].val.type == op_reg);
          ASSERT(assignments->arr[r].var.tag.reg != tag_const);
          ASSERT(assignments->arr[r].val.tag.reg == tag_const);
          ASSERT(assignments->arr[r].var.val.reg < TEMP_REG0);
          //printf("checking with %d\n", assignments->arr[r].var.val.reg);
          if (otherval == assignments->arr[r].var.val.reg) {
            /*printf("reg=%d, assignments->arr[r].val.val.reg=%d\n",
                reg, assignments->arr[r].val.val.reg);*/
            if (reg == assignments->arr[r].val.val.reg) {
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

static bool
find_temporary_regs(int *temporaries, int n_temporaries,
    tag_t const *temporary_tags, assignments_t const *assignments,
    nomatch_pair_t const *nomatch_pairs, int num_nomatch_pairs)
{
  char temp_regs[NUM_REGS];
  int j;
  memset(temp_regs, 0x0, sizeof temp_regs);
  for (j = 0; j < n_temporaries; j++) {
    int r;
    for (r = NUM_REGS - 1; r >= 0; r--) {
      operand_t opcons = {op_reg, 4, 0, {.reg=r}, {.reg=tag_const}};
      operand_t opvar = {op_reg, 4, 0, {.reg=0},
        {.reg=temporary_tags?temporary_tags[j]:tag_var}};
      if (   assignment_is_coherent(&opvar, &opcons)
          && !temp_regs[r]
          && !temp_assignment_violates_nomatch_pairs(j, r, assignments,
            nomatch_pairs, num_nomatch_pairs)) {
        temp_regs[r] = 1;
        break;
      }
    }
    if (r < 0) {
      int i;
      printf("Looking for %d'th temporary:\n", j);
      for (i = 0; i < NUM_REGS; i++) {
        operand_t opcons = {op_reg, 4, 0, {.reg=i}, {.reg=tag_const}};
        operand_t opvar = {op_reg, 4, 0, {.reg=0},
          {.reg=temporary_tags[j]}};
        printf("    reg %d: %s %s %s\n", i, temp_regs[i]?"temp":"",
            assignment_is_coherent(&opvar, &opcons)?"":"not-coherent",
            temp_assignment_violates_nomatch_pairs(j, i, assignments,
              nomatch_pairs, num_nomatch_pairs)?"violates-nomatch-pairs":"");
      }
      return false;
    }
    ASSERT(j < MAX_TEMPORARIES);
    ASSERT(r >= 0 && r < NUM_REGS);
    temporaries[j] = r;
  }
  return true;
}

size_t
rename_mem_operands_to_disps(uint8_t *obuf, size_t obuf_size,
		uint8_t const *buf, target_ulong disp0, target_ulong disp1, bool addr16)
{
  uint8_t const *ptr = buf, *end;
  uint8_t *optr = obuf, *oend = obuf + obuf_size;
  size_t n_prefix_bytes, n_opcode_bytes, disp_bytes, i;
  uint8_t modrm_byte, new_modrm_byte, sib_byte, mod, nnn, rm;
	static uint8_t prefixes[16];
	size_t prefixes_out_bytes;
  target_ulong scratch_reg;
  static regset_t use, def;
  static insn_t insn;
  int opc, disas;
  int temporary;

  disas = disas_insn(ptr, (target_ulong)ptr, &insn, addr16?2:4, false);
  ASSERT(disas > 0);
	end = buf + disas;
  insn_get_usedef(&insn, &use, &def);
  regset_union(&use, &def);
  temporary = regset_find_unused(&use);
  ASSERT(temporary >= 0 && temporary < NUM_REGS);
	regset_mark(&use, tag_const, temporary);

  optr += peepgen_emit_code(optr, oend - optr, peep_snippet_save_reg, 2,
      temporary, (target_ulong)&scratch_reg);
  ASSERT(optr < oend);
  optr += peepgen_emit_code(optr, oend - optr, peep_snippet_mov_mem_to_reg, 2,
      temporary, (target_ulong)disp0);
  ASSERT(optr < oend);
	prefixes_out_bytes = convert_insn_prefixes(prefixes, sizeof prefixes, ptr,
			end - ptr, &addr16, true, &n_prefix_bytes);
  ASSERT(optr < oend);

  ptr += n_prefix_bytes;
  n_opcode_bytes = get_opcode(ptr, &opc);
  if (is_accumulator_opcode(opc)) {
		peepgen_label_t accumulator_snippet;
		ASSERT(temporary != R_EAX);
		ptr += n_opcode_bytes;
		switch (opc) {
			case 0xa0: accumulator_snippet = peep_snippet_movb_regaddr_to_al; break;
			case 0xa1: if (addr16) {
									 accumulator_snippet = peep_snippet_movw_regaddr_to_ax;
								 } else {
									 accumulator_snippet = peep_snippet_movl_regaddr_to_eax;
								 }
								 break;
			case 0xa2: accumulator_snippet = peep_snippet_movb_al_to_regaddr; break;
			case 0xa3: if (addr16) {
									 accumulator_snippet = peep_snippet_movw_ax_to_regaddr;
								 } else {
									 accumulator_snippet = peep_snippet_movl_eax_to_regaddr;
								 }
								 break;
			default: NOT_REACHED();
		}
		optr += peepgen_emit_code(optr, oend - optr, accumulator_snippet, 1,
				temporary);
		ptr += addr16?2:4;
		goto done;
  }
	if (insn_is_string_op(&insn)) {
		peepgen_label_t string_snippet;
		target_ulong scratch2;
		int temp2;
		ASSERT(temporary != R_EDI);
		ptr += n_opcode_bytes;
		switch (opc) {
			case 0xa4:
			case 0xa5:
				//NOT_TESTED();
				temp2 = regset_find_unused(&use);
				ASSERT(temp2 >= 0 && temp2 < NUM_REGS);
				ASSERT(temp2 != R_ESI && temp2 != R_EDI);
				ASSERT(temporary != R_ESI);
				ASSERT(disp1);
				if (opc == 0xa4) {
				  string_snippet = peep_snippet_movb_dispaddr_to_regaddr;
				} else if (addr16) {
					string_snippet = peep_snippet_movw_dispaddr_to_regaddr;
				} else {
					string_snippet = peep_snippet_movl_dispaddr_to_regaddr;
				}
				optr += peepgen_emit_code(optr, oend - optr, peep_snippet_save_reg, 2,
						temp2, (target_ulong)&scratch2);
				ASSERT(optr < oend);
		    optr += peepgen_emit_code(optr, oend - optr, string_snippet, 3,
				    temporary, temp2, disp1);
				ASSERT(optr < oend);
				optr += peepgen_emit_code(optr, oend - optr, peep_snippet_load_reg, 2,
						temp2, (target_ulong)&scratch2);
				ASSERT(optr < oend);
				break;
			case 0xaa:
			case 0xab:
			  if (opc == 0xaa) {
				  string_snippet = peep_snippet_movb_al_to_regaddr;
				} else if (addr16) {
					string_snippet = peep_snippet_movw_ax_to_regaddr;
				} else {
					string_snippet = peep_snippet_movl_eax_to_regaddr;
				}
		    optr += peepgen_emit_code(optr, oend - optr, string_snippet, 1,
				    temporary);
				break;
			default:
				printf("opc=%x\n", opc);
				NOT_IMPLEMENTED();
		}
		goto done;
	}
	memcpy(optr, prefixes, prefixes_out_bytes);
	optr += prefixes_out_bytes;
  for (i = 0; i < n_opcode_bytes; i++) {
    *optr++ = *ptr++;
  }
  ASSERT(optr < oend);
  modrm_byte = *ptr++;
  mod = modrm_byte & 0xc0;
  nnn = (modrm_byte & 0x38) >> 3;
  rm = modrm_byte & 0x7;
  ASSERT(mod != 0xc0);
	disp_bytes = 0;
	if (!addr16 && rm == 4) {
		uint8_t sib_byte;
		sib_byte = *ptr++;
		if (mod == 0x0 && (sib_byte & 0x7) == 0x5) {
			disp_bytes = 4;
		}
	}
	if (mod == 0x80) {
		disp_bytes = addr16?2:4;
	} else if (mod == 0x40) {
		disp_bytes = 1;
	} else {
		if (addr16 && mod == 0x0 && rm == 0x6) {
			disp_bytes = 2;
		} else if (!addr16 && mod == 0x0 && rm == 0x5) {
			disp_bytes = 4;
		}
	}
  ptr += disp_bytes;

  new_modrm_byte = 0x00 | (nnn << 3) | 0x4;
  *optr++ = new_modrm_byte;
  sib_byte = (0x4 << 3) | temporary;
  *optr++ = sib_byte;
  /* copy the rest of the bytes, as is. */
  while (ptr < end) {
    *optr++ = *ptr++;
  }
done:
  ASSERT(optr < oend);
  optr += peepgen_emit_code(optr, oend - optr, peep_snippet_load_reg, 2,
      temporary, (target_ulong)&scratch_reg);
  ASSERT(optr < oend);
  return optr - obuf;
}

static bool
operand_accesses_cs_gs(operand_t const *op)
{
  ASSERT(op->type == op_mem);
  if (   op->val.mem.segtype == segtype_sel
      && (op->val.mem.seg.sel == R_GS || op->val.mem.seg.sel == R_CS)
      && op->tag.mem.seg.sel == tag_const) {
    return true;
  }
  return false;
}

#define MAX_PEEP_STRING_SIZE  80

static size_t
mode_translate(void *out_buf, size_t out_buf_size, void *in_buf,
    size_t in_buf_len, insn_t const *insn,
    cpu_constraints_t const *cpu_constraints, char *peep_string)
{
  char const *ptr = (char const *)in_buf, *end = ptr + in_buf_len;
  char *optr = (char *)out_buf, *oend = optr + out_buf_size;
  size_t n_prefix_bytes;
  operand_t const *op;
  char *peep_string_ptr = peep_string;
	bool addr16;

  if (*cpu_constraints & CPU_CONSTRAINT_PROTECTED) {
    ASSERT(in_buf_len <= out_buf_size);
		size_t i;
		for (i = 0; i < in_buf_len; i++) {
			*optr++ = ldub((target_ulong)ptr);
			ptr++;
		}
    //memcpy(out_buf, in_buf, in_buf_len);
    return in_buf_len;
  }

  ASSERT(*cpu_constraints & CPU_CONSTRAINT_REAL);

  if (insn_accesses_mem16(insn, &op, NULL) && operand_accesses_cs_gs(op)) {
    assignments_t assignments;
    int temporaries[2];
    uint32_t mem16addr;
		static uint8_t ptr_copy[32];
		size_t i;
    ASSERT(op->val.mem.segtype == segtype_sel);

    assignments_init(&assignments);
    if (op->val.mem.base != -1) {
      operand_t vr0, r0;
      vr0.type = r0.type = op_reg;
      vr0.val.reg = 0;              r0.val.reg = op->val.mem.base;
      vr0.tag.reg = tag_var;        r0.tag.reg = tag_const;
      vr0.size = 4;                 r0.size = 4;
      assignments_add(&assignments, &vr0, &r0);
    }

    if (op->val.mem.index != -1) {
      operand_t vr1, r1;
      vr1.type = r1.type = op_reg;
      vr1.val.reg = 0;              r1.val.reg = op->val.mem.index;
      vr1.tag.reg = tag_var;        r1.tag.reg = tag_const;
      vr1.size = 4;                 r1.size = 4;
      assignments_add(&assignments, &vr1, &r1);
    }

    find_temporary_regs(temporaries, 2, peep_snippet_read_mem16_temporary_tags,
        &assignments, peep_snippet_read_mem16_nomatch_pairs,
        peep_snippet_read_mem16_num_nomatch_pairs);
    peep_string_ptr += snprintf(peep_string_ptr, MAX_PEEP_STRING_SIZE,
        "mode16mem %d %d [%d] [%d] %d 0x%lx %p", op->val.mem.base,
        op->val.mem.index, temporaries[0], temporaries[1],
        op->val.mem.seg.sel, (long)op->val.mem.disp, &mem16addr);
    mem16addr = 0;
    ASSERT(optr < oend);
    optr += peepgen_emit_code(optr, oend - optr, peep_snippet_read_mem16, 7,
        op->val.mem.base, op->val.mem.index, temporaries[0], temporaries[1],
        op->val.mem.seg.sel, (long)op->val.mem.disp, &mem16addr);
    ASSERT(optr < oend);
		for (i = 0; i < in_buf_len; i++) {
			ptr_copy[i] = ldub((target_ulong)ptr+i);
		}
		/* No need to specify disp1 in call to rename_mem_operands_to_disps()
		 * because no string instruction can have cs, gs segments. */
    optr += rename_mem_operands_to_disps(optr, oend - optr, ptr_copy,
				(target_ulong)&mem16addr, 0, true);
    ASSERT(optr < oend);
    return (optr - (char *)out_buf);
  }
  ASSERT(optr < oend);
	addr16 = true;
  optr += convert_insn_prefixes(optr, oend - optr, ptr, end - ptr, &addr16,
      false, &n_prefix_bytes);
  ASSERT(optr < oend);
  ptr += n_prefix_bytes;
  while (ptr < end) {
    *optr++ = *ptr++;
  }
  ASSERT(optr - (char *)out_buf <= (int)out_buf_size);
  return (optr - (char *)out_buf);
}

static void
peepgen_exec(peepgen_label_t label, int n_args, ...)
{
  char buf[64];
  char *ptr = buf, *end = buf + sizeof buf;
  va_list args;

  va_start(args, n_args);
  ptr += vpeepgen_emit_code(ptr, end - ptr, label, n_args, args);
  va_end(args);

  ptr += vpeepgen_emit_code(ptr, end - ptr, peep_snippet_ret, 0, args);
  ((void (*)(void))buf)();
}

void *
hw_memcpy(void *dst, const void *src, size_t n)
{
  void *ret;
  ret = memcpy(dst, src, n);
  if (memcmp(dst, src, n)) {
    /* could be a rom byte. */
    peepgen_exec(peep_snippet_romwrite_set_dst, 1, dst);
    peepgen_exec(peep_snippet_romwrite_set_src, 1, vtop_mon(src));
    peepgen_exec(peep_snippet_romwrite_set_size, 1, n);
    peepgen_exec(peep_snippet_romwrite_transfer, 0);
    ret = dst;
  }
  return ret;
}

#define PEEP_STRING_SIZE 80

size_t
peep_translate(void *buf, size_t buf_size, insn_t *insns, int n_insns,
    uint16_t *edge_offsets, uint16_t *jmp_offsets, char **rb_buf,
    uint16_t *rb_code_offset, uint16_t *rb_rb_offset, size_t *n_rollbacks,
    target_ulong cur_addr, target_ulong fallthrough_addr, int is_terminating,
    cpu_constraints_t const *cpu_constraints, char *peep_string)
{
  /* static-allocate all array types. */
  static char insns_buf[256];
  static assignments_t assignments;
  peepgen_label_t label;
  static long params[32];
  int n_params = 0;
  static char temp_regs[NUM_REGS];
  rollbacks_t rollbacks, *rb;
  char *ptr = buf, *end = (char *)buf + buf_size;
  struct list *eqlist;
  struct list_elem *e;
  char *peep_string_ptr = peep_string;
  int i;

  DBE(MATCH_ALL, insns2str(insns, n_insns, insns_buf, sizeof insns_buf));
  DBE(MATCH_ALL, printf("\nTranslating %s:\n", insns_buf));

  eqlist = hash_find_bucket_with_hash(&peep_tab, hash_insns(n_insns, insns));
  for (e = list_begin(eqlist); e != list_end(eqlist); e = list_next(e)) {
    struct peep_entry_t *entry;
    struct hash_elem *elem;
    /* convert list_elem to hash_elem. */
    elem = list_entry(e, struct hash_elem, list_elem);
    /* convert hash_elem to peep_entry_t. */
    entry = hash_entry(elem, peep_entry_t, peeptab_elem);
    cur_peep_entry = entry;
    DBE(MATCH_ALL, printf("  checking with %s:\n", entry->name));
    if ((*cpu_constraints & entry->cpu_constraints) != *cpu_constraints) {
      DBE(MATCH_ALL, printf("  cpu_constraints mismatch. %#llx is not "
            "contained in %#llx\n", *cpu_constraints, entry->cpu_constraints));
      continue;
    }
    if (templ_matches_insns(insns, n_insns, entry->tmpl, entry->n_tmpl,
          &assignments)) {
      int temporaries[entry->n_temporaries];
      unsigned j, num_reg_params = 0;

      DBE(MATCH, printf("  MATCHED!\n"));
      label = entry->label;

      if (peep_string) {
        peep_string_ptr += snprintf(peep_string_ptr, MAX_PEEP_STRING_SIZE,
            "\t: %s ", peep_label_str[(size_t)label]);
      }
      for (j = 0; (int)j < assignments.num_assignments; j++) {
        params[n_params++] = operand_get_value(&assignments.arr[j].val);
        if (peep_string) {
          peep_string_ptr += snprintf(peep_string_ptr, MAX_PEEP_STRING_SIZE,
              " %lx", params[n_params - 1]);
        }
        if (assignments.arr[j].var.type == op_reg) {
          ASSERT(assignments.arr[j].var.type == assignments.arr[j].val.type);
          ASSERT(params[n_params-1] == assignments.arr[j].val.val.reg);
          num_reg_params++;
        }
      }

      if (find_temporary_regs(temporaries, entry->n_temporaries,
            entry->temporaries, &assignments,
            entry->nomatch_pairs, entry->num_nomatch_pairs) == false) {
        ERR("Too many registers used in peephole rule %s [num_reg_params=%d, "
            "num_temporaries=%d].\n", peep_label_str[(size_t)label],
            num_reg_params, entry->n_temporaries);
      }
      for (j = 0; j < entry->n_temporaries; j++) {
        ASSERT(temporaries[j] >= 0 && temporaries[j] < NUM_REGS);
        params[n_params++] = temporaries[j];
        if (peep_string) {
          peep_string_ptr += snprintf(peep_string_ptr, MAX_PEEP_STRING_SIZE,
              " [%lx]", params[n_params - 1]);
        }
      }


      if (rb_buf) {
        rollbacks.buf = *rb_buf;
        rollbacks.code_offset = rb_code_offset;
        rollbacks.rb_offset = rb_rb_offset;
        rb = &rollbacks;
      } else {
        rb = NULL;
      }
      ASSERT(!rb || rb->buf);
      ptr += peepgen_code(label, params, ptr, edge_offsets, jmp_offsets,
          rb, cur_addr, fallthrough_addr, is_terminating);
      if (rb_buf) {
        *rb_buf = *rb_buf + rollbacks.buf_size;
				/*
        if (rollbacks.buf_size) {
          *rb_buf += emit_jump_indir_insn(*rb_buf,
              (target_ulong)&vcpu.func_monitor_eip);
        }
				*/
        ASSERT(n_rollbacks);
        *n_rollbacks = rollbacks.nb_rollbacks;
      }
			if (ptr > end) {
				PANIC("ptr=%p, end=%p, end - buf=%x, ptr - buf=%x. "
						"peep_string='%s', label=%d\n", ptr, end,
						end - (char *)buf, ptr - (char *)buf, peep_string, label);
			}
      ASSERT(ptr <= end);
      return ptr - (char *)buf;
    }
    DBE(MATCH, printf("  not matched. hash=%#x!\n",
          peep_entry_hash(&entry->peeptab_elem, NULL)));
    cur_peep_entry = NULL;
  }
  return 0;
}

static size_t
emit_tb_header(uint8_t *obuf, uint8_t *oend, size_t n_exec,
		long fallthrough_addr)
{
  static long params[4];
  uint8_t *optr = obuf;

  if (vcpu.record_log || vcpu.replay_log) {
    params[0] = n_exec;
    optr += peepgen_code(peep_snippet_increment_vcpu_n_exec, params, optr, NULL,
        NULL, NULL, fallthrough_addr, fallthrough_addr, 0);

    if (rr_log_lockstep_mode() || vcpu.replay_log) {
      params[0] = n_exec;
      optr += peepgen_code(peep_snippet_callout_rr_log_vcpu_state, params, optr,
          NULL, NULL, NULL, fallthrough_addr, fallthrough_addr, 0);
    }
  }

  return (optr - obuf);
}

static bool
pattern_match(char const *obuf, ssize_t osize, char const *pat, ssize_t size)
{
	ssize_t i;
	for (i = 0; i < osize - size; i++) {
		bool mismatch_found = false;
		ssize_t j;
		for (j = 0; j < size; j++) {
			if (obuf[i + j] != pat[j]) {
				mismatch_found = true;
				break;
			}
		}
		if (!mismatch_found) {
			return true;
		}
	}
	return false;
}

static bool
translation_contains_jump_to_monitor(uint8_t const *obuf, size_t osize)
{
	char buf[64];
	char *ptr;
	size_t size;

	size = peepgen_code(peep_snippet_jump_to_monitor, NULL, buf, NULL, NULL,
			NULL, 0, 0, 0);

	if (pattern_match(obuf, osize, buf, size)) {
		return true;
	} else {
		return false;
	}
}

size_t
translate(uint8_t *code, target_ulong eip_virt, void *tpage, size_t tpage_size,
    rollbacks_t *rollbacks, size_t *tb_len, uint16_t *edge_offsets,
    uint16_t *jmp_offsets, uint8_t *eip_boundaries, uint16_t *tc_boundaries,
    char **peep_string, size_t *num_insns,
    cpu_constraints_t const *cpu_constraints)
{
  uint8_t *ptr = code, *ptr_next;
  char *optr, *oend, *rbptr, *rr_log_ptr, *rr_log_end;
  static insn_t insns[MAX_TU_SIZE];
  int n_in = 0, peep;
  void *translated_code;
  size_t tlen, n_insns_off;
  static long params[4];
  static uint16_t edge_offset[2], jmp_offset[2];
  int n_params = 0, disas;
  target_ulong cur_addr, fallthrough_addr;
  static char tmp_peep_string[MAX_PEEP_STRING_SIZE];
  bool is_terminating, is_sti_fallthrough_addr;
	cpu_constraints_t constraints;
  char *rb_buf;

  ASSERT(rollbacks);

  jmp_offset[0] = jmp_offset[1] = edge_offset[0] = edge_offset[1] = 0xffff;

  /* use 2 for real mode, 4 for protected. */
  unsigned size = (vcpu.segs[R_CS].flags & DESC_B_MASK)?4:2;

  optr = tpage;
  oend = (char *)tpage + tpage_size;

  rr_log_ptr = optr;
  optr += emit_tb_header(optr, 0, 0, 0);
  rr_log_end = optr;

  do {
    ASSERT(rollbacks);
    if (rollbacks[n_in].buf) {
      rb_buf = rollbacks[n_in].buf;
    } else {
      /* we just need a dummy memory store. use tpage. */
      rb_buf = tpage;
    }

    if (tc_boundaries) {
      tc_boundaries[n_in] = optr - (char *)tpage;
    }
    rollbacks[n_in].nb_rollbacks = 0;
		cur_addr = (ptr - code) + eip_virt;
    disas = disas_insn(ptr, cur_addr, &insns[n_in], size, true);
    if (!disas) {
			target_ulong addr = (target_ulong)ptr;
      printf("disas failed: size=%d. ptr=%p, ptr[]=%hhx,%hhx,%hhx,%hhx,%hhx,"
          "%hhx,%hhx,%hhx,%hhx,%hhx,%hhx,%hhx\n", size, ptr, ldub(addr),
          ldub(addr+1), ldub(addr+2), ldub(addr+3), ldub(addr+4), ldub(addr+5),
					ldub(addr+6), ldub(addr+7), ldub(addr+8), ldub(addr+9), ldub(addr+10),
					ldub(addr+11), ldub(addr+12));
    }
    ASSERT(disas);
    ptr_next = ptr + disas;
		ASSERT(n_in < max_tu_size);
    if (   insn_is_terminating(&insns[n_in])
				|| n_in == max_tu_size - 1) {
      is_terminating = true;
    } else {
			is_terminating = false;
#if 0
      insn_t tmp_insn;
      uint8_t *ptr_next2;
      /* Check if this is the last instruction that finishes on this page. A
			 * translation block must not span two pages. The only exception is if
			 * the first instruction spans two pages. In other words, all instructions
			 * in a translation block must end on the same page. */
      ptr_next2 = ptr_next + disas_insn(ptr_next, 0, &tmp_insn, size);
      if (n_in > 0 &&
          ((target_ulong)ptr_next2 & ~PGMASK) != ((target_ulong)ptr & ~PGMASK)){
        is_terminating = true;
      } else {
        is_terminating = false;
      }
#endif
    }
    fallthrough_addr = (ptr_next - code) + eip_virt;
    rbptr = rb_buf;
    tmp_peep_string[0] = '\0';
		constraints = *cpu_constraints;
		is_sti_fallthrough_addr = (vcpu.IF==2) || remove_sti_fallthrough_addr(ptr);

		if (is_sti_fallthrough_addr) {
			optr += peepgen_code(peep_snippet_check_IF2_and_set, NULL, optr, NULL,
					NULL, NULL, cur_addr, fallthrough_addr, 0);
			/* XXX: have a way to tell the translator that this translation should
			 * not be chained. It will involve setting edge0 to NULL or something
			 * to that effect.
			 */
		}

    peep = peep_translate(optr, oend - optr, &insns[n_in], 1,
				edge_offset, jmp_offset, &rbptr, rollbacks[n_in].code_offset,
        rollbacks[n_in].rb_offset, &rollbacks[n_in].nb_rollbacks, cur_addr,
        fallthrough_addr, is_terminating, &constraints, tmp_peep_string);
    if (!peep) {
      peep = mode_translate(optr, oend - optr, ptr, ptr_next - ptr,
          &insns[n_in], &constraints, tmp_peep_string);
    }
    rollbacks[n_in].buf_size = rbptr - rb_buf;
#define adjust_offset(offset, curptr, begin) do {                           \
  if (offset != 0xffff) {                                                   \
    offset += (char *)curptr - (char *)begin;                               \
  }                                                                         \
} while(0)
    adjust_offset(edge_offset[0], optr, tpage);
    adjust_offset(edge_offset[1], optr, tpage);

    adjust_offset(jmp_offset[0], optr, tpage);
    adjust_offset(jmp_offset[1], optr, tpage);

		optr += peep;
    ASSERT(optr <= oend);

		if (   is_sti_fallthrough_addr
				&& !translation_contains_jump_to_monitor(optr - peep, peep)) {
			optr += peepgen_code(peep_snippet_callout_nop_if_pending_irq, NULL, optr,
					NULL, NULL, NULL, cur_addr, fallthrough_addr, 0);
			ASSERT(optr <= oend);
		}

    ptr = ptr_next;
    if (eip_boundaries) {
      eip_boundaries[n_in] = ptr - code;
    }
#ifndef NDEBUG
    if (peep_string) {
      if (strlen(tmp_peep_string)) {
        peep_string[n_in] = malloc(strlen(tmp_peep_string) + 1);
				ASSERT(peep_string[n_in]);
        strlcpy(peep_string[n_in], tmp_peep_string, strlen(tmp_peep_string)+1);
      } else {
        peep_string[n_in] = NULL;
      }
    }
#endif
		if (insn_is_sti(&insns[n_in])) {
			add_sti_fallthrough_addr(ptr);
			/* XXX: need to invalidate existing translations of ptr. */
		}
    n_in++;
  } while (!is_terminating);

  if (edge_offsets) {
    edge_offsets[0] = edge_offset[0];
    edge_offsets[1] = edge_offset[1];
  }
  if (jmp_offsets) {
    jmp_offsets[0] = jmp_offset[0];
    jmp_offsets[1] = jmp_offset[1];
  }
  fallthrough_addr = (ptr - code) + eip_virt;
  if (!insn_is_terminating(&insns[n_in - 1])) {
    int size;
    size = peepgen_code(peep_snippet_emit_edge1, params, optr, edge_offset,
        jmp_offset, NULL, fallthrough_addr, fallthrough_addr, 1);
    if (edge_offsets) {
      edge_offsets[1] = edge_offset[1] + (optr - (char *)tpage);
    }
    if (jmp_offsets) {
      jmp_offsets[1] = jmp_offset[1] + (optr - (char *)tpage);
    }
    optr += size;
  }

  /* patch n_insns. */
  emit_tb_header(rr_log_ptr, rr_log_end, n_in, eip_virt);

  if (tb_len) {
    *tb_len = ptr - code;
  }
  if (num_insns) {
    *num_insns = n_in;
  }

  ASSERT(optr <= oend);
  tlen = optr - (char *)tpage;
  ASSERT(tlen <= tpage_size);

  if (tc_boundaries) {
    tc_boundaries[n_in] = optr - (char *)tpage;
  }

  translated_code = tpage;
  return tlen;
}

void
peep_init(void)
{
  unsigned i;
  size_t peeptab_size = sizeof peep_tab_entries/sizeof peep_tab_entries[0];
  hash_init_size(&peep_tab, max(256U, peeptab_size), peep_entry_hash,
      peep_entry_equal, NULL);
  for (i = 0; i < peeptab_size; i++) {
    hash_insert(&peep_tab, &peep_tab_entries[i].peeptab_elem);
  }
}

void
set_max_tu_size(int size)
{
  ASSERT(size > 0 && size <= MAX_TU_SIZE);
  max_tu_size = size;
}

size_t
emit_jump_indir_insn(uint8_t *optr, target_ulong target)
{
  long params[1];
  params[0] = target;
  size_t size;

  size = peepgen_code(peep_snippet_jump_indir_insn, params, optr, NULL, NULL,
      NULL, 0, 0, 1);
  return size;
}
