#include "mdebug.h"
#include "loader.h"
#define SEL_BASE 0

/* Flags in control register 0. */
#define CR0_PE  0x00000001      /* Protection Enable. */
#define CR0_EM  0x00000004      /* (Floating-point) Emulation. */
#define CR0_PG  0x80000000      /* Paging. */
#define CR0_WP  0x00010000      /* Write-Protect enable in kernel mode. */
#define CR4_PSE 0x00000010      /* Page Size Enable */

.global start
start:
  .code16
#MDEBUG_START(10)
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
#### for our purposes.  We cap memory at 64 MB because that's all we
#### prepare page tables for, below.

  movb $0x88, %ah
  int $0x15
  jc panic
  cli     # BIOS might have enabled interrupts
  addl $1024, %eax  # Total kB memory
#cmp $0x10000, %eax  # Cap at 64 MB
#jbe 1f
#mov $0x10000, %eax 
  1:  shrl $2, %eax   # Total 4 kB pages 
  movl %eax, ram_pgs

# Point %es to the PDE table
  mov $(LOADER_PAGEDIR_BASE >> 4), %ax
  mov %ax, %es

# Create page directory at LOADER_PAGEDIR_BASE (es) and fill it with zeros
  subl %eax, %eax
  subl %edi, %edi
  movl $0x400, %ecx
  rep stosl

# Turn on the the PSE flag in cr4 to enable 4MB pages.
  movl %cr4, %eax
  orl  $CR4_PSE, %eax
  movl %eax, %cr4

  mov $(LOADER_PAGEDIR_BASE >> 4), %ax
  mov %ax, %es

#### Map a 4MB page starting at 0000:0000 using an identity map.
# The global (bit 8) and pagesize (7) flags also need to be set.
  movl $0 | (1<<8) | (1<<7) | 0x7, %eax
  movl $0, %edi
  movl %eax, %es:(%di)

#  movl $LOADER_MONITOR_BASE | (1<<8) | (1<<7) | 0x7, %eax
#  movl $0xffc, %edi
#  movl %eax, %es:(%di)
#  movl $(LOADER_MONITOR_BASE >> 20), %edi
#  movl %eax, %es:(%di)

#### Set page directory base register.
  movl $LOADER_PAGEDIR_BASE, %eax
  movl %eax, %cr3

  callw check_extensions_present
  #callw read_drive_params
#### Load the "loader" at address BOOTSECTOR_LOADER_BASE
  movw $(LOADER_SIZE/BYTES_PER_SECTOR), %cx
  movl $MONITOR_OFFSET, %esi
  movl $BOOTSECTOR_LOADER_BASE, %edx
  callw read_sectors

#### Switch to protected mode

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

  movl %cr0, %eax
  orl $CR0_PE | CR0_PG | CR0_WP, %eax
  movl %eax, %cr0

# We're now in protected mode in a 16-bit segment.  The CPU still has
# the real-mode code segment cached in %cs's segment descriptor.  We
# need to reload %cs, and the easiest way is to use a far jump.
# Because we're not in a 32-bit segment the data32 prefix is needed to
# jump to a 32-bit offset.

  data32 ljmp $SEL_KCSEG, $1f

  .code32

# Reload all other  segment registers and the stack pointer to point to null
# segment.
1:  movl $SEL_KDSEG, %eax
  movw %ax, %ds
  movw %ax, %es
  movw %ax, %fs
  movw %ax, %gs
  movw %ax, %ss

# Write out the number of loader and monitor pages
#movl $MONITOR_OFFSET, monitor_ofs
#movl $LOADER_LOAD_PAGES, loader_pgs
#movl $MONITOR_LOAD_PAGES, monitor_pgs

# Load the stack pointer with top-of-mapped-physical-memory (which is 4MB).
  movl $0x3fffff, %esp

# Call in to the loader
  movl $BOOTSECTOR_LOADER_BASE, %eax
  call *%eax

.code16
#read_drive_params:
#movb  $8, %ah                # BIOS read drive parameters function
#movb  $0x80, %dl             # drive to read (0x80 = first hard drive)
#int   $0x13                  # make the BIOS call
#jc    panic
#cli
#incb  %dh
#movb  %dh, NUM_HEADS
#andb  $0x3f, %cl
#movb  %cl, SECTORS_PER_TRACK
#retw

check_extensions_present:
movb  $0x41, %ah             # BIOS check extensions present function
movw  $0x55aa,%bx            # required by the bios function
movb  $0x80, %dl             # drive to read (0x80 = first hard drive)
int   $0x13
jb    panic
cli
cmpw  $0xaa55, %bx
jne   panic
testb $1, %cl
je    panic
ret

# read_sectors:
# arguments: (edx:32-bit buffer pointer, si:start_sector, cx:number_of_sectors)
# output: void, halt on error.
read_sectors:
# 8 bytes for specifying the absolute number of the start of the sectors
# convert the 20-bit buffer pointer edx to seg:offset using es:bx
pushw %es
movl  %esi, %edi
loop:
movl  %edx, %eax
shrl  $4, %eax
movw  %ax, %es        #es contains the top 16 bits
movw  %dx, %bx
andw  $0xf, %bx       #bx contains the lower 4 bits

#save edx
pushl %edx

