#include "sys/rr_log.h"
#include <stdio.h>
#include <string.h>
#include "devices/disk.h"
#include "threads/thread.h"
#include "mem/paging.h"
#include "mem/vaddr.h"
#include "mem/palloc.h"
#include "mem/malloc.h"
#include "mem/md5.h"
#include "mem/pt_mode.h"
#include "mem/vaddr.h"
#include "peep/tb.h"
#include "sys/bootsector.h"
#include "sys/interrupt.h"
#include "sys/loader.h"
#include "sys/mode.h"
#include "sys/init.h"
#include "sys/io.h"
#include "sys/vcpu.h"

#define RR_LOG_MACHINE_STATE_SIZE 2560

#ifndef REC_THRESHOLD
#define REC_THRESHOLD 0
#endif

#ifndef REC_PRINT_FREQ
#define REC_PRINT_FREQ 0
#endif

#ifndef RECORD_DISK
#define RECORD_DISK QEMU:record.wr.fifo
//#define RECORD_DISK QEMU:mrep_disk
//#define RECORD_DISK U3_Cruzer_Micro
#endif

#ifndef REPLAY_DISK
#define REPLAY_DISK QEMU:replay.dsk
#endif
extern size_t ram_pages;

static vcpu_t vcpu_copy;

off_t record_log_disk_begin = 0; //0x1000000;

char last_entry_tag[16];
//uint64_t last_entry_n_exec;
//target_ulong last_entry_eip_virt;
uint64_t last_entry_tell;
static uint64_t prev_n_exec;

target_ulong rr_log_panic_eip = 0;

/* Helper functions. */
static void rr_callbacks(char const *tag, bool replay);

static void
read_next_tag(void) {
  int num_read;
  uint32_t dummy;
	uint32_t last_entry_size;
  num_read = replay_log_scanf("%[^:]: %016llx %08lx %08x:", last_entry_tag,
        &vcpu.replay_last_entry_n_exec, &last_entry_size, &dummy);
  ASSERT(strlen(last_entry_tag)<sizeof(last_entry_tag));
  last_entry_tell = replay_log_tell();
  ASSERT(num_read == 4);
}

uint64_t
replay_log_tell(void)
{
  return ftello(vcpu.replay_log);
}

uint64_t
record_log_tell(void)
{
  ASSERT(vcpu.record_log);
  return ftello(vcpu.record_log);
}

int
replay_log_scanf(char const *format, ...)
{
  va_list args;
  int retval;
  int n_args = 0;
  char const *ptr;
  static int n_lines = 1;
  
  ASSERT(vcpu.replay_log);

  ptr = format;
  while ((ptr = strchr(ptr, '%'))) {
    if (*(ptr + 1) != '%' && *(ptr + 1) != '*') {
      n_args++;
      ptr = ptr + 1;
    } else {
      ptr = ptr + 2;
    }
  }

  va_start(args, format);
  retval = vfscanf(vcpu.replay_log, format, args);
  va_end(args);

  if (retval != n_args) {
    ERR("%#llx: Error scanning replay log. Expected '%s'[%d args] at line %d, "
        "matched %d args.\n", vcpu.n_exec, format, n_args, n_lines+1, retval);
    ABORT();
  }
  n_lines++;

  return retval;
}

int
record_log_printf(char const *format, ...)
{
  va_list args;
  int retval;

  ASSERT(vcpu.record_log);
  va_start(args, format);
  retval = vfprintf (vcpu.record_log, format, args);
  va_end(args);

  return retval;
}

static void
rr_log_identify_disks(void)
{
  struct disk *record_log_disk = NULL, *replay_log_disk = NULL;
	char const *record_disk_name, *replay_disk_name;

	if (record_disk_name = rr_log_get_record_disk_name()) {
		record_log_disk = identify_disk_by_name(record_disk_name);
	}
  if (record_log_disk) {
		int seek;
    /* hdb is present. Use record mode. */
    vcpu.record_log = fopen(record_log_disk, "rw");
		seek = fseeko(vcpu.record_log, record_log_disk_begin, SEEK_SET);
		ASSERT(seek == 0);
    ASSERT(vcpu.record_log);
    printf("Record log present... %s [%'zu-%'zu sectors]\n",
        record_disk_name, (size_t)record_log_disk_begin/DISK_SECTOR_SIZE,
				disk_size(record_log_disk));
  }

	if (replay_disk_name = rr_log_get_replay_disk_name()) {
		replay_log_disk = identify_disk_by_name(replay_disk_name);
	}
  if (replay_log_disk) {
    printf("Replay log present... [%'zu sectors]\n",
        disk_size(replay_log_disk));
    vcpu.replay_log = fopen(replay_log_disk, "rw");
    ASSERT(vcpu.replay_log);
  }
}

