#include "peep/jumptable2.h"
#include <stdio.h>
#include <hash.h>
#include <stdlib.h>
#include "mem/malloc.h"
#include "peep/tb.h"

static struct hash jumptable2;

static unsigned jumptable2_hash(struct hash_elem const *e, void *aux);
static bool jumptable2_equal(struct hash_elem const *a,
    struct hash_elem const *b, void *aux);

void
jumptable2_init(void)
{
  hash_init(&jumptable2, jumptable2_hash, jumptable2_equal, NULL);
}

void
jumptable2_clear(void)
{
  hash_clear(&jumptable2, NULL);
}

void
jumptable2_add(struct tb_t *tb)
{
	struct hash_elem *ret;
  ret = hash_insert(&jumptable2, &tb->jumptable2_elem);
  ASSERT(!ret);
}

void *
jumptable2_find(target_ulong eip_virt, target_ulong eip)
{
  struct hash_elem *e;
  struct tb_t needle;
  needle.eip_virt = eip_virt;
  needle.eip = eip;
  if (!(e = hash_find(&jumptable2, &needle.jumptable2_elem))) {
    return NULL;
  }
  //log_printf("%s(%#lx) returned success.\n", __func__, eip);
  return hash_entry(e, struct tb_t, jumptable2_elem);
}

void
jumptable2_remove(struct tb_t *tb)
{
  hash_delete(&jumptable2, &tb->jumptable2_elem);
}

static unsigned
jumptable2_hash(struct hash_elem const *e, void *aux)
{
  struct tb_t const *entry;
  entry = hash_entry(e, struct tb_t, jumptable2_elem);
  return entry->eip_virt;
}

static bool jumptable2_equal(struct hash_elem const *a,
    struct hash_elem const *b, void *aux)
{
  struct tb_t const *ea, *eb;
  ea = hash_entry(a, struct tb_t, jumptable2_elem);
  eb = hash_entry(b, struct tb_t, jumptable2_elem);
  return ea->eip_virt == eb->eip_virt && ea->eip == eb->eip;
}
