#!/usr/bin/expect -f

set timeout -1
stty
set disk_image [lindex $argv 0]
set host 10.20.3.27
set USER $env(USER)
set PASSWORD $env(PASSWORD)
set WEBSERVER 10.20.254.81

spawn scp $disk_image $USER@$WEBSERVER:public_html/boot.dsk
expect "*password*"
send "$PASSWORD\r"
sleep 1
expect "$USER@*"

spawn ssh $USER@$host
expect "*password*"
send "$PASSWORD\r"

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
send "set oemhp_image=http://$WEBSERVER/~$USER/boot.dsk\r"

expect "</map1/oemhp_vm1/floppydr1>hpiLO->"
send "set oemhp_boot=connect\r"

expect "</map1/oemhp_vm1/floppydr1>hpiLO->"
send "cd /\r"

expect "</>hpiLO->"
send "start /system1\r"

expect "</>hpiLO->"
send "vsp\r"

expect "*DONE*"

set timeout 5
expect "*ALL DONE*"
set timeout -1

spawn ssh $USER@$host
expect "*password*"
send "$PASSWORD\r"
expect "</>hpiLO->"
send "stop -f /system1\r"

exit 0