void
rr_log_start(void)
{
	vcpu.replay_last_entry_n_exec = 0;
	if (vcpu.replay_log) {
		//cpu_reset_interrupt(CPU_INTERRUPT_HARD);
		//ASSERT(vcpu.interrupt_request == 0);
		read_next_tag();
		ASSERT(!strcmp(last_entry_tag, "MS"));
	} else {
		prev_n_exec = vcpu.n_exec;
	}
  rr_log_vcpu_state(-1);
	tb_flush();
	swap_flush();
#ifdef TRANSLATE
  vcpu_set_log(VCPU_LOG_TRANSLATE);
#endif
#ifdef IN_ASM
  vcpu_set_log(VCPU_LOG_IN_ASM);
#endif
#ifdef OUT_ASM
  vcpu_set_log(VCPU_LOG_IN_ASM | VCPU_LOG_OUT_ASM);
#endif
#ifdef TU_SIZE
  set_max_tu_size(TU_SIZE);
#else
	set_max_tu_size(MAX_TU_SIZE);
#endif
  if (vcpu.replay_log) {
    set_max_tu_size(1);     /* for replay, use single instruction tb's
                               to eliminate duplicate translations. 
                               to make fcallout_patch_pc() simpler. */
  }
}

void
rr_log_init (void)
{
	rr_log_identify_disks();
	rr_log_start();
#ifdef PANIC_EIP
	rr_log_panic_eip = PANIC_EIP;
#endif
}

void
record_log_flush(void)
{
  if (vcpu.record_log) {
    fflush(vcpu.record_log);
  }
}

static ssize_t
replay_log_read(void *buf, size_t count)
{
  /* plain fread() does not take care of read-only portions of memory. */
	//printf("%s() %d:\n", __func__, __LINE__);
  return fread(buf, 1, count, vcpu.replay_log);
	/*
  uint8_t *sector;
  uint8_t *ptr = buf;
  uint8_t *end = ptr + count;
  sector = malloc(DISK_SECTOR_SIZE);
	ASSERT(sector);
  do {
    size_t num_read, num_chars;

    num_chars = min(end - ptr, DISK_SECTOR_SIZE);
    num_read = fread(sector, 1, num_chars, vcpu.replay_log);
    hw_memcpy(ptr, sector, num_read);
    ptr += num_read;
    if (num_read < num_chars) {
      break;
    }
    ASSERT(num_read == num_chars);
  } while (ptr < end);
  free(sector);
  return ptr - (uint8_t *)buf;
	*/
}

static ssize_t
replay_log_mem_read(void *buf, size_t count)
{
  ASSERT(vcpu.replay_log);
  return replay_log_read(buf, count);
}

static ssize_t
record_log_write(void *buf, size_t count)
{
  ASSERT(vcpu.record_log);
  return fwrite(buf, 1, count, vcpu.record_log);
}

static ssize_t
record_log_mem_write(void *buf, size_t count)
{
  ssize_t ret;
  ASSERT(vcpu.record_log);
	//printf("%s(): count=%'zu\n", __func__, count);
  ret = record_log_write(buf, count);
	//printf("%s(): done.\n", __func__);
  return ret;
}

static ssize_t
replay_log_cmp(void *buf, size_t count)
{
	static uint8_t *page = NULL;
  uint8_t *ptr, *end;

	if (!page) {
		page = palloc_get_page(PAL_ASSERT);
		ASSERT(page);
	}
  ptr = buf;
  end = ptr + count;
  do {
    size_t num_read;
    int cmp;
    num_read = fread(page, 1, min(PGSIZE, end - ptr), vcpu.replay_log);
    ASSERT((int)num_read == min(PGSIZE, end - ptr));
    cmp = memcmp(page, ptr, num_read);
    if (cmp != 0) {
      unsigned i;
      for (i = 0; i < num_read; i++) {
        if (page[i] != ptr[i]) {
          ERR("%#llx: Mismatch in memory at position %#x. Expected '%#hhx'"
              "(actual), got '%#hhx'(log).\n", vcpu.n_exec,
              (ptr - (uint8_t *)buf) + i,
              ptr[i], page[i]);
          ABORT();
        }
      }
      ABORT();
    }
    ptr += num_read;
  } while (ptr < end);
  return count;
}

