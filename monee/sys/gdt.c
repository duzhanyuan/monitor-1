#include "sys/gdt.h"
#include <debug.h>
#include <bitmap.h>
#include <string.h>
#include <stdio.h>
#include "sys/vcpu.h"
#include "sys/tss.h"
#include "sys/mode.h"
#include "mem/palloc.h"
#include "mem/vaddr.h"
#include "mem/pt_mode.h"
#include "mem/malloc.h"

#define GDT 2

#define DESCTYPE_MASK (1 << 12)

/* The Global Descriptor Table (GDT).

   The GDT, an x86-specific structure, defines segments that can
   potentially be used by all processes in a system, subject to
   their permissions.  There is also a per-process Local
   Descriptor Table (LDT) but that is not used by modern
   operating systems.

   Each entry in the GDT, which is known by its byte offset in
   the table, identifies a segment.  For our purposes only three
   types of segments are of interest: code, data, and TSS or
   Task-State Segment descriptors.  The former two types are
   exactly what they sound like.  The TSS is used primarily for
   stack switching on interrupts.

   For more information on the GDT as used here, refer to
   [IA32-v3a] 3.2 "Using Segments" through 3.5 "System Descriptor
   Types". */

static uint64_t gdt[GDT_SIZE];

static size_t gdt_size;
static struct bitmap *shadow_segdesc_bitmap;

/* Helper structs. */
struct segdesc {
  unsigned limit:20;
  unsigned base:32;
  unsigned type:4;
  unsigned sys_or_cd:1;
  unsigned dpl:2;
  unsigned p:1;
  unsigned avl:1;
  unsigned l:1;
  unsigned d_or_b:1;
  unsigned g:1;
};

/* GDT helpers. */
static uint64_t make_code_desc (int dpl, bool guest);
static uint64_t make_data_desc (int dpl, bool guest);
static uint64_t make_tss_desc (void *laddr);
static uint64_t make_gdtr_operand (uint16_t limit, void *base);

/* Sets up a proper GDT.  The bootstrap loader's GDT didn't
   include user-mode selectors or a TSS, but we need both now. */
void
gdt_init (void)
{
  uint64_t gdtr_operand;
  int i;

  /* Initialize GDT. */
  memset(gdt, 0, sizeof gdt);

  gdt[SEL_NULL / sizeof *gdt] = 0;
  gdt[SEL_KCSEG / sizeof *gdt] = make_code_desc (0, false);
  gdt[SEL_KDSEG / sizeof *gdt] = make_data_desc (0, false);
  gdt[SEL_UCSEG / sizeof *gdt] = make_code_desc (3, false);
  gdt[SEL_UDSEG / sizeof *gdt] = make_data_desc (3, false);
  gdt[SEL_TSS / sizeof *gdt] = make_tss_desc (tss_get ());
  gdt[SEL_GCSEG / sizeof *gdt] = make_code_desc (3, true);
  gdt[SEL_GDSEG / sizeof *gdt] = make_data_desc (3, true);
  /* TMPSEG is used to save and restore guest flags which involves modification
   * to %ss. TMPSEG should be identical to UDSEG, except that we use a
   * separate GDT descriptor, so that the interrupt handler does not get
   * confused on seeing an interrupt. */
  gdt[SEL_TMPSEG / sizeof *gdt] = make_data_desc (3, false);

  shadow_segdesc_bitmap = bitmap_create(NUM_SEGS);
  ASSERT(shadow_segdesc_bitmap);

  vcpu.gdt.base = 0;
  vcpu.gdt.limit = 0;

  memset(gdt, 0x0, SEL_BASE);

  /* Load GDTR, TR.  See [IA32-v3a] 2.4.1 "Global Descriptor
     Table Register (GDTR)", 2.4.4 "Task Register (TR)", and
     6.2.4 "Task Register".  */
  gdtr_operand = make_gdtr_operand (sizeof gdt - 1, gdt);
  asm volatile ("lgdt %0" : : "m" (gdtr_operand));
  asm volatile ("ljmpl %0, $1f; 1:" : : "i"(SEL_KCSEG));
  asm volatile ("mov %0, %%ds; 1:" : : "a"(SEL_KDSEG));
  asm volatile ("mov %0, %%es; 1:" : : "a"(SEL_KDSEG));
  asm volatile ("mov %0, %%fs; 1:" : : "a"(SEL_KDSEG));
  asm volatile ("mov %0, %%gs; 1:" : : "a"(SEL_KDSEG));
  asm volatile ("mov %0, %%ss; 1:" : : "a"(SEL_KDSEG));
  asm volatile ("ltr %w0" : : "q" (SEL_TSS));
}

