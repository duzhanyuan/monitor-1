#ifndef PEEP_OPCTABLE_H
#define PEEP_OPCTABLE_H
#include <stdio.h>

typedef int opc_t;
#define opc_inval 0

#define FGRPS_DPNUM(dp, rm)    
#define FLOAT_MEM_DPNUM(fp_index)

void opctable_init(void);
void opctable_insert(char const *name, int dp_num, int sizeflag);
opc_t opctable_find(int dp_num, int sizeflag);
char const *opctable_name(opc_t opc);
int opctable_size(void);
void opctable_print(FILE *fp);

#endif