static ssize_t
replay_log_mem_cmp(void *buf, size_t count)
{
  ASSERT(vcpu.replay_log);
  return replay_log_cmp(buf, count);
}

/******************************************************************************
 * Log machine state.
 ******************************************************************************/


#define append_space(numchars, size, func) do {                               \
  int i;                                                                      \
  if (func == record_log_printf) {                                            \
    ASSERT(!(size) || ((int)(numchars) < (int)(size)));                       \
    for (i = (int)(numchars); i < (int)(size) - 1; i++) {                     \
      func(" ");                                                              \
    }                                                                         \
  } else if (func == replay_log_scanf) {                                      \
    func(" ");                                                                \
  }                                                                           \
} while(0)


#define rw_state(func, vcpu, prefix, rr, rw_func) do {                        \
  int i;                                                                      \
  uint64_t ms_start, ms_stop;                                                 \
  target_phys_addr_t cr3;                                                     \
  target_ulong eip_virt;                                            					\
  void *eip;                                                                  \
  unsigned num_read;                                                          \
  uint64_t cur_n_exec = get_n_exec(vcpu.callout_next);                        \
  uint32_t mem_size = min(ram_pages * PGSIZE, (uint32_t)MAX_MEM_SIZE);        \
  int len = RR_LOG_MACHINE_STATE_SIZE + mem_size;                             \
  eip = vcpu.eip;                                                             \
	vcpu.eflags |= IF_MASK;	/* this bit is redundant, so just set it always. */ \
  if (func == replay_log_scanf) {                                             \
    ASSERT(!strcmp(last_entry_tag, "MS"));                                    \
    ms_start = last_entry_tell;                                               \
		num_read = replay_log_scanf(" %x:", prefix eip);													\
		ASSERT(num_read == 1);																										\
    vcpu.n_exec = vcpu.replay_last_entry_n_exec;                              \
  } else {                                                                    \
    eip_virt = vcpu_get_eip();                                                \
    func("MS:  %016llx %08lx %08x:", prefix cur_n_exec, prefix len, 0);       \
    ms_start = rr##_log_tell();                                               \
    func(" %#x:", eip);                                         							\
  }                                                                           \
  func(" %p: machine_state_start\n", prefix eip);                             \
  func("\tregs:\n");                                                          \
  for (i = 0; i < NUM_REGS; i++) {                                            \
    func("\t\t%d: %08x\n", prefix i, prefix vcpu.regs[i]);                    \
  }                                                                           \
  func("\teip: %08x\n", prefix eip);                                          \
  func("\teflags: %08x\n", prefix vcpu.eflags);                               \
  func("\tldt: [%04x,%08x,%08x,%08x]\n", prefix vcpu.ldt.selector,            \
      prefix vcpu.ldt.base, prefix vcpu.ldt.limit,                            \
      prefix vcpu.ldt.flags);                                                 \
  func("\ttr: [%04x,%08x,%08x,%08x]\n", prefix vcpu.tr.selector,              \
      prefix vcpu.tr.base, prefix vcpu.tr.limit,                              \
      prefix vcpu.tr.flags);                                                  \
  func("\tgdt: [%08x,%08x]\n", prefix vcpu.gdt.base, prefix vcpu.gdt.limit);  \
  func("\tidt: [%08x,%08x]\n", prefix vcpu.idt.base,                          \
      prefix vcpu.idt.limit);                                                 \
  func("\tcr:\n");                                                            \
  for (i = 0; i < NUM_CRS; i++) {                                             \
    func("\t\t%d: %08x\n", prefix i, prefix vcpu.cr[i]);                      \
  }                                                                           \
  func("\tIF: %hx\n", prefix vcpu.IF);                                        \
  func("\tIOPL: %hx\n", prefix vcpu.IOPL);                                    \
  func("\tAC: %hx\n", prefix vcpu.AC);                                        \
  func("\ta20_mask: %x\n", prefix vcpu.a20_mask);                             \
  func("\tsegs:\n");                                                          \
  for (i = 0; i < NUM_SEGS; i++) {                                            \
    func("\t\t%d: [%04x,%08x,%08x,%08x]\n", prefix i,                         \
        prefix vcpu.orig_segs[i], prefix vcpu.segs[i].base,                   \
        prefix vcpu.segs[i].limit, prefix vcpu.segs[i].flags);                \
  }                                                                           \
  func("\tfxstate:\n");                                                       \
  for (i = 0; i < 512; i++) {                                                 \
    func(" %02hhx", prefix vcpu.fxstate[i]);                                  \
  }                                                                           \
  func("\n");                                                                 \
  func("\tmem[%x]:\n", prefix mem_size);                                      \
  num_read = rw_func((void *)0, mem_size);                                    \
  ASSERT(num_read == mem_size);                                               \
  func("\n");                                                                 \
  func("%016llx %p: machine_state_stop", prefix cur_n_exec,                   \
      prefix eip);                                                            \
  if (func == replay_log_scanf) {                                             \
    vcpu.eip = eip;                                                           \
  }                                                                           \
  ms_stop = rr##_log_tell();                                                  \
  append_space(ms_stop - ms_start, len, func);                                \
  func("\n");                                                                 \
} while(0)