/* System segment or code/data segment? */
enum seg_class
  {
    CLS_SYSTEM = 0,             /* System segment. */
    CLS_CODE_DATA = 1           /* Code or data segment. */
  };

/* Limit has byte or 4 kB page granularity? */
enum seg_granularity
  {
    GRAN_BYTE = 0,              /* Limit has 1-byte granularity. */
    GRAN_PAGE = 1               /* Limit has 4 kB granularity. */
  };

/* Returns a segment descriptor with the given 32-bit BASE and
   20-bit LIMIT (whose interpretation depends on GRANULARITY).
   The descriptor represents a system or code/data segment
   according to CLASS, and TYPE is its type (whose interpretation
   depends on the class).

   The segment has descriptor privilege level DPL, meaning that
   it can be used in rings numbered DPL or lower.  In practice,
   DPL==3 means that user processes can use the segment and
   DPL==0 means that only the kernel can use the segment.  See
   [IA32-v3a] 4.5 "Privilege Levels" for further discussion. */
static uint64_t
make_seg_desc (uint32_t base,
               uint32_t limit,
               enum seg_class class,
               int type,
               int dpl,
               enum seg_granularity granularity)
{
  uint32_t e0, e1;

  ASSERT (limit <= 0xfffff);
  ASSERT (class == CLS_SYSTEM || class == CLS_CODE_DATA);
  ASSERT (type >= 0 && type <= 15);
  ASSERT (dpl >= 0 && dpl <= 3);
  ASSERT (granularity == GRAN_BYTE || granularity == GRAN_PAGE);

  e0 = ((limit & 0xffff)             /* Limit 15:0. */
        | (base << 16));             /* Base 15:0. */

  e1 = (((base >> 16) & 0xff)        /* Base 23:16. */
        | (type << 8)                /* Segment type. */
        | (class << 12)              /* 0=system, 1=code/data. */
        | (dpl << 13)                /* Descriptor privilege. */
        | (1 << 15)                  /* Present. */
        | (limit & 0xf0000)          /* Limit 16:19. */
        | (1 << 22)                  /* 32-bit segment. */
        | (granularity << 23)        /* Byte/page granularity. */
        | (base & 0xff000000));      /* Base 31:24. */

  return e0 | ((uint64_t) e1 << 32);
}

/* Returns a descriptor for a readable code segment with base at
   0, a limit of 4 GB, and the given DPL. */
static uint64_t
make_code_desc (int dpl, bool guest)
{
  uint32_t limit;
  if (guest) {
    limit = (LOADER_MONITOR_VIRT_BASE - 1) >> 12;// 0xffbff;
  } else {
    limit = 0xfffff;
  }
  return make_seg_desc (0, limit, CLS_CODE_DATA, 10, dpl, GRAN_PAGE);
}

/* Returns a descriptor for a writable data segment with base at
   0, a limit of 4 GB, and the given DPL. */
static uint64_t
make_data_desc (int dpl, bool guest)
{
  uint32_t limit;
  if (guest) {
    limit = (LOADER_MONITOR_VIRT_BASE - 1) >> 12;// 0xffbff;
  } else {
    limit = 0xfffff;
  }
  return make_seg_desc (0, limit, CLS_CODE_DATA, 2, dpl, GRAN_PAGE);
}

/* Returns a descriptor for an "available" 32-bit Task-State
   Segment with its base at the given linear address, a limit of
   0x67 bytes (the size of a 32-bit TSS), and a DPL of 0.
   See [IA32-v3a] 6.2.2 "TSS Descriptor". */
