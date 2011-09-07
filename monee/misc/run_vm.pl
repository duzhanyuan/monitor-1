#!/usr/bin/perl -w

use strict;
use warnings;
use POSIX;
use Fcntl;
use File::Temp 'tempfile';
use File::Copy;
use File::Basename;
use Sys::Hostname;
use Getopt::Long qw(:config bundling);
use Fcntl qw(SEEK_SET SEEK_CUR);

if ($#ARGV != 4) {
	print "Usage: run_vm.pl <src-dir> <simulator> <disk-file> ";
	print "<output-file> <timeout>\n";
	exit(1);
}

my $SRCDIR = $ARGV[0];
my $sim = $ARGV[1];
my $disk = $ARGV[2];
my $output = $ARGV[3];
my $timeout = $ARGV[4];
my $kill_on_failure;
my $start_time = time ();
my $NAME;
if ($SRCDIR =~ /([^\/]*)$/) {
	$NAME = $1;
}
#my $mem = 64;				#megs
my $mem = 12;				#megs
#my $mem = 12;				#megs  (bad-jump seems to be failing for mem=12)

sub run_tapas
{
  my $bin = shift;
  my $os_dsk = shift;
  my $serial_output = shift;
  my $extra_opts = shift;
  system("$SRCDIR/misc/run_tapas.sh $os_dsk | tee $serial_output");
}

sub run_tapas_q
{
  my $bin = shift;
  my $os_dsk = shift;
  my $serial_output = shift;
  my $extra_opts = shift;
  system("$SRCDIR/misc/run_tapas_q.exp $os_dsk $extra_opts | tee $serial_output");
}


sub run_qemu
{
	my $bin = shift;
	my $os_dsk = shift;
	my $serial_output = shift;
	my $extra_opts = shift;
	my $cmd;

	$cmd = "$bin -hda $os_dsk -m $mem -net none ";
	$cmd .= $extra_opts;
	$cmd .= "-nographic ";
	$cmd .= "-serial /dev/null -serial file:$serial_output ";
	$cmd .= "-monitor null 2>/dev/null ";
  #$cmd .= "-usb -usbdevice disk:/bin/ls";

	return $cmd;
}

# open_disk($disk)
#
# Opens $disk, if it is not already open, and returns its file handle
# and file name.
sub open_disk {
	my ($file_name) = shift;
	sysopen (my $handle, $file_name, O_RDWR)
		or die "$file_name: open: $!\n";
	return $handle;
}

# extend_disk($disk, $size)
#
# Extends $disk, if necessary, so that it is at least $size bytes
# long.
sub extend_disk {
	my ($file_name, $size) = @_;
	my $handle = open_disk($file_name);
	if (-s ($handle) < $size) {
		sysseek ($handle, $size - 1, 0) == $size - 1
			or die "$file_name: seek: $!\n";
		syswrite ($handle, "\0") == 1
			or die "$file_name: write: $!\n";
	}
}

# disk_geometry($file)
#
# Examines $file and returns a valid IDE disk geometry for it, as a
# hash.
sub disk_geometry {
    my ($file) = @_;
    my ($size) = -s $file;
    die "$file: stat: $!\n" if !defined $size;
    die "$file: size $size not a multiple of 512 bytes\n" if $size % 512;
    my ($cyl_size) = 512 * 16 * 63;
    my ($cylinders) = ceil ($size / $cyl_size);
		extend_disk ($file, $cylinders * $cyl_size) if $size % $cyl_size;

    return (CAPACITY => $size / 512,
	    C => $cylinders,
	    H => 16,
	    S => 63);
}

sub print_bochs_disk_line {
	my ($device, $disk) = @_;
	if (defined $disk) {
		my (%geom) = disk_geometry ($disk);
		print BOCHSRC "$device: type=disk, path=$disk, mode=flat, ";
		print BOCHSRC "cylinders=$geom{C}, heads=$geom{H}, spt=$geom{S}, ";
		print BOCHSRC "translation=none\n";
	}
}