static void
sync_segcache_phy(void)
{
  int i;
  for (i = 0; i < NUM_SEGS; i++) {
    /* printf("%s() %d: i=%d, selector=%#x. SEL_BASE=%#x\n", __func__, __LINE__,
        i, (uint32_t)vcpu.segs[i].selector, SEL_BASE); */
    if (vcpu.segs[i].selector < SEL_BASE) {
      uint32_t e1, e2;
      pt_mode_t pt_mode;
      target_ulong desc_virt = vcpu.gdt.base + (vcpu.orig_segs[i] & ~0x7);
      target_phys_addr_t desc_phys = pt_walk((void *)vcpu.cr[3], desc_virt,
          NULL, NULL, PTWALK_ASSERT);
      e1 = ldl_phys((void *)desc_phys);
      e2 = ldl_phys((uint8_t *)desc_phys + 4);
      vcpu.segs[i].base = get_seg_base(e1, e2);
      vcpu.segs[i].limit = get_seg_limit(e1, e2);
      vcpu.segs[i].flags = e2;
      /* printf("%s() %d: i=%d, base=%#x,limit=%#x,flags=%#x.\n", __func__,
          __LINE__, i, (uint32_t)vcpu.segs[i].base,
          (uint32_t)vcpu.segs[i].limit, (uint32_t)vcpu.segs[i].flags); */
    }
  }
}

bool
rr_log_lockstep_mode(void)
{
#if REC_PRINT_FREQ
  return true;
#else
  return false;
#endif
}

#define rr_log_intr_generic(intno, func, prefix, rr) do {                     \
  uint64_t intr_start, intr_stop;                                             \
  size_t len = RR_LOG_ENTRY_SIZE;                                             \
  if (func == replay_log_scanf) {                                             \
		target_ulong eip;																													\
		int num_read;																															\
    ASSERT(!strcmp(last_entry_tag, "INTR"));                                  \
    intr_start = last_entry_tell;                                             \
		num_read = replay_log_scanf(" %x:", prefix eip);													\
		ASSERT(num_read == 1);																										\
		ASSERT(eip == (target_ulong)vcpu.eip);																		\
  } else {                                                                    \
    target_ulong eip_virt;                                          					\
    eip_virt = vcpu_get_eip();                                                \
    record_log_printf("INTR:%016llx %08lx %08x:", vcpu.n_exec, len, 0);       \
    intr_start = record_log_tell();                                           \
    rr##_log_printf(" %#x:", vcpu.eip);                         							\
  }                                                                           \
  func(" %x", prefix intno);                                                  \
  intr_stop = rr##_log_tell();                                                \
  append_space(intr_stop - intr_start, len, func);                            \
  func("\n");                                                                 \
  if (func == replay_log_scanf) {                                             \
		char tag[16];																															\
		strlcpy(tag, last_entry_tag, sizeof tag);																	\
    read_next_tag();                                                          \
		rr_callbacks(tag, true);																									\
  } else {                                                                    \
		rr_callbacks(last_entry_tag, false);																			\
	}																																						\
} while(0)

static void
record_dump_state(void)
{
	int logflags;
	pt_mode_t pt_mode;

	logflags = vcpu_get_log_flags();
	vcpu_clear_log(VCPU_LOG_EXCP);
	pt_mode = switch_to_phys();
	/* sync segcache to print out the correct values. */
	sync_segcache_phy();
	rw_state(record_log_printf, vcpu, , record, record_log_mem_write);

	record_log_flush();
	switch_pt(pt_mode);
	vcpu_set_log(logflags);
	pic_save_state(&vcpu.isa_pic);
}