static uint64_t
make_tss_desc (void *laddr)
{
  return make_seg_desc ((uint32_t) laddr, 0x67, CLS_SYSTEM, 9, 0, GRAN_BYTE);
}


/* Returns a descriptor that yields the given LIMIT and BASE when
   used as an operand for the LGDT instruction. */
static uint64_t
make_gdtr_operand (uint16_t limit, void *base)
{
  return limit | ((uint64_t) (uint32_t) base << 16);
}

uint32_t
get_seg_base(uint32_t e1, uint32_t e2)
{
  return ((e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000));
}

uint32_t
get_seg_limit(uint32_t e1, uint32_t e2)
{
  unsigned int limit;
  limit = (e1 & 0xffff) | (e2 & 0x000f0000);
  if (e2 & DESC_G_MASK) {
    limit = (limit << 12) | 0xfff;
  }
  return limit;
}

bool
read_segment(uint32_t *e1_ptr, uint32_t *e2_ptr, int selector, bool shadow,
    bool set_accessed)
{
  uint32_t gdt_base;
  uint16_t gdt_limit;
  int index;
  target_ulong ptr;
	//ASSERT(intr_get_level() == INTR_ON);//XXX

  if (shadow) {
    gdt_base = (uint32_t)gdt;
    gdt_limit = sizeof gdt - 1;
  } else {
    ASSERT(vcpu.cr[0] & CR0_PE_MASK);
    if (selector & 0x4) {
      NOT_IMPLEMENTED();
      //dt = &vcpu.orig.ldt;      //XXX
    } else {
      gdt_base = vcpu.gdt.base;
      gdt_limit = vcpu.gdt.limit;
    }
  }
  index = selector & ~7;
  if ((index + 7) > gdt_limit) {
    return false;
  }
  ptr = gdt_base + index;

	if (shadow) {
		ASSERT(is_monitor_vaddr((void *)ptr));
		*e1_ptr = *(uint32_t *)ptr;
		*e2_ptr = *(uint32_t *)(ptr + 4);
	} else {
		if (set_accessed) {
			*e1_ptr = ldl_kernel(ptr);
			*e2_ptr = ldl_kernel(ptr + 4);
		} else {
			*e1_ptr = ldl_kernel_dont_set_access_bit(ptr);
			*e2_ptr = ldl_kernel_dont_set_access_bit(ptr + 4);
		}
	}

  /* set the access bit if not already set */
  if (set_accessed && !((*e2_ptr) & DESC_A_MASK)) {
    *e2_ptr |= DESC_A_MASK;
    stl_kernel(ptr + 4, *e2_ptr);
  }

  return true;
}

void
gdt_make_shadow_segdesc(long segno)
{
  enum seg_granularity granularity;
  uint32_t limit;

  if (vcpu.segs[segno].flags & DESC_S_MASK) {
    /* segment descriptor, not implemented yet. */
    //NOT_IMPLEMENTED();
  }
  if  (vcpu.segs[segno].flags & DESC_G_MASK) {
    granularity = GRAN_PAGE;
    limit = vcpu.segs[segno].limit >> 12;
  } else {
    granularity = GRAN_BYTE;
    limit = vcpu.segs[segno].limit;
  }

	/* XXX: Truncate limit here to LOADER_MONITOR_VIRT_BASE - 1! */

  /* See [IA32 Vol3, 3-13]. */
  gdt[vcpu.segs[segno].selector >> 3] = make_seg_desc(
      vcpu.segs[segno].base,
      limit,
      CLS_CODE_DATA,
      2, /* type */
      3, /* dpl */
      granularity);

  DBGn(GDT, "%s(): gdt[%04x](%p) = %016llx\n", __func__,
      vcpu.segs[segno].selector >> 3,
      &(gdt[vcpu.segs[segno].selector >> 3]),
      gdt[vcpu.segs[segno].selector >> 3]);
}

