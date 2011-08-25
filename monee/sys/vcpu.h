#ifndef SYS_VCPU_H
#define SYS_VCPU_H
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <lib/types.h>
#include "hw/i8259.h"
#include "mem/pt_mode.h"
#include "peep/insntypes.h"
#include "sys/interrupt.h"
#include "sys/loader.h"
#include "sys/vcpu_consts.h"
#include "sys/mode.h"
#include "sys/monitor.h"


/* Hidden flags. */
#define HF_CS32_SHIFT 1
#define HF_CS64_SHIFT 2

/*
#define HF_CS32_MASK  (1 << HF_CS32_SHIFT)
#define HF_CS64_MASK  (1 << HF_CS64_SHIFT)
*/

//#define MAX_MEM_SIZE (0x4b0 << 10)          //1200 KB : min needed to boot pintos
//#define MAX_MEM_SIZE (0xc00000)              //12 MB: min needed to boot monitor
#define MAX_MEM_SIZE (0x400000)              //4 MB
//#define MAX_MEM_SIZE (0x80ULL << 30)      // 128 GB

typedef enum {
  cpu_mode_16 = 0,
  cpu_mode_32,
  cpu_mode_any,
} cpu_mode_t;

typedef struct desc_table_t {
  uint32_t base;
  uint32_t limit;
} desc_table_t;

typedef struct segcache_t {
  uint32_t selector;
  target_ulong base;
  uint32_t limit;
  uint32_t flags;
} segcache_t;

typedef struct vcpu_segments_t {
  segcache_t ldt;
  segcache_t tr;
} vcpu_segments_t;

typedef struct vcpu_interrupts_t {
  uint8_t vector[256];
  uint8_t pending;
} vcpu_interrupts_t;

#define MAX_FUNC_DEPTH 4
#define MAX_FUNC_CODESIZE 256
struct func_struct {
	void *func;
  uint32_t func_n_args;
  uint32_t func_args[4];
  void *func_next;
  uint32_t func_stack[4];
  void *func_tc_done;
  uint8_t func_tc_buf[MAX_FUNC_CODESIZE];
  struct intr_frame func_intr_frame;
	struct monitor_t func_monitor_state;
	struct monitor_t *last_monitor_context;
	mode_t mode;
};

struct FILE;
struct PicState2;

typedef struct vcpu_t {
  void (*eip)(void);
  void (*eip_executing)(void);			/* To handle exceptions during callouts. */
  target_ulong regs[NUM_REGS];      /* a, c, d, b, sp, bp, si, di */
  uint32_t eflags;

  segcache_t ldt;
  segcache_t tr;
  desc_table_t gdt;

  /* segments */
  segcache_t segs[NUM_SEGS];    /* es, fs, gs, ds, ss, cs */
  uint32_t orig_segs[NUM_SEGS]; /* used only if segs[i].selector >= SEL_BASE. */
  desc_table_t idt;
  uint32_t cr[NUM_CRS];
  //target_ulong sti_fallthrough;
	
	/* IF can take values 0, 1, and 2. The '2' value signifies that the
	 * interrupts must be enabled after the execution of the next instruction. */
  uint16_t IF;            /* We use 16-bit storage so that we can use 'w'
                             variants of temporary regs. 'b' variants have
                             to deal with mess of different storage. */
  uint16_t IOPL;
  uint16_t AC;
  uint32_t a20_mask;
  int halted:1;

  unsigned char fxstate[512] __attribute__((aligned(16)));

  /* The default value of gs register. Used to restore %gs. */
  uint32_t default_user_gs;

  void *tc_ptr;

  /* page tables:
   *    shadow_page_dir[0] is the one that hardware sees when guest is
	 *    	at supervisor privilege level.
   *    shadow_page_dir[1] is the one that hardware sees when guest is
	 *    	at user privilege level.
   *    phys_map (declared later) is the PA->MA mapping.
   */
  uint32_t *shadow_page_dir[2];
	long long cur_mtraces_version;

  /* Direct jump chaining. */
  uint8_t *prev_tb;
  unsigned long edge;

	/* Emulated I/O devices. */
	struct PicState2 isa_pic;

  /* R/R functionality. */
  struct FILE *record_log;
  struct FILE *replay_log;
  uint64_t n_exec;
  uint32_t temporaries[MAX_TEMPORARIES];
  uint32_t scratch[8];
  uint32_t jtarget;     /* for indir jump. cannot use scratch to support
                           interrupt handling. */
	/* Replay only. */
	uint64_t replay_last_entry_n_exec;

  /* callouts. */
  void *callout;
  uint32_t callout_n_args;
  uint32_t callout_args[8];
  void *callout_cur;
  void *callout_next;
  void (*callout_label)(void);
  int next_eip_is_set;

  /* func() calls. */
  void (*func_label)(void);
  void *func_monitor_eip;
	struct func_struct func_struct[MAX_FUNC_DEPTH];
	struct func_struct cur_func_struct;
	struct monitor_t func_intr_frame_monitor_state;
	int func_depth;

  /* interrupt handling. */
  int interrupt_request;

  /* callouts and funcs. */
  void (*tc_label)(void);

  /* tb replacement. */
  //struct tb_t *locked_tb;

  /* exception handling. */
  int exception_index;
  int error_code;
  int exception_is_int;
  uint32_t exception_next_eip;
  target_ulong exception_cr2;
  jmp_buf jmp_env;              /* This must be the last element. It interferes
                                   with offset computation. */
} vcpu_t;