void
rr_log_vcpu_state(int n_exec)
{
	extern bool micro_replay_on;
	if (micro_replay_on) printf("%s() %d: vcpu.n_exec=%llx, vcpu.eip=%p\n",
			__func__, __LINE__, vcpu.n_exec, vcpu.eip);
  if (vcpu.replay_log) {
    if (n_exec < 0) {
      pt_mode_t pt_mode;
      int i, logflags;

      ASSERT(n_exec == -1);
			logflags = vcpu_get_log_flags();
			vcpu_clear_log(VCPU_LOG_EXCP);
      pt_mode = switch_to_phys();
      rw_state(replay_log_scanf, vcpu, &, replay, replay_log_mem_read);
      switch_pt(pt_mode);
			vcpu_set_log(logflags);
			pic_load_state(&vcpu.isa_pic);
      read_next_tag();
			shadow_pagedir_sync();
      for (i = 0; i < 6; i++) {
        if (!(vcpu.cr[0] & CR0_PE_MASK) || vcpu.orig_segs[i] >= SEL_BASE) {
          vcpu.segs[i].selector = (SEL_SHADOW + (i << 3)) | 0x3;
        } else {
          vcpu.segs[i].selector = vcpu.orig_segs[i] | 0x3;
        }
        gdt_make_shadow_segdesc(i);
      }
      if (vcpu.a20_mask == 0xffffffff) {
        paging_enable_a20();
      }
      vcpu.edge = 2;
      clear_fcallout_patches();
      //XXX deal with other orig elements: idt, gdt, etc...
		} else {
			int num_iter = 0;
			while (vcpu.replay_last_entry_n_exec <= vcpu.n_exec) {
				/*
				printf("%s() %d: vcpu.n_exec=%llx,vcpu.replay_last_entry_n_exec=%llx, "
						"vcpu.eip=%p\n", __func__, __LINE__, vcpu.n_exec,
						vcpu.replay_last_entry_n_exec, vcpu.eip);
						*/
				if (num_iter++ > 100) {
					printf("%s() %d: vcpu.n_exec=%llx, replay_last_entry_n_exec=%llx.\n",
							__func__, __LINE__, vcpu.n_exec, vcpu.replay_last_entry_n_exec);
				}
				if (   vcpu.n_exec != vcpu.replay_last_entry_n_exec
						&& vcpu.n_exec != vcpu.replay_last_entry_n_exec + 1) {
					printf("n_exec=%d, vcpu.n_exec = %llx, "
							"vcpu.replay_last_entry_n_exec=%llx\n", n_exec,
							vcpu.n_exec, vcpu.replay_last_entry_n_exec);
				}
				ASSERT(   vcpu.n_exec == vcpu.replay_last_entry_n_exec
						   || vcpu.n_exec == vcpu.replay_last_entry_n_exec + 1);
				uint64_t cur_n_exec = get_n_exec(vcpu.callout_next);
				ASSERT(cur_n_exec == vcpu.n_exec - 1 || cur_n_exec == vcpu.n_exec);
				if (cur_n_exec != vcpu.replay_last_entry_n_exec) {
					ASSERT(vcpu.n_exec == vcpu.replay_last_entry_n_exec);
					ASSERT(cur_n_exec == vcpu.n_exec - 1);
					ASSERT(cur_n_exec == vcpu.replay_last_entry_n_exec - 1);
					/*
					printf("cur_n_exec=%llx, vcpu.replay_last_entry_n_exec=%llx\n",
							cur_n_exec, vcpu.replay_last_entry_n_exec);
							*/
					break;
				}
				/* printf("cur_n_exec=%llx, vcpu.replay_last_entry_n_exec=%llx\n",
						cur_n_exec, vcpu.replay_last_entry_n_exec); */
				if (!strcmp(last_entry_tag, "MS") || !strcmp(last_entry_tag, "INTR")) {
					ASSERT(cur_n_exec == vcpu.replay_last_entry_n_exec);
					if (!strcmp(last_entry_tag, "MS")) {
						uint64_t last = vcpu.replay_last_entry_n_exec;
						int i, logflags;
						pt_mode_t pt_mode;

						logflags = vcpu_get_log_flags();
						vcpu_clear_log(VCPU_LOG_EXCP);
						pt_mode = switch_to_phys();
						/* sync current segcache for correct comparison with vcpu_copy. */
						sync_segcache_phy();
						rw_state(replay_log_scanf, vcpu_copy, &, replay,replay_log_mem_cmp);
						switch_pt(pt_mode);
						vcpu_set_log(logflags);
						pic_load_state(&vcpu_copy.isa_pic);
						if (   !vcpu_equal(&vcpu, &vcpu_copy)
								|| !pic_states_equal(&vcpu.isa_pic, &vcpu_copy.isa_pic)) {
							printf("failed at %#llx. eip=%p\n", last, vcpu.eip);
							ABORT();
						}
						//printf("succeeded at %#llx. eip=%p\n", last, vcpu.eip);
						read_next_tag();
					} else if (!strcmp(last_entry_tag, "INTR")) {
						unsigned intno;
						if (vcpu.IF != 1) {
							printf("IF flag (%d) not set when interrupt raised at 0x%llx:%p\n",
									vcpu.IF, cur_n_exec, vcpu.eip);
						}
						ASSERT(vcpu.IF == 1);
						rr_log_intr_generic(intno, replay_log_scanf, &, replay);
						LOG(INT, "%#llx %p: Raising interrupt %#x\n", cur_n_exec, vcpu.eip,
								intno);
						vcpu.n_exec = cur_n_exec;
						raise_interrupt(intno, 0, -1, 0);
						NOT_REACHED();
					} else {
						ABORT();
					}
				} else if (!strcmp(last_entry_tag, "PANC")) {
					rr_callbacks(last_entry_tag, true);
					printf("Panic tag reached.\n");
					ABORT();
				} else if (!strcmp(last_entry_tag, "EXIT")) {
					rr_callbacks(last_entry_tag, true);
					printf("Exit tag reached. Shouldn't have happened -- "
							"On replay, the exit should happen with the same sequence as "
							"that during record. Aborting...\n");
					ABORT();
				} else {
					printf("%s(): Seen %s tag at %llx [vcpu.n_exec %llx, vcpu.eip %p, "
							"vcpu.replay_last_entry_n_exec %llx]\n", __func__, last_entry_tag,
							get_n_exec(vcpu.callout_next), vcpu.n_exec, vcpu.eip,
							vcpu.replay_last_entry_n_exec);
					rr_callbacks(last_entry_tag, true);
					NOT_REACHED();
				}
				/*
				printf("%s() %d: vcpu.n_exec=%llx,vcpu.replay_last_entry_n_exec=%llx, "
						"vcpu.eip=%p\n", __func__, __LINE__, vcpu.n_exec,
						vcpu.replay_last_entry_n_exec, vcpu.eip);
						*/
			}
		}
  }

  if (vcpu.record_log) {
    int64_t cur_n_exec = 0;

		//printf("vcpu.n_exec=%llx, prev_n_exec=%llx\n", vcpu.n_exec, prev_n_exec);
    ASSERT(vcpu.n_exec >= prev_n_exec);
    if (   n_exec <= 0 || rr_log_force_dump_on_next_tb_entry
        || (   REC_PRINT_FREQ && (int64_t)vcpu.n_exec >= REC_THRESHOLD
            && (int64_t)(vcpu.n_exec - prev_n_exec) >= REC_PRINT_FREQ
            && ((cur_n_exec = get_n_exec(vcpu.callout_next)) >= REC_THRESHOLD)
            && (int64_t)(cur_n_exec - prev_n_exec) >= REC_PRINT_FREQ)) {
      int i;
			if (n_exec < 0) {
				ASSERT(n_exec == -1);
        cur_n_exec = 0;
      } else {
				if (rr_log_force_dump_on_next_tb_entry) {
					cur_n_exec = get_n_exec(vcpu.callout_next);
					rr_log_force_dump_on_next_tb_entry = false;
				}
				if (n_exec == 0) {
					cur_n_exec = vcpu.n_exec;
				}
        printf("Dumping machine state to record log at 0x%llx...\n",
						cur_n_exec);
      }
			record_dump_state();
      if (n_exec >= 0) {
        prev_n_exec = cur_n_exec;
      }
    }
  }
}

