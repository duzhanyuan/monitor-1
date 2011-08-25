/* Constants fixed by the PC BIOS. */
#define LOADER_BASE 0x7c00      /* Physical address of loader's base. */
#define LOADER_END  0x7e00      /* Physical address of end of loader. */

/* Physical address of kernel base. */
#define LOADER_KERN_BASE 0x100000       /* 1 MB. */

/* Kernel virtual address at which all physical memory is mapped.

   The loader maps the 4 MB at the bottom of physical memory to
   this virtual base address.  Later, paging_init() adds the rest
   of physical memory to the mapping.

   This must be aligned on a 4 MB boundary. */

#define LOADER_PHYS_BASE 0xc0000000     /* 3 GB. */

/* Important loader physical addresses. */
#define LOADER_SIG (LOADER_END - LOADER_SIG_LEN)   /* 0xaa55 BIOS signature. */
#define LOADER_ARGS (LOADER_SIG - LOADER_ARGS_LEN)     /* Command-line args. */
#define LOADER_ARG_CNT (LOADER_ARGS - LOADER_ARG_CNT_LEN) /* Number of args. */
#define LOADER_RAM_PGS (LOADER_ARG_CNT - LOADER_RAM_PGS_LEN) /* # RAM pages. */

/* Sizes of loader data structures. */
#define LOADER_SIG_LEN 2
#define LOADER_ARGS_LEN 64
#define LOADER_ARG_CNT_LEN 4
#define LOADER_RAM_PGS_LEN 4

/* GDT selectors defined by loader.
      More selectors are defined by userprog/gdt.h. */
#define SEL_NULL        0x00    /* Null selector. */
#define SEL_KCSEG       0x08    /* Kernel code selector. */
#define SEL_KDSEG       0x10    /* Kernel data selector. */

/* Flags in control register 0. */
#define CR0_PE 0x00000001      /* Protection Enable. */
#define CR0_EM 0x00000004      /* (Floating-point) Emulation. */
#define CR0_PG 0x80000000      /* Paging. */
#define CR0_WP 0x00010000      /* Write-Protect enable in kernel mode. */




.globl start
start:
	
# Code runs in real mode, which is a 16-bit segment.
	.code16
#MDEBUG_START(10)

# Disable interrupts, because we will not be prepared to handle them
# in protected mode until much later.
# String instructions go upward (e.g. for "rep stosl" below).

  cli
	cld

# Set up data segments.

	subw %ax, %ax
	movw %ax, %es
	movw %ax, %ds

# Set up stack segment.
# Stack grows downward starting from us.
# We don't ever use the stack, but we call into the BIOS,
# which might.

	movw %ax, %ss
	movw $0x7c00, %sp
	
#### Enable A20.  Address line 20 is tied to low when the machine
#### boots, which prevents addressing memory about 1 MB.  This code
#### fixes it.
	
# Poll status register while busy.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# Send command for writing output port.

	movb $0xd1, %al
	outb %al, $0x64

# Poll status register while busy.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# Enable A20 line.

	movb $0xdf, %al
	outb %al, $0x60

#### Get memory size, via interrupt 15h function 88h.  Returns CF
#### clear if successful, with AX = (kB of physical memory) - 1024.
#### This only works for memory sizes <= 65 MB, which should be fine
#### for our purposes.  We cap memory at 1200 KB (sorav) /*64 MB*/ because
####  that's all we prepare page tables for, below.
	
	movb $0x88, %ah
	int $0x15
	jc panic
	cli			# BIOS might have enabled interrupts
	addl $1024, %eax	# Total kB memory
	cmp $1200, %eax	  # Cap at 1200 KB (sorav) [64 MB]
	jbe 1f
  mov $1200, %eax   # sorav: mov $0x10000, %eax
1:	shrl $2, %eax		# Total 4 kB pages
	movl %eax, ram_pgs
	
#### Create temporary page directory and page table and set page
#### directory base register.

