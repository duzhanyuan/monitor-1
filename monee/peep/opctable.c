#include "peep/opctable.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <debug.h>
#include "sys/vcpu.h"

#define MAX_DISAS_ENTRIES 2560
#define MAX_SIZEFLAGS 4
#define MAX_OPCS 1024
#define MAX_

static opc_t opctable[MAX_DISAS_ENTRIES][MAX_SIZEFLAGS];

static char nametable[MAX_OPCS][8];
static size_t nametable_size = 0;

static int sizeflagtable[MAX_SIZEFLAGS];
static int sizeflagtable_size = 0;

static int find_sizeflag_num(int sizeflag);

void
opctable_init(void)
{
  int i;
  for (i = 0; i < MAX_DISAS_ENTRIES; i++) {
    int j;
    for (j = 0; j < MAX_SIZEFLAGS; j++) {
      opctable[i][j] = opc_inval;
    }
  }
  snprintf(nametable[nametable_size], sizeof nametable[nametable_size],"(bad)");
  nametable_size++;
}

void
opctable_insert(char const *name, int dp_num, int sizeflag)
{
  unsigned i;
  opc_t opc = (opc_t)nametable_size;
  int sizeflag_num;

  for (i = 0; i < nametable_size; i++) {
    if (!strcmp(nametable[i], name)) {
      opc = i;
    }
  }
  ASSERT(opc < MAX_OPCS);
  sizeflag_num = find_sizeflag_num(sizeflag);
  ASSERT(sizeflag_num < MAX_SIZEFLAGS);
  ASSERT(dp_num < MAX_DISAS_ENTRIES);

  opctable[dp_num][sizeflag_num] = opc;

  if (opc == (int)nametable_size) {
    snprintf(nametable[opc], sizeof nametable[opc], "%s", name);
    nametable_size++;
  }
  if (sizeflag_num == sizeflagtable_size) {
    //printf("Adding %d to sizeflagtable.\n", sizeflag);
    sizeflagtable[sizeflag_num] = sizeflag;
    sizeflagtable_size++;
  }
}

void
opctable_print(FILE *fp)
{
  int dp_num;
  for (dp_num = 0; dp_num < MAX_DISAS_ENTRIES; dp_num++) {
    int sizeflag;
    for (sizeflag = 0; sizeflag < MAX_SIZEFLAGS; sizeflag++) {
      opc_t opc;
      opc = opctable[dp_num][sizeflag];
      fprintf(fp, "(%#x,%d)->%#x->%s\n", dp_num, sizeflag, opc,
          nametable[opc]);
    }
  }
}

opc_t
opctable_find(int dp_num, int sizeflag)
{
  int sizeflagnum = find_sizeflag_num(sizeflag);
  ASSERT(sizeflagnum < sizeflagtable_size);
  if (opctable[dp_num][sizeflagnum] == opc_inval) {
    //printf("Invalid opcode found at eip=%#x.\n", vcpu.eip);
    return -1;
  }
  return opctable[dp_num][sizeflagnum];
}

char const *
opctable_name(opc_t opc)
{
  return nametable[opc];
}

static int
find_sizeflag_num(int sizeflag)
{
  int i;
  int sizeflag_num = sizeflagtable_size;
  for (i = 0; i < sizeflagtable_size; i++) {
    if (sizeflagtable[i] == sizeflag) {
      sizeflag_num = i;
      break;
    }
  }
  ASSERT(sizeflagtable_size <= MAX_SIZEFLAGS);
  ASSERT(sizeflag_num < MAX_SIZEFLAGS);
  return sizeflag_num;
}