void
record_log_finish(char const *tag)
{
  struct thread *t = running_thread();
  uint64_t cur_n_exec;
  int i;

  if (!vcpu.record_log) {
    return;
  }

	if (REC_PRINT_FREQ) {
		record_dump_state();
	}
  cur_n_exec = get_n_exec(vcpu.callout_next);
  ASSERT(cur_n_exec <= vcpu.n_exec);
	vcpu.n_exec = cur_n_exec;
  //record_log_printf("PANC:%016llx %08lx %08x:", cur_n_exec, 512, 0);
  record_log_printf("%s:%016llx %08lx %08x:", tag, vcpu.n_exec, 512, 0);
  for (i = 0; i < 1024; i++) {
    record_log_printf("%c", '0');
  }
	record_log_flush();
	/*
  if (is_thread(t)) {
    pt_mode_t pt_mode;
    pt_mode = switch_to_phys();
    record_log_flush();
    switch_pt(pt_mode);
  }
	*/
}

void
record_log_panic(void)
{
	record_log_finish("PANC");
}

void
record_log_shutdown(void)
{
	record_log_finish("EXIT");
}

/******************************************************************************
 * Log entry functions.
 ******************************************************************************/

#define rr_in_generic(func, prefix, cpu, name, type, printf_format, rr) do {  \
  uint64_t entry_start, entry_stop;                                           \
  uint64_t cur_n_exec = get_n_exec(vcpu.callout_next);                        \
  unsigned len = RR_LOG_ENTRY_SIZE;                                           \
  if (func == replay_log_scanf) {                                             \
    /* printf("last_entry_tag=%s. vcpu.replay_last_entry_n_exec=%#llx, "			\
				"vcpu.n_exec=%#llx, cur_n_exec=%#llx\n", last_entry_tag, 							\
				vcpu.replay_last_entry_n_exec, vcpu.n_exec, cur_n_exec); */           \
    ASSERT(!strcmp(last_entry_tag, "IN"));                                    \
  } else {                                                                    \
    func("IN:  %016llx %08lx", prefix cur_n_exec, prefix len);                \
    func(" %08hx", prefix rport);                                             \
    func(":");                                                                \
  }                                                                           \
  entry_start = rr##_log_tell();                                              \
  func(" " printf_format, prefix data);                                       \
  entry_stop = rr##_log_tell();                                               \
  append_space(entry_stop - entry_start, len, func);                          \
  func("\n");                                                                 \
  if (func == replay_log_scanf) {                                             \
		char tag[16];																															\
		strlcpy(tag, last_entry_tag, sizeof last_entry_tag);											\
    read_next_tag();                                                          \
		rr_callbacks(tag, true);																									\
  } else {                                                                    \
		rr_callbacks(last_entry_tag, false);																			\
	}																																						\
} while(0)

