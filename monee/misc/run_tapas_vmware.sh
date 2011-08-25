#!/usr/bin/expect -f

set timeout -1
stty
#set host 10.20.254.105
set host   [lindex $argv 0]
set vmname [lindex $argv 1]
set vms_path "/cygdrive/e/vms"
#set vms_path "Desktop/vms"

exec rm -f serial.$vmname
exec touch serial.$vmname
spawn scp $vmname.vmx $vmname-s001.vmdk $vmname.dsk $vmname.vmdk serial.$vmname Administrator@$host:$vms_path
expect "*password*"
send "sbansal\r"
expect "sbansal@*"

spawn ssh Administrator@$host
expect "*password*"
send "sbansal\r"

expect "$ "
send "cd $vms_path\r"

expect "$ "
send "vmrun.exe stop $vmname.vmx\r"
expect "$ "
send "vmrun.exe start $vmname.vmx\r"

expect "$ "
send "tail -f serial.$vmname\r"

expect "*DONE*"
#Ctrl-C
send \003

expect "$ "
send "vmrun.exe stop $vmname.vmx\r"

expect "$ "
send "exit\r"

exit 0