extern uint32_t *phys_map;

extern vcpu_t vcpu;

target_ulong vcpu_get_eip(void);
bool vcpu_equal(vcpu_t const *cpu1, vcpu_t const *cpu2);
uint64_t get_n_exec(const void *tc_ptr);
void cpu_interrupt(int mask);
void cpu_reset_interrupt(int mask);
int vcpu_get_privilege_level(void);

extern struct BlockDriverState *hda_bdrv, *hdb_bdrv, swap_bdrv,
       rr_log_init_dump_bdrv;

#define get_sp_mask(e2) ({                  \
  long ret;                                 \
  if ((e2) & DESC_B_MASK) {                 \
    ret = 0xffffffff;                       \
  } else {                                  \
    ret = 0xffff;                           \
  }                                         \
  ret;                                      \
})

#define PUSHW(ssp, sp, sp_mask, val) do {                                      \
  sp -= 2;                                                                     \
  stw_kernel((ssp) + (sp & (sp_mask)), (val));                                 \
} while (0)

#define PUSHL(ssp, sp, sp_mask, val) do {                                      \
  sp -= 4;                                                                     \
  stl_kernel((ssp) + (sp & (sp_mask)), (val));                                 \
} while (0)

#define POPW(ssp, sp, sp_mask, val) do {                                      \
  val = lduw_kernel((ssp) + (sp & (sp_mask)));                                \
  sp += 2;                                                                    \
} while(0)

#define POPL(ssp, sp, sp_mask, val) do {                                      \
  val = (uint32_t)ldl_kernel((ssp) + (sp & (sp_mask)));                       \
  sp += 4;                                                                    \
} while(0)


#define SET_ESP(val, sp_mask) \
  vcpu.regs[R_ESP] = (vcpu.regs[R_ESP] & ~(sp_mask)) | ((val) & (sp_mask))



#define reset_stack() do {                                                    \
  asm("movl %0, %%esp ; movl %%esp, %%ebp ; movl $0x0, (%%ebp)" : :           \
      "g"((uint8_t *)thread_current() + PGSIZE -                              \
          2*sizeof(struct intr_handler_stack_frame)));                        \
} while(0)

#ifdef __MONITOR__
#define ld(ptr, type, suffix) ({						    															\
		type val = 0;																															\
		target_ulong addr = (target_ulong)(ptr);																	\
		ASSERT(addr < LOADER_MONITOR_VIRT_BASE);  /* XXX: for now. */							\
		asm volatile ("mov %0, %%gs" :: "r"(SEL_GDSEG));													\
		asm volatile ("mov" xstr(suffix) " %%gs:(%1), %0": "=q"(val): "r"(addr):	\
			"memory"); 																															\
		asm volatile ("mov %0, %%gs" :: "r"(SEL_UDSEG));													\
		val;																																			\
		 })
