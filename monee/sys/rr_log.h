#ifndef SYS_RR_LOG_H
#define SYS_RR_LOG_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <types.h>


#define RR_LOG_ENTRY_SIZE 40

void rr_log_init (void);
void rr_log_start (void);
int replay_log_scanf(char const *format, ...);
int record_log_printf(char const *format, ...);
void record_log_flush(void);
void record_log_panic(void);
void record_log_shutdown(void);
void record_log_finish(char const *tag);
uint64_t replay_log_tell(void);
uint64_t record_log_tell(void);

void rr_log_vcpu_state(int n_exec);

struct FILE;

uint8_t  rr_inb(uint16_t port);
uint16_t rr_inw(uint16_t port);
uint32_t rr_inl(uint16_t port);
void rr_out(uint16_t port, target_ulong data, size_t data_size);

void rr_ins(uint16_t port, void *addr, size_t cnt, size_t data_size);
void rr_outs(uint16_t port, const void *addr, size_t cnt, size_t data_size);

void rr_interrupt(int intno, int error_code, target_ulong next_eip);

char const *rr_log_get_record_disk_name(void);
char const *rr_log_get_replay_disk_name(void);

void rr_log_register_callback(
		void (*callback)(char const *rr_tag, bool replay, void *opaque),
		void *opaque);
void rr_log_unregister_callback(
		void (*callback)(char const *rr_tag, bool replay, void *opaque),
		void *opaque);

bool rr_log_lockstep_mode(void);

extern bool rr_log_force_dump_on_next_tb_entry;
extern target_ulong rr_log_panic_eip;
extern off_t record_log_disk_begin;

#endif
