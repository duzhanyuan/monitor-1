#include "peep/insn.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "mem/vaddr.h"
#include "peep/debug.h"
#include "peep/opctable.h"
#include "peep/regset.h"
#include "insntypes.h"
#include "assignments.h"
#include "sys/bootsector.h"
#include "sys/vcpu.h"

typedef long long int64;
typedef unsigned long long uint64;

static char pb1[1024];

void
print_insn(insn_t const *insn) {
  if (insn2str(insn, pb1, sizeof pb1)) {
    printf("%s", pb1);
  } else {
    printf("invalid-insn");
  }
}

void
println_insn(insn_t const *insn) {
  if (insn2str(insn, pb1, sizeof pb1)) {
    printf("%s\n", pb1);
  } else {
    printf("invalid-insn\n");
  }
}


char const *regs8[] = {"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"};
char const *regs16[]= {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
char const *regs32[]= {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
char const *vregs8[]= {"vr0b", "vr1b", "vr2b", "vr3b", "vr0B", "vr1B",
  "vr2B", "vr3B"};
char const *vregs8rex[]= {"vr0b", "vr1b", "vr2b", "vr3b", "vr4b",
  "vr5b", "vr6b", "vr7b"};
char const *vregs16[]={"vr0w", "vr1w", "vr2w", "vr3w", "vr4w", "vr5w",
  "vr6w", "vr7w"};
char const *vregs32[]={"vr0d", "vr1d", "vr2d", "vr3d", "vr4d", "vr5d",
  "vr6d", "vr7d"};
char const *tregs8[]={"tr0b", "tr1b", "tr2b", "tr3b", "tr4b", "tr5b",
  "tr6b", "tr7b"};
char const *tregs16[]={"tr0w", "tr1w", "tr2w", "tr3w", "tr4w", "tr5w",
  "tr6w", "tr7w"};
char const *tregs32[]={"tr0d", "tr1d", "tr2d", "tr3d", "tr4d", "tr5d",
  "tr6d", "tr7d"};

char const *segs[] = {"es", "cs", "ss", "ds", "fs", "gs"};
size_t const num_segs = sizeof segs/sizeof segs[0];
char const *vsegs[] = {"vseg0", "vseg1", "vseg2", "vseg3", "vseg4", "vseg5"};
size_t const num_vsegs = sizeof vsegs/sizeof vsegs[0];

static int
reg2str(unsigned val, int tag, int size, bool rex_used, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;

  if (size == 1) {
    if (val < 8) {
      DBE(INSN, printf("%%%s", tag?vregs8[val]:regs8[val]));
      ptr += snprintf(ptr, end - ptr, "%%%s", tag?vregs8[val]:regs8[val]);
    } else {
      ASSERT((val - 8) < sizeof tregs8/sizeof tregs8[0]);
      DBE(INSN, printf(ptr, end - ptr, "%%%s", tregs8[val - 8]));
      ptr += snprintf(ptr, end - ptr, "%%%s", tregs8[val - 8]);
    }
  } else if (size == 2) {
    if (val < 8) {
      DBE(INSN, printf("%%%s", tag?vregs16[val]:regs16[val]));
      ptr += snprintf(ptr, end - ptr, "%%%s", tag?vregs16[val]:regs16[val]);
    } else {
      ASSERT(tag);
      ASSERT((val - 8) < sizeof tregs16/sizeof tregs16[0]);
      ptr += snprintf(ptr, end - ptr, "%%%s", tregs16[val - 8]);
    }
  } else if (size == 4) {
    if (val < 8) {
      ptr += snprintf(ptr, end - ptr, "%%%s", tag?vregs32[val]:regs32[val]);
    } else {
      ASSERT(tag);
      ASSERT((val - 8) < sizeof tregs32/sizeof tregs32[0]);
      ptr += snprintf(ptr, end - ptr, "%%%s", tregs32[val - 8]);
    }
  } else ASSERT(0);
  return (ptr - buf);
}

static bool
str2reg(char const *str, int *val, int *tag, int *size, bool *rex_used)
{
	unsigned i;
  int rsize, num_elems;
  char const **regs;
  tag_t ttag;

  for (ttag = tag_const;
       ttag == tag_const || ttag == tag_var;
       ttag = (ttag == tag_const) ? tag_var : tag_abcd) {

    for (rsize = 1; rsize <= 4; rsize *= 2) {
      int i;

      if (ttag == tag_const) {
        if (rsize == 1) {
          regs = regs8;
          num_elems = sizeof regs8/sizeof regs8[0];
        } else if (rsize == 2) {
          regs = regs16;
          num_elems = sizeof regs16/sizeof regs16[0];
        } else if (rsize == 4) {
          regs = regs32;
          num_elems = sizeof regs32/sizeof regs32[0];
        }
      } else if (ttag == tag_var) {
        if (rsize == 1) {
          regs = vregs8;
          num_elems = sizeof vregs8/sizeof vregs8[0];
        } else if (rsize == 2) {
          regs = vregs16;
          num_elems = sizeof vregs16/sizeof vregs16[0];
        } else if (rsize == 4) {
          regs = vregs32;
          num_elems = sizeof vregs32/sizeof vregs32[0];
        }
      } else ASSERT(0);

      for (i = 0; i < num_elems; i++) {
        if (str[0] == '%' && !strcmp(str+1, regs[i])) {
          *size = rsize;
          *val = i;
          *tag = ttag;
          *rex_used = false; //for now..
          return true;
        }
      }
    }
  }

  /* check for temporaries. */
  for (i = 0; i < sizeof tregs32/sizeof tregs32[0]; i++) {
    if (str[0] == '%' && !strcmp(str+1, tregs32[i])) {
      *size = 4;
      *val = TEMP_REG0 + i;
      *tag = tag_var;
      *rex_used = false; //for now..
      return true;
    }
  }

  return false;
}



static int
seg2str(int val, int tag, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  if (val != -1) {
    ASSERT(val >= 0 && val < 6);
    ptr += snprintf(ptr, end - ptr, "%%%s", tag?vsegs[val]:segs[val]);
  }
  return (ptr - buf);
}

static bool
str2seg(char const *str, int *val, int *tag)
{
  unsigned i;
  char buf[8];

  for (i = 0; i < sizeof segs/sizeof segs[0]; i++) {
    snprintf(buf, sizeof buf, "%%%s", segs[i]);
    if (!strcmp(buf, str)) {
      *tag = tag_const;
      *val = i;
      return true;
    }
    snprintf(buf, sizeof buf, "%%%s", vsegs[i]);
    if (!strcmp(buf, str)) {
      *tag = tag_var;
      *val = i;
      return true;
    }
  }
  return false;
}

static bool
str2prefix(char const *str, int *val, int *tag)
{
  int i;
  char const *ptr;
  if (strstart(str, "prefix", &ptr)) {
    if (!strcmp(ptr, "NN")) {
      *tag = tag_var;
      *val = 0;
    } else {
      *val = strtol(ptr, NULL, 16);
      *tag = tag_const;
    }
    return true;
  }
  return false;
}

static int
mem2str(operand_mem_val_t const *val, operand_mem_tag_t const *tag, int size,
        bool rex_used, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  char base_reg[16] = "";
  char index_reg[16] = "";

  if (val->segtype == segtype_sel) {
    size_t seglen;
    seglen = seg2str(val->seg.sel, tag->seg.sel, ptr, end - ptr);
    if (seglen) {
      ptr += seglen;
      *ptr++ = ':';
    }
  } else if (val->segtype == segtype_desc) {
    if (tag->seg.desc) {
      ptr += snprintf(ptr, end - ptr, "C%d", val->seg.desc);
    } else {
      ptr += snprintf(ptr, end - ptr, "%d:", val->seg.desc);
    }
  }
  *ptr='\0';
  if (tag->disp) {
    ptr += snprintf(ptr, end - ptr, "C%d", (int)val->disp);
  } else {
    if (val->disp) {
      ptr += snprintf(ptr, end - ptr, "%llu", val->disp);
    }
  }
  if (val->base != -1) {
    reg2str(val->base, tag->base, val->addrsize, rex_used, base_reg,
        sizeof base_reg);
  } else {
    snprintf(base_reg, sizeof base_reg, "");
  }

  if (val->index != -1) {
    reg2str(val->index, tag->index, val->addrsize, rex_used, index_reg,
        sizeof index_reg);
  }

  if (val->base != -1 || val->index != -1) {
    ptr += snprintf(ptr, end - ptr, "(");
  }
  if (val->index != -1) {
    ptr += snprintf(ptr, end - ptr, "%s,%s,%d", base_reg, index_reg, val->scale);
  } else if (val->base != -1) {
    ptr += snprintf(ptr, end - ptr, "%s", base_reg);
  }
  if (val->base != -1 || val->index != -1) {
    ptr += snprintf(ptr, end - ptr, ")");
  }
  return ptr - buf;
}

static int
imm2str(int64 val, int tag, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  ptr += snprintf(ptr, end - ptr, tag?"C%llu":"%llu", val);
  return ptr - buf;
}

static bool
str2imm(char const *str, uint64_t *val, int *tag, int *size)
{
  if (str[0] == 'C') {
    int i;
    char const *ptr = str + 1;

    *tag = tag_var;
    while (*ptr >= '0' && *ptr <= '9') {
      *ptr++;
    }
    if (*ptr == 'b') {
      *size = 1;
    } else if (*ptr == 'w') {
      *size = 2;
    } else if (*ptr == 'd') {
      *size = 4;
    } else if (*ptr == '\0') {
      /* end of constant string. use default size. */
      *size = 4;
    } else {
      ERR("imm2str('%s') failed. variable does not end with 'b', 'w' or 'd'.\n",
          str);
      ASSERT(0);
    }

    char varnum[ptr - str + 1];
    memcpy(varnum, str, ptr - str);
    *(varnum + (ptr - str)) = '\0';
    *val = atoi(varnum+1);
    return true;
  } else {
    *tag = tag_const;
    *val = atoi(str);
    *size = 0;
    return true;
  }
  return false;
}

static int
mmx2str(int val, int tag, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  ptr += snprintf(ptr, end - ptr, tag?"mmxNN%d":"mmx%d", val);
  return ptr - buf;
}

static int
xmm2str(int val, int tag, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  ptr += snprintf(ptr, end - ptr, tag?"xmmNN%d":"xmm%d", val);
  return ptr - buf;
}

static int
cr2str(int val, int tag, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  ptr += snprintf(ptr, end - ptr, tag?"crNN%d":"cr%d", val);
  return ptr - buf;
}

static int
db2str(int val, int tag, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  ptr += snprintf(ptr, end - ptr, tag?"dbNN%d":"db%d", val);
  return ptr - buf;
}

static int
tr2str(int val, int tag, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  ptr += snprintf(ptr, end - ptr, tag?"trNN%d":"tr%d", val);
  return ptr - buf;
}

static int
op3dnow2str(int val, int tag, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  ptr += snprintf(ptr, end - ptr, tag?"op3dnowNN%d":"op3dnow%d", val);
  return ptr - buf;
}

static int
prefix2str(int val, int tag, char *buf, int buflen)
{
  char *ptr = buf, *end = buf + buflen;
  if (tag) {
    ptr += snprintf(ptr, end - ptr, "prefixNN", val);
  } else {
    ptr += snprintf(ptr, end - ptr, "prefix%x", val);
  }
	return ptr - buf;
}

int
operand2str(operand_t const *op, char *buf, size_t size)
{
  char *ptr = buf, *end = buf + size;

  switch (op->type) {
    case invalid:
      DBE(INSN, printf("(inval)"));
      break;
    case op_reg:
      ptr += reg2str(op->val.reg, op->tag.reg, op->size, op->rex_used,
          ptr, end - ptr);
      DBE(INSN, printf("(reg) %s", buf));
      break;
    case op_seg:
      ptr += seg2str(op->val.seg, op->tag.seg, ptr, end - ptr);
      DBE(INSN, printf("(seg) %s", buf));
      break;
    case op_mem:
      ptr += mem2str(&op->val.mem, &op->tag.mem, op->size, op->rex_used, ptr,
          end - ptr);
      DBE(INSN, printf("(mem) %s", buf));
      break;
    case op_imm:
      ptr += imm2str(op->val.imm, op->tag.imm, ptr, end - ptr);
      DBE(INSN, printf("(imm) %s", buf));
      break;
    case op_cr:
      ptr += cr2str(op->val.cr, op->tag.cr, ptr, end - ptr);
      DBE(INSN, printf("(cr) %s", buf));
      break;
    case op_db:
      ptr += db2str(op->val.db, op->tag.db, ptr, end - ptr);
      DBE(INSN, printf("(db) %s", buf));
      break;
    case op_tr:
      ptr += tr2str(op->val.tr, op->tag.tr, ptr, end - ptr);
      DBE(INSN, printf("(tr) %s", buf));
      break;
    case op_mmx:
      ptr += mmx2str(op->val.mmx, op->tag.mmx, ptr, end - ptr);
      DBE(INSN, printf("(mm) %s", buf));
      break;
    case op_xmm:
      ptr += xmm2str(op->val.xmm, op->tag.xmm, ptr, end - ptr);
      DBE(INSN, printf("(xmm) %s", buf));
      break;
    case op_3dnow:
      ptr += op3dnow2str(op->val.d3now, op->tag.d3now, ptr, end - ptr);
      DBE(INSN, printf("(3dnow) %s", buf));
      break;
    case op_prefix:
      ptr += prefix2str(op->val.prefix, op->tag.prefix, ptr, end - ptr);
      break;
    default:
      printf("Invalid op->type[%d]\n", op->type);
      ASSERT(0);
  }

  return ptr - buf;
}

int
insn2str(insn_t const *insn, char *buf, size_t size)
{
  int i;
  char *ptr = buf, *end = buf + size;
  ptr += snprintf(ptr, end - ptr, "%s", opctable_name(insn->opc));
  *ptr++ = ' ';
  for (i = 0; i < 3; i++) {
    DBE(INSN, printf(" %d:", i));

    ptr += operand2str(&insn->op[i], ptr, end - ptr);
    if (insn->op[i].type != invalid && i != 2 && insn->op[i+1].type!= invalid) {
      ptr += snprintf(ptr, end - ptr, ",");
    }
  }
  ASSERT(ptr < end);
  return (ptr - buf);
}

int
insns2str(insn_t const *insns, int n_insns, char *buf, size_t size)
{
  char *ptr = buf, *end = buf + size;
  int i;

  for (i = 0; i < n_insns; i++) {
    if (i != 0) {
      *ptr++ = ' '; *ptr++ = ';'; *ptr++ = ' ';
    } else {
      *ptr++ = '{';
    }
    ptr += insn2str(&insns[i], ptr, end - ptr);
    ASSERT(ptr < end);
  }
  *ptr++ = '}';
  *ptr++ = '\0';
  return (ptr - buf);
}

bool
insn_is_terminating(insn_t const *insn)
{
  char const *opc;
  opc = opctable_name(insn->opc);
  if (opc[0] == 'j') {
    return true;
  }
  if (opc[0] == 'l' && opc[1] == 'j') {
    return true;
  }
  if (strstart(opc, "call", NULL)) {
    return true;
  }
  if (strstart(opc, "lcall", NULL)) {
    return true;
  }
  if (strstart(opc, "loop", NULL)) {
    return true;
  }
  if (!strcmp(opc, "int")) {
    return true;
  }
  if (!strcmp(opc, "retw") || !strcmp(opc, "retl")) {
    return true;
  }
  if (!strcmp(opc, "lretw") || !strcmp(opc, "lretl")) {
    return true;
  }
  if (!strcmp(opc, "iret")) {
    return true;
  }
  if (!strcmp(opc, "hlt")) {
    return true;
  }
  /* mov_to_cr3, mov_to_cr0 */
  if (   !strcmp(opc, "mov") && insn->op[0].type == op_cr
      && (insn->op[0].val.cr == 3 || insn->op[0].val.cr == 0)
      && insn->op[0].tag.cr == tag_const) {
    return true;
  }
  return false;
}

static bool
check_operand(operand_t const *op)
{
  switch (op->type) {
    case invalid:
      if (op->tag.all != tag_const) {
        goto error;
      }
      break;
    case op_imm:
      if (op->size != 0) {
        goto error;
      }
      break;
		default:
			break;
  }
  return true;
error:
  ERR("check_operand_failed on operand:\n");
  //operand2str(op,pb1,sizeof pb1));
  return false;
}

static bool
check_insn(insn_t const *insn)
{
  int i;
  for (i = 0; i < 3; i++) {
    if (!check_operand(&insn->op[i])) {
      return false;
    }
  }
	return true;
}

static void
operand_init(operand_t *op)
{
  /* Load the operand with the default values for memory operands. */
  op->type = invalid;
  op->size = 0;
  op->val.mem.addrsize = 4;
  op->val.mem.segtype = segtype_sel;
  op->val.mem.seg.sel = R_DS;
  op->val.mem.base = -1;
  op->val.mem.scale = 0;
  op->val.mem.index = -1;
  op->val.mem.disp = 0;

  op->tag.all = tag_const;
  op->tag.mem.all = tag_const;
  op->tag.mem.seg.sel = tag_const;
  op->tag.mem.base = tag_const;
  op->tag.mem.index = tag_const;
  op->tag.mem.disp = tag_const;
}

void
insn_init(insn_t *insn)
{
  int i;
  insn->opc = opc_inval;
  //strlcpy(insn->opc, "inval", sizeof insn->opc);
  for (i = 0; i < 3; i++) {
    operand_init(&insn->op[i]);
  }
}

/* This function should be better written, so that if we change the register
 * names, this function does not need to be changed. For now, let us go with
 * this simple implementation.
 */
static opertype_t
str_op_get_type(char const *ptr)
{
  opertype_t type;
  if (ptr[0] == '%') {
    if (ptr[2] == 's' && ptr[3] == '\0') {                         //cons seg
      type = op_seg;
    } else if ((ptr[1] == 'v' || ptr[1] == 't') && ptr[2] == 'r') {//var reg
      type = op_reg;
    } else if (ptr[1] == 'v' && ptr[2] == 's' && ptr[3] == 'e') {  //var seg
      type = op_seg;
    } else {                                                       //cons reg
      type = op_reg;
    }
  } else if (ptr[0] >= '0' && ptr[0] <= '9') {                     //cons imm
    type = op_imm;
  } else if ((ptr[0] == 'C')) {                                    //var imm
    type = op_imm;
  } else if (strstart(ptr, "prefix", NULL)) {
    type = op_prefix;
  } else {
    ERR("Type of string operand '%s' not supported.\n", ptr);
    ASSERT(0);
  }
  return type;
}

bool
str2operand(operand_t *op, char const *str)
{
  int val, tag, size;
  bool rex_used;
  uint64_t ival;
  opertype_t type;
  char const *ptr = str;
  char *wptr;

  while (*ptr++ == ' ');
  ptr--;

  char strcopy[strlen(ptr)+1];
  strlcpy(strcopy, ptr, sizeof strcopy);
  wptr = strcopy + strlen(strcopy) - 1;
  while (*wptr == ' ') { wptr--; }
  *(++wptr) = '\0';
  ptr = strcopy;

  type = str_op_get_type(ptr);

  switch (type) {
    case op_reg:
      if (!str2reg(ptr, &val, &tag, &size, &rex_used)) {
        return false;
      }
      op->type = type;
      op->size = size;                     ASSERT(op->size == size);
      op->val.reg = val;                   ASSERT(op->val.reg == val);
      op->tag.reg = tag;                                                    
      op->rex_used = rex_used;             ASSERT(op->rex_used == rex_used);
      return true;
    case op_imm:
      if (!str2imm(ptr, &ival, &tag, &size)) {
        return false;
      }
      op->type = type;
      op->size = size;
      op->val.imm = ival;
      op->tag.imm = tag;
      return true;
    case op_seg:
      if (!str2seg(ptr, &val, &tag)) {
        return false;
      }
      op->type = type;
      op->size = 0;
      op->val.seg = val;                   ASSERT(op->val.seg == val);
      op->tag.seg = tag;                                                    
      return true;
    case op_prefix:
      if (!str2prefix(ptr, &val, &tag)) {
        return false;
      }
      op->type = type;
      op->size = 0;
      op->val.prefix = val;
      op->tag.prefix = tag;
      return true;
    default:
      ASSERT(0);
      break;
  }
  return false;
}

static bool
is_pcrel_operand(insn_t const *insn, int j)
{
  char const *opc = opctable_name(insn->opc);
  if (   opc[0] == 'j'
      || !strcmp(opc, "call")
      || !strcmp(opc, "loop")) {
    if (j == 0) {
      return true;
    }
  }
  return false;
}

#define rename_constants_mem_field(op, variable, value, field, ftype) do {    \
  operand_t _op;                                                              \
  _op.type = op_##ftype;                                                      \
  _op.val.ftype = op->val.mem.field;                                          \
  _op.tag.ftype = op->tag.mem.field;                                          \
  _op.size = (_op.type == op_imm || _op.type == op_seg)?                      \
                                  0:op->val.mem.addrsize;                     \
  if (operands_equal(&_op, value)) {                                          \
    op->val.mem.field = variable->val.ftype;                                  \
    ASSERT(variable->tag.ftype != tag_const);                                 \
    op->tag.mem.field = variable->tag.ftype;                                  \
  }                                                                           \
} while(0)

void
insn_rename_constants(insn_t *insn, assignments_t const *assignments)
{
  int i;
  /*
  char buf[4096];
  insn2str(insn, buf, sizeof buf);
  printf("Renaming %s:\n", buf);
  */
  for (i = 0; i < assignments->num_assignments; i++) {
    int j;
    operand_t const *var, *val;
    var = &assignments->arr[i].var;
    val = &assignments->arr[i].val;
    for (j = 0; j < 3; j++) {
      operand_t *op = &insn->op[j];
      if (op->type == op_mem) {
        if (op->val.mem.segtype == segtype_sel) {
          rename_constants_mem_field(op, var, val, seg.sel, seg);
        } else {
          rename_constants_mem_field(op, var, val, seg.desc, imm);
        }
        rename_constants_mem_field(op, var, val, base, reg);
        rename_constants_mem_field(op, var, val, index, reg);
        rename_constants_mem_field(op, var, val, disp, imm);
      } else if (op->type == op_reg && val->type == op_reg) {
        /* For register operands, also take into account the varying sizes. */
        ASSERT(val->tag.reg == tag_const);
        if (   op->val.reg == val->val.reg
            && op->tag.reg == tag_const) {
          unsigned size = op->size;
          ASSERT(op->size <= var->size);
          memcpy(op, var, sizeof(operand_t));
          op->size = size;
        }
      } else {
        if (operands_equal(op, val)) {
          memcpy(op, var, sizeof(operand_t));
        }
      }
    }
  }
}

char const *
tag2str(opertype_t tag)
{
  switch (tag) {
    case tag_const: return "tag_const";
    case tag_var: return "tag_var";
    case tag_eax: return "tag_eax";
    case tag_abcd: return "tag_abcd";
    case tag_no_esp: return "tag_no_esp";
    case tag_no_eax: return "tag_no_eax";
    case tag_no_eax_esp: return "tag_no_eax_esp";
    case tag_no_cs_gs: return "tag_no_cs_gs";
    case tag_cs_gs: return "tag_cs_gs";
    default: NOT_REACHED();
  }
}

opertype_t
str2tag(char const *tagstr)
{
  opertype_t tags[] = {tag_const, tag_var, tag_eax, tag_abcd, tag_no_esp,
    tag_no_eax, tag_no_eax_esp, tag_no_cs_gs, tag_cs_gs};
  unsigned i;

  for (i = 0; i < sizeof tags/sizeof tags[0]; i++) {
    if (!strcmp(tag2str(tags[i]), tagstr)) {
      return tags[i];
    }
  }
  printf("tagstr = %s\n", tagstr);
  NOT_REACHED();
}


char const *
get_opertype_str(opertype_t type)
{
  switch(type) {
    case invalid: return "invalid";
    case op_reg: return "reg";
    case op_mem: return "mem";
    case op_seg: return "seg";
    case op_imm: return "imm";
    case op_cr: return "cr";
    case op_db: return "db";
    case op_tr: return "tr";
    case op_mmx: return "mmx";
    case op_xmm: return "xmm";
    case op_3dnow: return "d3now";
    case op_prefix: return "prefix";
    default: NOT_REACHED();
  }
}

bool
operands_equal(operand_t const *op1, operand_t const *op2)
{
  if (op1->type != op2->type) {
    return false;
  }
  if (op1->size != op2->size) {
    return false;
  }
  if (op1->type == invalid) {
    return true;
  }

  switch (op1->type) {
    case op_reg:   return (   op1->val.reg == op2->val.reg
                           && op1->tag.reg == op2->tag.reg);
    case op_seg:   return (   op1->val.seg == op2->val.seg
                           && op1->tag.seg == op2->tag.seg);
    case op_mem:   return (   op1->val.mem.segtype == op2->val.mem.segtype
                           && op1->val.mem.seg.all == op2->val.mem.seg.all
                           && op1->val.mem.base == op2->val.mem.base
                           && op1->val.mem.scale == op2->val.mem.scale
                           && op1->val.mem.index == op2->val.mem.index
                           && op1->val.mem.disp == op2->val.mem.disp
                           && op1->tag.mem.seg.all == op2->tag.mem.seg.all
                           && op1->tag.mem.all == op2->tag.mem.all
                           && op1->tag.mem.base == op2->tag.mem.base
                           && op1->tag.mem.index == op2->tag.mem.index
                           && op1->tag.mem.disp == op2->tag.mem.disp);
    case op_imm:   /* All tag_const imm operands should have 0 size. */
                   ASSERT(op1->tag.imm != tag_const || op1->size == 0);
                   ASSERT(op2->tag.imm != tag_const || op2->size == 0);
                   return (   op1->val.imm == op2->val.imm
                           && op1->tag.imm == op2->tag.imm);
    case op_cr:    return (   op1->val.cr == op2->val.cr
                           && op1->tag.cr == op2->tag.cr);
    case op_db:    return (   op1->val.db == op2->val.db
                           && op1->tag.db == op2->tag.db);
    case op_tr:    return (   op1->val.tr == op2->val.tr
                           && op1->tag.tr == op2->tag.tr);
    case op_mmx:   return (   op1->val.mmx == op2->val.mmx
                           && op1->tag.mmx == op2->tag.mmx);
    case op_xmm:   return (   op1->val.xmm == op2->val.xmm
                           && op1->tag.xmm == op2->tag.xmm);
    case op_3dnow: return (   op1->val.d3now == op2->val.d3now
                           && op1->tag.d3now == op2->tag.d3now);
    case op_prefix: return (   op1->val.prefix == op2->val.prefix
                            && op1->tag.prefix == op2->tag.prefix);
		case invalid: NOT_REACHED();
  }
  ASSERT(0);
  return false;
}

/* This function is used to generate the arguments given to peepgen_code. */
long
operand_get_value(operand_t const *op)
{
  switch (op->type) {
    case invalid:
      ASSERT(0);
    case op_reg:
      return op->val.reg;
    case op_seg:
      return op->val.seg;
    case op_mem:
      ASSERT(0);
    case op_imm:
      ASSERT(op->tag.imm == tag_const);
      ASSERT(op->size == 0);
      return op->val.imm;
    case op_cr:
      return op->val.cr;
    case op_db:
      return op->val.db;
    case op_tr:
      return op->val.tr;
    case op_mmx:
      return op->val.mmx;
    case op_xmm:
      return op->val.xmm;
    case op_3dnow:
      return op->val.d3now;
    case op_prefix:
      return op->val.prefix;
    default:
      ERR("Invalid type(%d) for operand_get_value(op).\n", op->type);
      NOT_REACHED();
  }
}

bool
insn_is_indirect_jump(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
  if (!strcmp(opc, "jmp") && insn->op[0].type == op_mem) {
    return true;
  }
  return false;
}

bool
insn_is_string_op(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
  if (   !strcmp(opc, "movs")
			|| !strcmp(opc, "cmps")
			|| !strcmp(opc, "stos")
			|| !strcmp(opc, "lods")
			|| !strcmp(opc, "scas")
			|| !strcmp(opc, "ins")
			|| !strcmp(opc, "outs")) {
    return true;
  }
  return false;
}

bool
insn_is_movs_or_cmps(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
  if (   !strcmp(opc, "movs")
			|| !strcmp(opc, "cmps")) {
    return true;
  }
  return false;
}

bool
insn_is_cmps_or_scas(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
  if (   !strcmp(opc, "cmps")
			|| !strcmp(opc, "scas")) {
    return true;
  }
  return false;
}

bool
insn_is_push(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
  if (strstart(opc, "push", NULL)) {
    return true;
  }
  return false;
}

bool
insn_is_pop(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
  if (strstart(opc, "pop", NULL)) {
    return true;
  }
  return false;
}

bool
insn_is_sti(insn_t const *insn)
{
	char const *opc = opctable_name(insn->opc);
	if (strstart(opc, "sti", NULL)) {
		return true;
	}
	return false;
}

char *jmp_opcodes[] = {"jmp", "je", "jne", "jg", "jle", "jl", "jge", "ja",
  "jbe", "jb", "jae", "jo", "jno", "js", "jns"};

bool
insn_is_direct_jump(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
  char **jmp_opc;
  for (jmp_opc = jmp_opcodes;
       jmp_opc < jmp_opcodes + sizeof jmp_opcodes/sizeof jmp_opcodes[0];
       jmp_opc++) {
    if (!strcmp(opc, *jmp_opc) && insn->op[0].type == op_imm) {
      return true;
    }
  }
  return false;
}

bool
insn_is_conditional_jump(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
  if (insn_is_direct_jump(insn) && strcmp(opc, "jmp")) {
    return true;
  }
  return false;
}

static inline bool
operand_is_mem16(operand_t const *op)
{
  if (op->type == op_mem && op->val.mem.addrsize == 2) {
    return true;
  }
  return false;
}

static inline bool
operand_is_mem(operand_t const *op)
{
  return (op->type == op_mem);
}

static inline bool
operand_is_prefix(operand_t const *op)
{
  return (op->type == op_prefix);
}

static bool
insn_accesses_op_type(insn_t const *insn, struct operand_t const **op1,
		struct operand_t const **op2, bool (*type_fn)(operand_t const *op))
{
	struct operand_t const *retop1 = NULL, *retop2 = NULL;
	bool ret = false;
  if (!strcmp(opctable_name(insn->opc), "lea")) {
		return false;
  }
  if ((*type_fn)(&insn->op[0])) {
		retop1 = &insn->op[0];
		ret = true;
  }
  if ((*type_fn)(&insn->op[1])) {
		if (!retop1) {
			retop1 = &insn->op[1];
		} else {
			retop2 = &insn->op[1];
		}
		ret = true;
  }
  if ((*type_fn)(&insn->op[2])) {
		if (!retop1) {
			retop1 = &insn->op[2];
		} else {
			retop2 = &insn->op[2];
		}
		ret = true;
  }
	if (op1) {
		*op1 = retop1;
	}
	if (op2) {
		*op2 = retop2;
	}
  return ret;
}

bool
insn_accesses_mem16(insn_t const *insn, struct operand_t const **op1,
		struct operand_t const **op2)
{
	return insn_accesses_op_type(insn, op1, op2, operand_is_mem16);
}

bool
insn_accesses_mem(insn_t const *insn, struct operand_t const **op1,
		struct operand_t const **op2)
{
	return insn_accesses_op_type(insn, op1, op2, operand_is_mem);
}

bool
insn_accesses_stack(insn_t const *insn)
{
  char const *opc = opctable_name(insn->opc);
	if (   strstart(opc, "push", NULL)
			|| strstart(opc, "pop", NULL)
			|| !strcmp(opc, "call")
			|| !strcmp(opc, "ret")
			|| !strcmp(opc, "lcall")
			|| !strcmp(opc, "lret")
			|| !strcmp(opc, "iret")) {
		return true;
	}
	return false;
}

struct operand_t const *
insn_has_prefix(insn_t const *insn)
{
	struct operand_t const *retop = NULL;
	if (!insn_accesses_op_type(insn, &retop, NULL, operand_is_prefix)) {
		return NULL;
	}
	ASSERT(retop);
	return retop;
}

static unsigned
operand_get_size(operand_t const *op)
{
	switch (op->type) {
		case op_reg: return op->size;
		case op_mem: return op->size;
		default: return (unsigned)-1;
	}
}

unsigned
insn_get_operand_size(insn_t const *insn)
{
	unsigned size = 8;
	size = min(size, operand_get_size(&insn->op[0]));
	size = min(size, operand_get_size(&insn->op[1]));
	size = min(size, operand_get_size(&insn->op[2]));
	return size;
}

unsigned
insn_get_addr_size(insn_t const *insn)
{
	operand_t const *op;
	bool mem;
	mem = insn_accesses_mem(insn, &op, NULL);
	ASSERT(mem);
	ASSERT(op->tag.all == tag_const);
	ASSERT(op->tag.mem.all == tag_const);
	ASSERT(op->val.mem.addrsize == 2 || op->val.mem.addrsize == 4);
	return op->val.mem.addrsize;
}

void
insn_get_usedef(struct insn_t const *insn, struct regset_t *use,
    struct regset_t *def)
{
  int i;
  regset_clear_all(use);
  regset_clear_all(def);
  for (i = 0; i < 3; i++) {
    operand_t const *op = &insn->op[i];
    if (op->type == op_reg) {
      int reg;
      if (op->tag.reg == tag_const && op->size == 1) {
        reg = op->val.reg % 4;
      } else {
        reg = op->val.reg;
      }
      if (i == 0) {
        regset_mark(def, op->tag.reg, reg);
        //XXX: if opcode is r/w, then also mark use.
      } else {
        regset_mark(use, op->tag.reg, reg);
      }
    } else if (op->type == op_mem) {
      ASSERT(op->tag.mem.all == tag_const);
      if (op->val.mem.base != -1) {
        regset_mark(use, op->tag.mem.base, op->val.mem.base);
      }
      if (op->val.mem.index != -1) {
        regset_mark(use, op->tag.mem.index, op->val.mem.index);
      }
    }
  }
}
