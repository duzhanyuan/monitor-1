#include "mem/pt_mode.h"
#include <stdlib.h>
#include <stdio.h>
#include "mem/vaddr.h"
#include "sys/mode.h"
#include "sys/vcpu.h"

pt_mode_t
read_cr3(void)
{
  enum mode_t mode;
  target_phys_addr_t cr3;
  mode = switch_to_kernel();
  asm volatile ("movl %%cr3, %0" : "=r" (cr3));
  switch_mode(mode);
  return cr3;
}

pt_mode_t
switch_to_phys(void)
{
  enum mode_t mode;
  target_phys_addr_t cr3;

  mode = switch_to_kernel();
  asm volatile ("movl %%cr3, %0" : "=r" (cr3));
  if (cr3 != vtop_mon(phys_map)) {
    asm volatile ("movl %0, %%cr3" : : "r" (vtop_mon(phys_map)));
  }
  switch_mode(mode);
  return cr3;
}

pt_mode_t
switch_to_shadow(int user)
{
  enum mode_t mode;
  target_phys_addr_t cr3;

	ASSERT(user == 0 || user == 1);
  mode = switch_to_kernel();
  asm volatile ("movl %%cr3, %0" : "=r" (cr3));
  //if (cr3 != vtop_mon(vcpu.shadow_page_dir[user])) {
    asm volatile ("movl %0, %%cr3" : :
				"r" (vtop_mon(vcpu.shadow_page_dir[user])));
  //}
  switch_mode(mode);
  return cr3;
}

void
switch_pt(pt_mode_t pt_mode)
{
  if (pt_mode == vtop_mon(phys_map)) {
    switch_to_phys();
  } else if (vcpu.shadow_page_dir[0] &&
			pt_mode == vtop_mon(vcpu.shadow_page_dir[0])) {
    switch_to_shadow(0);
  } else if (vcpu.shadow_page_dir[1] &&
			pt_mode == vtop_mon(vcpu.shadow_page_dir[1])) {
    switch_to_shadow(1);
  } else {
		printf("pt_mode=0x%x, shadow_page_dirs=%p,%p\n", pt_mode,
				vcpu.shadow_page_dir[0], vcpu.shadow_page_dir[1]);
    ASSERT(0);
  }
}

void
pt_reload(void)
{
  enum mode_t mode;
  target_phys_addr_t cr3;

  mode = switch_to_kernel();
  asm volatile ("movl %%cr3, %0" : "=r" (cr3));
	asm volatile ("movl %0, %%cr3" : : "r" (cr3));
  switch_mode(mode);
}
