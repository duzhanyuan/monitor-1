#!/usr/bin/expect -f

set timeout -1
stty
set host 10.20.3.27

spawn ssh sbansal@$host
expect "*password*"
send "sbansalsbansal\r"

expect "</>hpiLO->"
send "stop -f /system1\r"

expect "</>hpiLO->"
#send "vm floppy insert http://dhcp96.cse.iitd.ernet.in/boot_image/boot.dsk\r"
send "cd map1\r"

expect "</map1>hpiLO->"
send "cd oemhp_vm1\r"

expect "</map1/oemhp_vm1>hpiLO->"
send "cd floppydr1\r"

expect "</map1/oemhp_vm1/floppydr1>hpiLO->"
send "set oemhp_boot=disconnect\r"

expect "</map1/oemhp_vm1/floppydr1>hpiLO->"
send "cd /\r"

expect "</>hpiLO->"
send "start /system1\r"
expect "</>hpiLO->"

exit 0