# Runs Bochs.
sub run_bochs
{
	my $bin = shift;
	my $os_dsk = shift;
	my $serial_output = shift;
	my $jitter = shift;

	my $serial = 1;
	my $realtime = 0;
	my $vga = "none";
	# Select Bochs binary based on the chosen debugger.
	#my ($bin) = $debug eq 'monitor' ? 'bochs-dbg' : 'bochs';

	my ($squish_pty);
	my $serial_output_base = basename($serial_output);
	my $bochsrc_file = "bochsrc.txt.$serial_output_base";
	my $bochsout_file = "bochsout.txt.$serial_output_base";
	#my $bochsrc_file = "bochsrc.txt";
	#my $bochsout_file = "bochsout.txt";
	#if ($serial) {
	#	$squish_pty = find_in_path ("squish-pty");
	#	print "warning: can't find squish-pty, so terminal input will fail\n"
	#	if !defined $squish_pty;
	#}

	# Write bochsrc.txt configuration file.
	open (BOCHSRC, ">", $bochsrc_file) or die "$bochsrc_file: create: $!\n";
	print BOCHSRC <<EOF;
romimage: file=\$BXSHARE/BIOS-bochs-latest
vgaromimage: file=\$BXSHARE/VGABIOS-lgpl-latest
boot: disk
cpu: ips=100000000
megs: $mem
log: $bochsout_file
panic: action=fatal
user_shortcut: keys=ctrlaltdel
EOF
	#print BOCHSRC "gdbstub: enabled=1\n" if $debug eq 'gdb';
	if ($realtime) {
		print BOCHSRC "clock: sync=realtime\n";
	} else {
		print BOCHSRC "clock: sync=none, time0=0\n";
	}
	#print BOCHSRC "ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15\n"
	#if @disks > 2;
	print_bochs_disk_line ("ata0-master", $os_dsk);
	if ($vga ne 'terminal') {
		if ($serial) {
			print BOCHSRC "com1: enabled=1, mode=term, dev=/dev/null\n";
			if (defined $squish_pty) {
				print BOCHSRC "com2: enabled=1, mode=term, dev=/dev/stdout\n";
			} else {
				print BOCHSRC "com2: enabled=1, mode=file, dev=$serial_output\n";
			}
		}
		print BOCHSRC "display_library: nogui\n" if $vga eq 'none';
	} else {
		print BOCHSRC "display_library: term\n";
	}
	close (BOCHSRC);

	# Compose Bochs command line.
	my (@cmd) = ($bin, '-f', $bochsrc_file, '-q');
	unshift (@cmd, $squish_pty) if defined $squish_pty;
	push (@cmd, '-j', $jitter) if defined $jitter;

	# Run Bochs.
	print join (' ', @cmd), "\n";
	my ($exit) = xsystem (@cmd);
	if (WIFEXITED ($exit)) {
		# Bochs exited normally.
		# Ignore the exit code; Bochs normally exits with status 1,
		# which is weird.
	} elsif (WIFSIGNALED ($exit)) {
		die "Bochs died with signal ", WTERMSIG ($exit), "\n";
	} else {
		die "Bochs died: code $exit\n";
	}
}

my $tmpfile = "os.dsk";

