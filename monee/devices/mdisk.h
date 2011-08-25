#ifndef DEVICES_MDISK_H
#define DEVICES_MDISK_H
#include <stdbool.h>
#include <stdint.h>
#include <types.h>

/* A paravirtual disk for QEMU (debug version). */

struct mdisk;

void mdisk_init(void);
bool mdisk_read(struct mdisk *mdisk, target_phys_addr_t paddr,
		uint32_t block, uint16_t count);
bool mdisk_write(struct mdisk *mdisk, target_phys_addr_t paddr,
		uint32_t block, uint16_t count);
struct mdisk *identify_mdisk_by_name(char const *name);
char const *mdisk_name(struct mdisk *mdisk);
void mdisk_free(struct mdisk *mdisk);

#endif