#set up the stack and call int 13
pushw $0
pushw $0
pushl %edi
#4 bytes for specifying the buffer pointer using segment:offset
pushw %es
pushw %bx
pushw $1
pushw $0x10
movb  $0x42,%ah
movw  %sp,%si
movb  $0x80, %dl             # drive to read (0x80 = first hard drive)
int   $0x13
jb    panic
cli
addw  $16, %sp
popl  %edx
addl  $512, %edx
incw  %di
decw  %cx
jne   loop
popw  %es
ret

#read_sectors:
#pushw %es
#.mainloop:
#movw  $5, %di                # try to read a sector at most 5 times.
#.sectorloop:
#pushw %cx
#pushl %edx
#movw  %si, %ax               # read LBA into ax
#xorw  %bx, %bx
#xorw  %dx, %dx
#movb  SECTORS_PER_TRACK, %bl # read SPT into bx
#testw %bx,%bx
#je    1f
#divw  %bx                    # divide (ax/bx -> ax,dx)
#jmp 2f
#1: xorw %ax,%ax
#2: incw  %dx                 # add one to the remainder (sector value)
#pushw %dx                    # save the sector value on stack
#xorw  %bx, %bx
#xorw  %dx, %dx
#movb  NUM_HEADS, %bl         # move num_heads to bx
#divw  %bx                    # divide (ax/bx -> ax,dx)
#movw  %dx, %bp
#
## The stack now contains sector. ax contains the cylinder. bp contains head
#
## The code to read a sector from the drive
#movb  %al, %ch
#popw  %ax
#movb  %al, %cl               # sector to read
#popl  %edx                   # restore saved value of edx
#movw  %dx, %bx               # move last 4 bits of buffer pointer to bx
#andw  $0xf, %bx
#movl  %edx, %eax
#shrl  $4, %eax
#movw  %ax, %es
#pushl %edx                   # save edx back
#xorw  %dx, %dx
#movw  %bp, %ax               # move head to ax
#movb  %al, %dh               # head to read
#movb  $2, %ah                # BIOS read sector function
#movb  $1, %al                # read one sector
#movb  $0x80, %dl             # drive to read (0x80 = first hard drive)
#int   $0x13                  # make the BIOS call
#cli
#popl  %edx                   # restore saved value
#popw  %cx
#jnc   .success
#dec   %di                    # decrease the counter
#jnz   .sectorloop            # try the command again (in case of h/w problems)
#jmp   panic                  # call the error command in case all else fails
#.success:
#addl  $BYTES_PER_SECTOR,%edx # increment buffer pointer
#incw  %si                    # increment current sector number
#loopw .mainloop              # decrement cx and read next sector
#.endread:                    # end of the read procedure
#popw  %es
#retw                         # return to main program

#### GDT

gdt:
    .quad 0x0000000000000000        # null seg
    .quad 0x00cf9a000000ffff        # code seg
    .quad 0x00cf92000000ffff        # data seg
#    .quad 0x004092000000ffff        # real mode data seg
gdtdesc:
#    .word 0x17
    .word 0x17
    .long gdt


#### Fatal error.
#### Print panic_message (with help from the BIOS) and spin.

panic:  .code16     # We only panic in real mode.
  movw $panic_message, %si
  movb $0xe, %ah
  subb %bh, %bh
1:  lodsb
  test %al, %al
2:  jz 2b     # Spin.
  int $0x10
  jmp 1b


panic_message:
  .ascii "Panic!"
  .byte 0

SECTORS_PER_TRACK:
  .byte 0

NUM_HEADS:
  .byte 0

#### Size of the monitor in 4 kB pages.
#### This is initialized by the bootsector and read by the kernel.
  .org BOOTSECTOR_MONITOR_PGS - BOOTSECTOR_BASE
monitor_pgs:
  .long MONITOR_LOAD_PAGES

#### Size of the loader in 4 kB pages.
#### This is initialized by the bootsector and read by the kernel.
  .org BOOTSECTOR_LOADER_PGS - BOOTSECTOR_BASE
loader_pgs:
  .long LOADER_LOAD_PAGES

#### Offset where the monitor pages live on the disk.
monitor_ofs:
  .long MONITOR_OFFSET

#### Physical memory size in 4 kB pages.
  ### This is initialized by the loader and read by the kernel.
  .org BOOTSECTOR_RAM_PGS - BOOTSECTOR_BASE
ram_pgs:
  .long 0

.org BOOTSECTOR_DISK_SIG - BOOTSECTOR_BASE
#.long 0x12345678
.long 0

.org BOOTSECTOR_MANDATORY_ZERO - BOOTSECTOR_BASE
.word 0

.org BOOTSECTOR_PARTITION_TABLE - BOOTSECTOR_BASE
#first partition entry
.byte 0x80        # set boot flag
.byte 0,0,0,0,0,0,0
#.byte 0,0,0,0xe,0,0,0
#.long 1           # starting lba (1)
.long 0           # starting lba (1)
.byte 0,0,0,0
#second partition entry
.byte 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
#third partition entry
.byte 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
#fourth partition entry
.byte 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

#### Boot-sector signature.
#### The BIOS checks that this is set properly.
.org BOOTSECTOR_SIG - BOOTSECTOR_BASE
.word 0xaa55