# Create page directory at 64 kB and fill with zeroes.
	mov $0x1000, %ax
	mov %ax, %es
	subl %eax, %eax
	subl %edi, %edi
	movl $0x400, %ecx
	rep stosl

# Add PDEs to point to PTEs for the first 64 MB of RAM.
# Also add identical PDEs starting at LOADER_PHYS_BASE.
# See [IA32-v3a] section 3.7.6 "Page-Directory and Page-Table Entries"
# for a description of the bits in %eax.
	

	movl $0x11007, %eax
	movl $0x11, %ecx
	subl %edi, %edi
1:	movl %eax, %es:(%di)
	movl %eax, %es:LOADER_PHYS_BASE >> 20(%di)
	addw $4, %di
	addl $0x1000, %eax
	loop 1b

# Set up one-to-map linear to physical map for the first 64 MB of RAM.
# See [IA32-v3a] section 3.7.6 "Page-Directory and Page-Table Entries"
# for a description of the bits in %eax.

	movw $0x1100, %ax
	movw %ax, %es
	movl $0x7, %eax
	movl $0x4000, %ecx
	subl %edi, %edi
1:	movl %eax, %es:(%di)
	addw $4, %di
	addl $0x1000, %eax
	loop 1b

# Set page directory base register.

	movl $0x10000, %eax
	movl %eax, %cr3
	
#### Switch to protected mode.

# Note that interrupts are still off.

# Point the GDTR to our GDT.  Protected mode requires a GDT.
# We need a data32 prefix to ensure that all 32 bits of the GDT
# descriptor are loaded (default is to load only 24 bits).

	data32 lgdt gdtdesc

# Then we turn on the following bits in CR0:
#    PE (Protect Enable): this turns on protected mode.
#    PG (Paging): turns on paging.
#    WP (Write Protect): if unset, ring 0 code ignores
#       write-protect bits in page tables (!).
#    EM (Emulation): forces floating-point instructions to trap.
#       We don't support floating point. 
	
	movl %cr0, %eax
	orl $CR0_PE | CR0_PG | CR0_WP | CR0_EM, %eax
	movl %eax, %cr0
	
# We're now in protected mode in a 16-bit segment.  The CPU still has
# the real-mode code segment cached in %cs's segment descriptor.  We
# need to reload %cs, and the easiest way is to use a far jump.
# Because we're not in a 32-bit segment the data32 prefix is needed to
# jump to a 32-bit offset.

	data32 ljmp $SEL_KCSEG, $1f + LOADER_PHYS_BASE
	
# We're now in protected mode in a 32-bit segment.

  .code32
  1:ret

#### GDT

gdt:
	.quad 0x0000000000000000	# null seg
	.quad 0x00cf9a000000ffff	# code seg
	.quad 0x00cf92000000ffff        # data seg
	
gdtdesc:
	.word	0x17			# sizeof (gdt) - 1
	.long	gdt + LOADER_PHYS_BASE	# address gdt

#### Fatal error.
#### Print panic_message (with help from the BIOS) and spin.

panic:  .code16			# We only panic in real mode.
	movw $panic_message, %si
	movb $0xe, %ah
	subb %bh, %bh
1:	lodsb
	test %al, %al
2:	jz 2b			# Spin.
	int $0x10
	jmp 1b

panic_message:
	.ascii "Panic!"
	.byte 0

#### Physical memory size in 4 kB pages.
#### This is initialized by the loader and read by the kernel.
	.org LOADER_RAM_PGS - LOADER_BASE
ram_pgs:
	.long 0

#### Command-line arguments and their count.
#### This is written by the `pintos' utility and read by the kernel.
#### The loader itself does not do anything with the command line.
	.org LOADER_ARG_CNT - LOADER_BASE
arg_cnt:
	.long 0
	.org LOADER_ARGS - LOADER_BASE
args:
	.fill 0x40, 1, 0

#### Boot-sector signature.
#### The BIOS checks that this is set properly.
	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55
