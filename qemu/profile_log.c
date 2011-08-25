#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "profile_log.h"
#include <stdlib.h>
#include "hash.h"
#define ASSERT assert

static struct hash ptab;
FILE *profile_log = NULL;

typedef struct entry_t {
  uint32_t prev_eip;
  uint32_t eip;
  uint64_t count;
  struct hash_elem ptab_elem;
} entry_t;

uint64_t total_count = 0;
uint64_t total_eips = 0;

void
profile_log_increment_count(uint32_t eip)
{
  struct hash_elem *e;
  static uint32_t prev_eip = 0;
  entry_t needle, *entry;
  needle.eip = eip;
  needle.prev_eip = prev_eip;
  e = hash_find(&ptab, &needle.ptab_elem);
  if (e) {
    entry = hash_entry(e, entry_t, ptab_elem);
    entry->count++;
  } else {
    entry_t *new_entry = malloc(sizeof(entry_t));
    new_entry->prev_eip = prev_eip;
    new_entry->eip = eip;
    new_entry->count = 1;
    hash_insert(&ptab, &new_entry->ptab_elem);
    total_eips++;
  }
  prev_eip = eip;
  total_count++;
}

int entry_compare(const void *_a, const void *_b)
{
  entry_t **a = (entry_t **)_a;
  entry_t **b = (entry_t **)_b;
  return (*b)->count - (*a)->count;
}

void
profile_log_dump(void)
{
  struct hash_iterator iter;
  int i = 0;
  entry_t *entries[total_eips];
 
  hash_first(&iter, &ptab);
  while (hash_next(&iter)) {
    entries[i++] = hash_entry(hash_cur(&iter), entry_t, ptab_elem);
  }
  ASSERT(i == total_eips);
  qsort(entries, total_eips, sizeof entries[0], entry_compare);
  fprintf(profile_log, "Dumping profile log:\n");
  for (i = 0; i < total_eips; i++) {
    fprintf(profile_log, "%#x->%#x %lld\n", entries[i]->prev_eip,
        entries[i]->eip, entries[i]->count);
  }
  fclose(profile_log);
}

unsigned
entry_hash_func(struct hash_elem const *e, void *aux)
{
  entry_t const *en = hash_entry(e, entry_t, ptab_elem);
  return en->eip*7 + en->prev_eip;
}

bool
entry_less_func(struct hash_elem const *a, struct hash_elem const *b, void *aux)
{
  entry_t const *ea, *eb;
  ea = hash_entry(a, entry_t, ptab_elem);
  eb = hash_entry(b, entry_t, ptab_elem);
  return ea->eip < eb->eip ||
    ((ea->eip == eb->eip) && ea->prev_eip < eb->prev_eip);
}

void
profile_log_init(char const *filename)
{
  profile_log = fopen(filename, "w");
  ASSERT(profile_log);
  hash_init(&ptab, &entry_hash_func, &entry_less_func, NULL);
}

static void
entry_free(struct hash_elem *e, void *aux)
{
  entry_t const *en;
  en = hash_entry(e, entry_t, ptab_elem);
  free(en);
}

void
profile_log_reset(void)
{
  hash_clear(&ptab, &entry_free);
  total_eips = 0;
  total_count = 0;
}
