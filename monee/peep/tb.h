#ifndef PEEP_TB_H
#define PEEP_TB_H
#include <types.h>
#include <stdlib.h>
#include <hash.h>
#include <rbtree.h>

#ifndef MAX_TU_SIZE
#define MAX_TU_SIZE 12         /* Max number of insns in a translation unit. */
//#define MAX_TU_SIZE 1         /* Max number of insns in a translation unit. */
#endif

typedef struct rollbacks_t {
  int nb_rollbacks;
  uint8_t *buf;
  size_t buf_size;
  uint16_t *code_offset;
  uint16_t *rb_offset;
} rollbacks_t;

typedef struct tb_t {
	target_ulong eip;						/* The register eip when this tb was executed. */
  target_ulong eip_virt;			/* The (cs_base+eip) to get the virtual addr. */
  target_phys_addr_t eip_phys;	/* The physical address of first byte. */
  target_phys_addr_t eip_phys_end_page;	/* The physical address of the page on
																					 which this tb ends (needed because a
																					 tb could span 2 pages). */
  size_t tb_len, num_insns;
  uint8_t *tc_ptr;
  uint8_t  *eip_boundaries;
  uint16_t *tc_boundaries;
#ifndef NDEBUG
  char **peep_string;
#endif

  struct rollbacks_t *rollbacks;

  /* For cache replacement. */
  bool accessed_bit;
  struct list_elem clock_elem;

  /* For direct jump chaining. */
  struct tb_t *jmp_first;
  struct tb_t *jmp_next[2];
  uint16_t jmp_offset[2];
  uint16_t edge_offset[2];

  unsigned alignment:2;

  /* For pc_hash. */
  struct hash_elem pc_elem;
  /* For tc_tree. */
  struct rbtree_elem tc_elem;
	/* For jumptable2. */
	struct hash_elem jumptable2_elem;
} tb_t;

void tb_init(void);
tb_t *tb_malloc(target_ulong eip, target_ulong eip_virt,
		target_phys_addr_t eip_phys, target_phys_addr_t eip_phys_end_page,
    size_t num_insns, size_t size, rollbacks_t const *rollbacks);
void tb_add(tb_t *tb);
tb_t *tb_find_pc(target_phys_addr_t eip_phys, target_ulong eip_phys_end_page,
		target_ulong eip_virt, target_ulong eip);
tb_t *tb_find(const void *tc_ptr);
void tb_add_jump(tb_t *tb, unsigned n, tb_t *tb_next);
void tb_unchain_all(void);
target_ulong tb_tc_ptr_to_eip_virt(const void *tc_ptr);
bool tb_is_tc_boundary(const void *tc_ptr);
uint8_t const *tb_get_tc_next(tb_t const *tb, uint8_t const *tc_ptr);
void tb_flush(void);

void tb_print_in_asm(tb_t const *tb, unsigned size);
void tb_print_out_asm(tb_t const *tb);
void tb_print_rb_asm(tb_t const *tb);
void print_asm(uint8_t *ptr, size_t len, unsigned size);

void tb_print_stats(void);


void *tb_pool_malloc(size_t size);

//extern tb_t *debug_tb;
//extern void *debug_esp;
#endif
