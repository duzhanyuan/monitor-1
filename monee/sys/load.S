#### The bootsector needs to have some way to know the loader's entry
#### point, that is, the address to which it should jump to start the
#### loader.  We handle this by writing the linker script loader.lds.S
#### so that this module appears at the very beginning of the loader
#### image, and then using that as the entry point.

.section .start

.globl start
.func start
  # Call main.
load:  call loader_main

  # loader_main() should not return, but if it does, spin.
1:  jmp 1b
.endfunc

