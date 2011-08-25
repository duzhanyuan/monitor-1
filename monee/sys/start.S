#### The loader needs to have some way to know the monitor's entry
#### point, that is, the address to which it should jump to start the
#### monitor.  We handle this by writing the linker script monitor.lds.S
#### so that this module appears at the very beginning of the monitor
#### image, and then using that as the entry point.

.section .start

.globl start
.func start
  # Call main.
start:  call main

  # main() should not return, but if it does, spin.
1:  jmp 1b
.endfunc