void
gdt_load(uint32_t base, uint16_t limit)
{
  int segno;
  DBGn(GDT, "%s(%#x, %#hx) called.\n", __func__, base, limit);

  /* Read all segment descriptors of existing segments into their
   * respective caches. */
  for (segno = 0; segno < NUM_SEGS; segno++) {
    uint32_t e1, e2;
    if (vcpu.segs[segno].selector < SEL_BASE) {
      DBGn(GDT, "vcpu.segs[%d].selector=%#x, SEL_BASE=%#x\n",
          segno, vcpu.segs[segno].selector, SEL_BASE);
      read_segment(&e1, &e2, vcpu.segs[segno].selector, true, false);
      if (e1 != 0 || e2 != 0) {
        /* The segment descriptor in gdt contains a useful value. Cache it. */

        /* Read the original seg desc. */
        read_segment(&e1, &e2, vcpu.segs[segno].selector, false, false);
        vcpu.segs[segno].base = get_seg_base(e1, e2);
        vcpu.segs[segno].limit = get_seg_limit(e1, e2);
        /* retain the accessed bit. */
        vcpu.segs[segno].flags = e2;
        vcpu.segs[segno].selector = (SEL_SHADOW + (segno << 3)) | 0x3;
        gdt_make_shadow_segdesc(segno);
      }
    }
    DBGn(GDT, "vcpu.segs[%d].selector=%#x, base=0x%x, limit=0x%x, flags=0x%x\n",
        segno, (uint32_t)vcpu.segs[segno].selector,
        (uint32_t)vcpu.segs[segno].base,
        (uint32_t)vcpu.segs[segno].limit, (uint32_t)vcpu.segs[segno].flags);
  }

  if (limit + 1 > SEL_BASE) {
    /* There is no space in the GDT to put monitor segments. We need to
     * handle this case differently. */
    NOT_IMPLEMENTED();
  }

  DBGn(GDT, "%s() %d: gdt=%p, sizeof gdt=0x%x, SEL_BASE=0x%x\n", __func__,
      __LINE__, gdt, sizeof gdt, SEL_BASE);
  /* Set all guest descriptors to zero. Fill them up lazily on receiving
   * #GPF. */
  memset(gdt, 0x0, SEL_BASE);
}

void
load_seg_cache(int segno, unsigned int selector, target_ulong base,
    unsigned int limit, unsigned int flags)
{
  segcache_t *sc;
  sc = &vcpu.segs[segno];
  sc->selector = selector | 0x3;
  vcpu.orig_segs[segno] = selector;
  sc->base = base;
  sc->limit = limit;
  sc->flags = flags;
  DBGn(GDT, "%s() %d: vcpu.segs[%d].selector=0x%x\n", __func__, __LINE__, segno,
      vcpu.segs[segno].selector);
  if ((!(vcpu.cr[0] & CR0_PE_MASK)) || selector >= SEL_BASE) {
    /* In this case, we use shadow descs. */
    vcpu.segs[segno].selector = (SEL_SHADOW + (segno << 3)) | 0x3;
  }
  DBGn(GDT, "%s() %d: vcpu.segs[%d].selector=0x%x\n", __func__, __LINE__, segno,
      vcpu.segs[segno].selector);
  gdt_make_shadow_segdesc(segno);
	if (segno == R_CS) {
		int user = vcpu_get_privilege_level();
		switch_to_shadow(user);
	}
}

void
segcache_sync(int segno)
{
  if (vcpu.segs[segno].selector < SEL_BASE) {
    uint32_t e1, e2;
    /* printf("%s() %d: segno=%d, selector=%#x. SEL_BASE=%#x\n", __func__,
        __LINE__, segno, (uint32_t)vcpu.segs[segno].selector, SEL_BASE); */
    read_segment(&e1, &e2, vcpu.orig_segs[segno], false, false);
    vcpu.segs[segno].base = get_seg_base(e1, e2);
    vcpu.segs[segno].limit = get_seg_limit(e1, e2);
    /* printf("%s(%d): e1=0x%x, e2=0x%x, base=0x%x, limit=0x%x\n", __func__,
        segno, e1, e2, vcpu.segs[segno].base, vcpu.segs[segno].limit); */
    vcpu.segs[segno].flags = e2;
  }
}