if ($sim eq 'qemu') {
	my $cmd;
	copy($disk, $tmpfile);

	$cmd = run_qemu("qemu", $tmpfile, $output, "");
	run_command($cmd);
	unlink($tmpfile);
	exit 0;
} elsif ($sim eq 'qemu-rr' || $sim eq 'qemu-record') {
	my ($cmd, $mdisks);
	copy($disk, $tmpfile);

	mkfifo("record.wr.fifo", 0777) unless -e "record.wr.fifo";

	$cmd  = "cat record.wr.fifo > replay.dsk & ";
	$mdisks = "-mdisk record.wr.fifo ";
	$cmd .= run_qemu("$SRCDIR/../$NAME-build/qemu", $tmpfile,
		"$output.record", $mdisks);
	run_command($cmd);
	copy("$output.record", $output);

	if ($sim eq 'qemu-rr' && $disk =~ /\.mdsk$/) {
		copy($disk, $tmpfile);
		$cmd  = "cat record.wr.fifo > out.dsk & ";
		$mdisks = "-mdisk record.wr.fifo -mdisk replay.dsk ";
		$cmd .= run_qemu("$SRCDIR/../$NAME-build/qemu", $tmpfile,
			"$output.replay", $mdisks);
		run_command($cmd);

		die if $disk =~ /\.mdsk$/ && (-s "out.dsk") == 0;
		$cmd = "diff replay.dsk out.dsk > /dev/null";
		run_command($cmd);
		copy("$output.replay", $output);
	}
	unlink("replay.dsk");
	unlink("out.dsk");
	unlink($tmpfile);
	exit 0;
} elsif ($sim eq "bochs") {
	copy($disk, $tmpfile);
	run_bochs ("bochs", $tmpfile, $output, int(rand(9999999)));
	unlink($tmpfile);
	exit 0;
} elsif ($sim eq "bochsm") {
	my $jitter = 1;
	$jitter = 100 if ($disk =~ /\.mdsk$/);
	for (my $j = 1; $j < $jitter; $j++) {
		copy($disk, $tmpfile);
		run_bochs ("bochs", $tmpfile, $output, $j);
		unlink($tmpfile);
	}
	exit 0;
} elsif ($sim eq "tapas") {
  copy($disk, $tmpfile);
	run_tapas ("tapas", $tmpfile, $output, int(rand(9999999)));
	unlink($tmpfile);
  exit 0;
} elsif ($sim eq "tapas.q") {
  copy($disk, $tmpfile);
  my $h=hostname;
	run_tapas_q ("tapas.q", $tmpfile, $output, "$ENV{LOGNAME}_${h}_$ENV{SESSION_NAME}_" . basename($disk));
	unlink($tmpfile);
  exit 0;
}



print "Simulator '$sim' not supported.\n";
exit 1;

# run_command(@args)
#
# Runs xsystem(@args).
# Also prints the command it's running and checks that it succeeded.
sub run_command {
	print join (' ', @_), "\n";
	die "command failed\n" if xsystem (@_);
}

# xsystem(@args)
#
# Creates a subprocess via exec(@args) and waits for it to complete.
# Relays common signals to the subprocess.
# If $timeout is set then the subprocess will be killed after that long.
sub xsystem {
    # QEMU turns off local echo and does not restore it if killed by a signal.
    # We compensate by restoring it ourselves.
    my $cleanup = sub {};
    if (isatty (0)) {
	my $termios = POSIX::Termios->new;
	$termios->getattr (0);
	$cleanup = sub { $termios->setattr (0, &POSIX::TCSANOW); }
    }

    # Create pipe for filtering output.
    pipe (my $in, my $out) or die "pipe: $!\n" if $kill_on_failure;

    my ($pid) = fork;
    if (!defined ($pid)) {
	# Fork failed.
	die "fork: $!\n";
    } elsif (!$pid) {
	# Running in child process.
	dup2 (fileno ($out), STDOUT_FILENO) or die "dup2: $!\n"
	  if $kill_on_failure;
	exec_setitimer (@_);
    } else {
	# Running in parent process.
	close $out if $kill_on_failure;

	my ($cause);
	local $SIG{ALRM} = sub { timeout ($pid, $cause, $cleanup); };
	local $SIG{INT} = sub { relay_signal ($pid, "INT", $cleanup); };
	local $SIG{TERM} = sub { relay_signal ($pid, "TERM", $cleanup); };
	alarm ($timeout * get_load_average () + 1) if defined ($timeout);

	if ($kill_on_failure) {
	    # Filter output.
	    my ($buf) = "";
	    my ($boots) = 0;
	    local ($|) = 1;
	    for (;;) {
		if (waitpid ($pid, WNOHANG) != 0) {
		    # Subprocess died.  Pass through any remaining data.
		    print $buf while sysread ($in, $buf, 4096) > 0;
		    last;
		}

		# Read and print out pipe data.
		my ($len) = length ($buf);
		waitpid ($pid, 0), last
		  if sysread ($in, $buf, 4096, $len) <= 0;
		print substr ($buf, $len);

		# Remove full lines from $buf and scan them for keywords.
		while ((my $idx = index ($buf, "\n")) >= 0) {
		    local $_ = substr ($buf, 0, $idx + 1, '');
		    next if defined ($cause);
		    if (/(Kernel PANIC|User process ABORT)/ ) {
			$cause = "\L$1\E";
			alarm (5);
		    } elsif (/Pintos booting/ && ++$boots > 1) {
			$cause = "triple fault";
			alarm (5);
		    } elsif (/FAILED/) {
			$cause = "test failure";
			alarm (5);
		    }
		}
	    }
	} else {
	    waitpid ($pid, 0);
	}
	alarm (0);
	&$cleanup ();

	if (WIFSIGNALED ($?) && WTERMSIG ($?) == SIGVTALRM ()) {
	    seek (STDOUT, 0, 2);
	    print "\nTIMEOUT after $timeout seconds of host CPU time\n";
	    exit 0;
	}

	return $?;
    }
}

