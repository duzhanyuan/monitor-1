#include "peep/sti_fallthrough.h"

static void *sti_fallthrough_addrs[1];

void
add_sti_fallthrough_addr(void *ptr)
{
	sti_fallthrough_addrs[0] = ptr;
}

bool
remove_sti_fallthrough_addr(void *ptr)
{
	return sti_fallthrough_addrs[0] == ptr;
}