#define st(ptr, val, type, suffix) ({				    															\
		target_ulong addr = (target_ulong)(ptr);																	\
		ASSERT(addr < LOADER_MONITOR_VIRT_BASE);	/* XXX: for now.	*/						\
		asm volatile ("mov %0, %%gs" :: "r"(SEL_GDSEG));													\
		asm volatile ("mov" xstr(suffix) " %0, %%gs:(%1)":: "q"(val),"r"(addr):		\
			"memory"); 																															\
		asm volatile ("mov %0, %%gs" :: "r"(SEL_UDSEG));													\
		 })
#else
#define ld(ptr, type, suffix) (*(type *)(ptr))
#define st(ptr, val, type, suffix) do { *(type *)(ptr) = (val); } while(0)
#endif

#define ld_kernel(ptr, type, suffix)  ({																			\
		type ret;																																	\
		pt_mode_t pt_mode;																												\
		/* ld_kernel may cause a TRACED page fault. Hence, it should always				\
		 * execute at cpl 3. */																										\
		ASSERT(read_cpl() == 3);																									\
		pt_mode = switch_to_shadow(0);																						\
		ret = ld(ptr, type, suffix);																							\
		switch_pt(pt_mode);																												\
		ret;																																			\
		})

#define st_kernel(ptr, val, type, suffix)  ({																	\
		pt_mode_t pt_mode;																												\
		/* st_kernel may cause a TRACED page fault. Hence, it should always				\
		 * execute at cpl 3. */																										\
		ASSERT(read_cpl() == 3);																									\
		pt_mode = switch_to_shadow(0);																						\
		st(ptr, val, type, suffix);																								\
		switch_pt(pt_mode);																												\
		})

#define ld_phys(ptr, type, suffix)  ({																				\
		type ret;																																	\
		pt_mode_t pt_mode;																												\
		pt_mode = switch_to_phys();																								\
		ret = ld(ptr, type, suffix);																							\
		switch_pt(pt_mode);																												\
		ret;																																			\
		})

#define st_phys(ptr, val, type, suffix)  ({																		\
		pt_mode_t pt_mode;																												\
		pt_mode = switch_to_phys();																								\
		st(ptr, val, type, suffix);																								\
		switch_pt(pt_mode);																												\
		})

static inline uint32_t
ldub(target_ulong ptr) {
	return ld(ptr, uint8_t, b);
}
static inline uint32_t
lduw(target_ulong ptr) {
	return ld(ptr, uint16_t, w);
}
static inline uint32_t
ldl(target_ulong ptr) {
	return ld(ptr, uint32_t, l);
}
static inline void
stub(target_ulong ptr, uint32_t val)	{
	st(ptr, val, uint8_t, b);
}
static inline void
stuw(target_ulong ptr, uint32_t val)	{
	st(ptr, val, uint16_t, w);
}
static inline void
stl(target_ulong ptr, uint32_t val)	{
	st(ptr, val, uint32_t, l);
}

#define ldub_kernel(ptr) 			ld_kernel(ptr, uint8_t, b)
#define lduw_kernel(ptr) 			ld_kernel(ptr, uint16_t, w)
#define ldl_kernel(ptr) 			ld_kernel(ptr, uint32_t, l)

#define stb_kernel(ptr, val) 	st_kernel(ptr, val, uint8_t, b)
#define stw_kernel(ptr, val) 	st_kernel(ptr, val, uint16_t, w)
#define stl_kernel(ptr, val) 	st_kernel(ptr, val, uint32_t, l)

#define ldub_phys(ptr) 				ld_phys(ptr, uint8_t, b)
#define lduw_phys(ptr) 				ld_phys(ptr, uint16_t, w)
#define ldl_phys(ptr) 				ld_phys(ptr, uint32_t, l)

#define stb_phys(ptr, val) 		st_phys(ptr, val, uint8_t, b)
#define stw_phys(ptr, val) 		st_phys(ptr, val, uint16_t, w)
#define stl_phys(ptr, val) 		st_phys(ptr, val, uint32_t, l)


uint32_t ldl_kernel_dont_set_access_bit(target_ulong vaddr);

#define CPU_INTERRUPT_HARD        0x02

#endif
