#include "lib/shutdown.h"
#include <stdio.h>
#include <sys/io.h>
#include <string.h>
#include "app/micro_replay.h"
#include "devices/serial.h"
#include "mem/palloc.h"
#include "mem/swap.h"
#include "peep/callouts.h"
#include "peep/tb.h"

static void print_stats(void);

/* Powers down the machine we're running on,
   as long as we're running on Bochs or QEMU. */
void
shutdown_power_off (void)
{
	const char s[] = "Shutdown";
	const char *p;

	/* This is a special power-off sequence supported by Bochs and
	 *      QEMU, but not by physical hardware. */
	for (p = s; *p != '\0'; p++)
		outb (0x8900, *p);

	/* This will power off a VMware VM if "gui.exitOnCLIHLT = TRUE"
	   is set in its configuration file.  (The "pintos" script does
	   that automatically.)  */
	asm volatile ("cli; hlt" : : : "memory");

	/* None of those worked. */
	printf ("still running...\n");
	for (;;);
}

static void shutdown_8900_write(void *opaque, uint32_t port, uint32_t data);

void
shutdown_init (void)
{
#ifdef __MONITOR__
	register_ioport_write(0x8900, 1, 1, shutdown_8900_write, NULL, true);
#endif
}

#ifdef __MONITOR__
static void
shutdown_8900_write(void *opaque, uint32_t port, uint32_t data)
{
#define SHUTDOWN_STRING "Shutdown"
#define STATS_STRING 		"Stats"
	static char shutdown_seen[strlen(SHUTDOWN_STRING)];
	static size_t num_shutdown_seen = 0;
	static char stats_seen[strlen(STATS_STRING)];
	static size_t num_stats_seen = 0;
	ASSERT(opaque == NULL);

	//printf("%s(): %x %x\n", __func__, port, data);
	if ((char)data == SHUTDOWN_STRING[num_shutdown_seen]) {
		num_shutdown_seen++;
	} else {
		num_shutdown_seen = 0;
	}

	if ((char)data == STATS_STRING[num_stats_seen]) {
		num_stats_seen++;
	} else {
		num_stats_seen = 0;
	}

	if (num_shutdown_seen == strlen(SHUTDOWN_STRING)) {
		shutdown_final_rites();
		num_shutdown_seen = 0;
	}
	if (num_stats_seen == strlen(STATS_STRING)) {
		print_stats();
		num_stats_seen = 0;
	}

	outb(port, data);
}

static void
print_stats(void)
{
	thread_print_stats();
	tb_print_stats();
	swap_print_stats();
	exception_print_stats();
	//micro_replay_print_stats();
	callout_print_stats();
}

void
shutdown_final_rites(void)
{
	print_stats();
	record_log_shutdown();
	intr_disable();
	serial_flush();
	printf("MONEE: ALL DONE.\n");
}
#endif
