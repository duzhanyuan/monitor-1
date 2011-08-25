/* This program attempts to execute code at address 0, which is not mapped.
   This should terminate the process with a -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  msg ("Congratulations - you have successfully called 0x1: %d", 
        ((int (*)(void))1)());
  fail ("should have exited with -1");
}