#define rr_in(name, type, printf_format)                                      \
  type rr_##name(uint16_t port) {                                             \
    type data;                                                                \
    uint16_t rport = port;                                                    \
    if (vcpu.replay_log && ioport_needs_log(port)) {                          \
      rr_in_generic(replay_log_scanf, &, vcpu_copy, name, type,               \
          printf_format, replay);                                             \
      ASSERT(rport == port);                                                  \
    } else {                                                                  \
      data = io_##name(port);                                                 \
    }                                                                         \
    if (vcpu.record_log && ioport_needs_log(port)) {                          \
      rr_in_generic(record_log_printf, ,vcpu, name,type,printf_format,record);\
    }                                                                         \
    return data;                                                              \
  }

#define rr_ins_generic(func, prefix, name, data_size, rr) do {                \
  uint64_t entry_start, entry_stop;                                           \
  unsigned i;                                                                 \
  unsigned len = max((size_t)RR_LOG_ENTRY_SIZE,																\
			strlen(#name)+cnt*(data_size*2+1)+1); 																	\
  if (func == replay_log_scanf) {                                             \
    ASSERT(!strcmp(last_entry_tag, "INS"));                                   \
  } else {                                                                    \
    uint64_t cur_n_exec;                                                      \
    cur_n_exec = get_n_exec(vcpu.callout_next);                               \
    func("INS: %016llx %08lx", prefix cur_n_exec, prefix len);                \
    func(" %08hx", prefix rport);                                             \
    func(":");                                                                \
  }                                                                           \
  entry_start = rr##_log_tell();                                              \
  for (i = 0; i < cnt; i++) {                                                 \
    char const *format;                                                       \
    if (data_size == 1) {                                                     \
      format = " %02hhx";                                                     \
      func(format, prefix (*((uint8_t *)addr + i)));                          \
    } else if (data_size == 2) {                                              \
      format = " %04hx";                                                      \
      func(format, prefix (*((uint16_t *)addr + i)));                         \
    } else if (data_size == 4) {                                              \
      format = " %08x";                                                       \
      func(format, prefix (*((uint32_t *)addr + i)));                         \
    }                                                                         \
  }                                                                           \
  entry_stop = rr##_log_tell();                                               \
  append_space(entry_stop - entry_start, len, func);                          \
  func("\n");                                                                 \
  if (func == replay_log_scanf) {                                             \
		char tag[16];																															\
		strlcpy(tag, last_entry_tag, sizeof last_entry_tag);											\
    read_next_tag();                                                          \
		rr_callbacks(tag, true);																									\
  } else {                                                                    \
		rr_callbacks(last_entry_tag, false);																			\
	}																																						\
} while (0)

rr_in(inb, uint8_t, "%hhx");
rr_in(inw, uint16_t, "%hx");
rr_in(inl, uint32_t, "%lx");

void
rr_ins(uint16_t port, void *addr, size_t cnt, size_t data_size)
{
  uint16_t rport = port;
  if (vcpu.replay_log && ioport_needs_log(port)) {
    rr_ins_generic(replay_log_scanf, &, rr_ins, data_size, replay);
    ASSERT(rport == port);
  } else {
    io_ins(port, addr, cnt, data_size);
  }
  if (vcpu.record_log && ioport_needs_log(port)) {
    rr_ins_generic(record_log_printf, , rr_ins, data_size, record);
  }
}

void rr_out(uint16_t port, target_ulong data, size_t data_size) {
	if (vcpu.replay_log && ioport_needs_log(port)) {
		rr_callbacks("OUT", true);
	} else {
		io_out(port, data, data_size);
	}
	if (vcpu.record_log && ioport_needs_log(port)) {
		rr_callbacks("OUT", false);
	}
}

void rr_outs(uint16_t port, const void *addr, size_t cnt, size_t data_size)
{
	if (vcpu.replay_log && ioport_needs_log(port)) {
		rr_callbacks("OUTS", true);
	} else {
		io_outs(port, addr, cnt, data_size);
	}
	if (vcpu.record_log && ioport_needs_log(port)) {
		rr_callbacks("OUTS", false);
	}
}

void
rr_interrupt(int intno, int error_code, target_ulong next_eip)
{
  if (vcpu.record_log) {
    rr_log_intr_generic(intno, record_log_printf, , record);
  }
}

char const *
rr_log_get_record_disk_name(void)
{
#ifdef RECORD_DISK
	return xstr(RECORD_DISK);
#endif
	return NULL;
}

char const *
rr_log_get_replay_disk_name(void)
{
#ifdef REPLAY_DISK
	return xstr(REPLAY_DISK);
#endif
	return NULL;
}

bool rr_log_force_dump_on_next_tb_entry = false;

#define MAX_RR_CALLBACKS 4
static struct {
	void (*callback)(char const *rr_tag, bool replay, void *opaque);
	void *opaque;
} rr_callbacks_arr[MAX_RR_CALLBACKS];
static int num_rr_callbacks = 0;

/* rr callbacks. */
void
rr_log_register_callback(
		void (*callback)(char const *rr_tag, bool replay, void *opaque),
		void *opaque)
{
	rr_callbacks_arr[num_rr_callbacks].callback = callback;
	rr_callbacks_arr[num_rr_callbacks].opaque = opaque;
	num_rr_callbacks++;
}

void
rr_log_unregister_callback(
		void (*callback)(char const *rr_tag, bool replay, void *opaque),
		void *opaque)
{
	int i, deleted;
	for (i = 0; i < num_rr_callbacks; i++) {
		if (   rr_callbacks_arr[i].callback == callback
				&& rr_callbacks_arr[i].opaque == opaque) {
			break;
		}
	}
	ASSERT(i < num_rr_callbacks);
	deleted = i;
	for (i = deleted + 1; i < num_rr_callbacks; i++) {
		rr_callbacks_arr[i - 1].callback = rr_callbacks_arr[i].callback;
		rr_callbacks_arr[i - 1].opaque = rr_callbacks_arr[i].opaque;
	}
	num_rr_callbacks--;
}

static void
rr_callbacks(char const *tag, bool replay)
{
	int i;
	for (i = 0; i < num_rr_callbacks; i++) {
		(*rr_callbacks_arr[i].callback)(tag, replay, rr_callbacks_arr[i].opaque);
	}
}