# relay_signal($pid, $signal, &$cleanup)
#
# Relays $signal to $pid and then reinvokes it for us with the default
# handler.  Also cleans up temporary files and invokes $cleanup.
sub relay_signal {
    my ($pid, $signal, $cleanup) = @_;
    kill $signal, $pid;
    eval { File::Temp::cleanup() };	# Not defined in old File::Temp.
    &$cleanup ();
    $SIG{$signal} = 'DEFAULT';
    kill $signal, getpid ();
}

# timeout($pid, $cause, &$cleanup)
#
# Interrupts $pid and dies with a timeout error message,
# after invoking $cleanup.
sub timeout {
    my ($pid, $cause, $cleanup) = @_;
    kill "INT", $pid;
    waitpid ($pid, 0);
    &$cleanup ();
    seek (STDOUT, 0, 2);
    if (!defined ($cause)) {
	my ($load_avg) = `uptime` =~ /(load average:.*)$/i;
	print "\nTIMEOUT after ", time () - $start_time,
	  " seconds of wall-clock time";
	print  " - $load_avg" if defined $load_avg;
	print "\n";
    } else {
	print "Simulation terminated due to $cause.\n";
    }
    exit 0;
}

# Returns the system load average over the last minute.
# If the load average is less than 1.0 or cannot be determined, returns 1.0.
sub get_load_average {
    my ($avg) = `uptime` =~ /load average:\s*([^,]+),/;
    return $avg >= 1.0 ? $avg : 1.0;
}

# Calls setitimer to set a timeout, then execs what was passed to us.
sub exec_setitimer {
    if (defined $timeout) {
	if ($ ge 5.8.0) {
	    eval "
              use Time::HiRes qw(setitimer ITIMER_VIRTUAL);
              setitimer (ITIMER_VIRTUAL, $timeout, 0);
            ";
	} else {
	    { exec ("setitimer-helper", $timeout, @_); };
	    exit 1 if !$!{ENOENT};
	    print STDERR "warning: setitimer-helper is not installed, so ",
	      "CPU time limit will not be enforced\n";
	}
    }
    exec (@_);
    exit (1);
}

sub SIGVTALRM {
    use Config;
    my $i = 0;
    foreach my $name (split(' ', $Config{sig_name})) {
	return $i if $name eq 'VTALRM';
	$i++;
    }
    return 0;
}

# find_in_path ($program)
#
# Searches for $program in $ENV{PATH}.
# Returns $program if found, otherwise undef.
sub find_in_path {
    my ($program) = @_;
    -x "$_/$program" and return $program foreach split (':', $ENV{PATH});
    return;
}
