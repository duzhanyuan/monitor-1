#!/bin/bash -f
sdir=/home/deepakravi
flog=/var/tmp/qmon.$USER.log
odir=~/public_html
DISK="D$JOB_ID"
run_vm=$sdir/run_vm.exp
stop_vm=$sdir/stop_vm.exp
export TERM=vt100
export LANG=C
URL=http://10.20.3.50/~$LOGNAME/$DISK

function log() {
 echo "+++ $@"
 #echo "+++ $@" >> $flog
}

function term(){
 log "Killing $DISK" 
 /usr/bin/env -i LANG=C TERM=vt100 XUSER=guest XPASS=guestguest $stop_vm 
 log "Killed $DISK"
 exit 0
}

trap term TERM INT STOP

log "Hello $DISK: Args = $@ " 
mkdir -p $odir
cp "$1" "$odir/$DISK"
sync
/usr/bin/env -i LANG=C TERM=vt100 XUSER=guest XPASS=guestguest $run_vm $URL 1500
x=$?
log "$DISK exited with $x" 
exit 0

