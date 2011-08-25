#ifndef MEMACCESS_H
#define MEMACCESS_H

typedef struct memaccess_t {
  int valid:1;
  int seg : 3;
  int base : 5;
  int index : 5;
  int scale : 3;
  uint64_t disp;
} memaccess_t;

#endif
