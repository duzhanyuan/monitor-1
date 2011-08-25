#ifndef __LIB_DEBUG_H
#define __LIB_DEBUG_H
#include <lib/mdebug.h>

/* GCC lets us add "attributes" to functions, function
   parameters, etc. to indicate their properties.
   See the GCC manual for details. */
#define UNUSED __attribute__ ((unused))
#define NO_RETURN __attribute__ ((noreturn))
#define NO_INLINE __attribute__ ((noinline))
#define PRINTF_FORMAT(FMT, FIRST) __attribute__ ((format (printf, FMT, FIRST)))

/* Halts the OS, printing the source file name, line number, and
   function name, plus a user-specific message. */
#define PANIC(...) debug_panic (__FILE__, __LINE__, __func__, __VA_ARGS__)

void debug_panic (const char *file, int line, const char *function,
                  const char *message, ...) PRINTF_FORMAT (4, 5) NO_RETURN;
void debug_backtrace (void **frame);
void debug_backtrace_all (void);

extern int dbg_level;
#ifndef NDEBUG
#define DBGn(n,x,args...) do {                                                \
  if (dbg_level >= n) {                                                       \
    printf(x, ##args);                                                        \
  }                                                                           \
} while(0)
#define DBE(l,x) if (dbg_level >= l) do { x; } while(0)
#define DBG_(x,args...) DBGn(1, "[%s() %d] " x, __func__, __LINE__, ##args)
#define DBG(x,args...) DBGn(1, x, ##args)
#define ERR(x,args...) do {                                                   \
  DBGn(0,"Error at %s:%d\n%s(): " x, __FILE__, __LINE__, __func__, ##args);   \
} while(0)
#define MSG(x, args...) printf(x, ##args)
#else
#define DBGn(n,x,...)
#define DBE(l,x)
#define DBG_(x,...)
#define DBG(x,...)
#define ERR(x,...)
#define MSG(...)
#endif


#define VCPU_LOG_ALWAYS        (1 << 0)
#define VCPU_LOG_OUT_ASM       (1 << 1)
#define VCPU_LOG_IN_ASM        (1 << 2)
#define VCPU_LOG_HW            (1 << 3)
#define VCPU_LOG_INT           (1 << 4)
#define VCPU_LOG_EXCP          (1 << 5)
#define VCPU_LOG_USB           (1 << 6)
#define VCPU_LOG_PCALL         (1 << 7)
#define VCPU_LOG_IOPORT        (1 << 8)
#define VCPU_LOG_CPU           (1 << 9)
#define VCPU_LOG_TRANSLATE     (1 << 10)
#define VCPU_LOG_MTRACE        (1 << 11)
#define VCPU_LOG_PAGING        (1 << 12)
#define VCPU_LOG_TB 		       (1 << 13)

extern int loglevel;

#define LOG(n,x,args...) do {                                               \
  if (loglevel & VCPU_LOG_##n) {                                            \
    printf(x, ##args);                                                      \
  }                                                                         \
} while(0)


typedef struct vcpu_log_item_t {
  int mask;
  char const *name;
  char const *help;
} vcpu_log_item_t ;

extern vcpu_log_item_t vcpu_log_items[];
void vcpu_set_log(int log_flags);
int vcpu_get_log_flags(void);
void vcpu_clear_log(int log_flags);
int vcpu_str_to_log_mask(char const *str);

#endif  /* lib/debug.h */


/* This is outside the header guard so that debug.h may be
   included multiple times with different settings of NDEBUG. */
#undef ASSERT
#undef WARN_ON
#undef NOT_REACHED
#undef NOT_IMPLEMENTED
#undef NOT_TESTED

#ifndef NDEBUG
#define ASSERT(CONDITION...)                                 \
        if ((CONDITION)) { } else {                               \
                PANIC ("assertion `%s' failed.", #CONDITION);   \
        }
//#define ASSERT2(CONDITION)
#define ASSERT2 ASSERT
#define WARN_ON(CONDITION...)              \
  ({                \
   int __ret_warn_on = !!(CONDITION);      \
   if (unlikely(__ret_warn_on)) {       \
      PANIC("assertion `!(%s)' failed.", #CONDITION);      \
   }\
   unlikely(__ret_warn_on);        \
   })

#else
#define ASSERT(CONDITION...) ((void) 0)
#define ASSERT2(CONDITION...) ((void) 0)
#define WARN_ON(CONDITION...) ((void) 0)
#endif
#define NOT_REACHED() PANIC ("%s", "executed an unreachable statement");
#define NOT_IMPLEMENTED() PANIC ("%s", "not-implemented");
#define NOT_TESTED() do {																					\
	printf ("%s() %d: Not tested.\n", __func__, __LINE__);					\
	debug_backtrace(__builtin_frame_address(0));										\
} while(0)
#define ABORT() PANIC ("%s", "aborting");

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define prefetch(x) __builtin_prefetch(x)

