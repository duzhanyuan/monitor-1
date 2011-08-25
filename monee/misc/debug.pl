#!/usr/bin/perl -w
use strict;
use warnings;
use File::Copy;
use POSIX ":sys_wait_h";
use Getopt::Long;

my $rec_freq = 0x100000;
my $rec_threshold = 0; #0x100000; #0x724725;

my $max_tu_size = 0;
my $replay_file = ""; #"replay.dsk";

my $log_flags = ""; #" -DTRANSLATE"; #" -DIN_ASM -DOUT_ASM";

my $rr_log_machine_state_size = 1024;
my $rr_log_entry_size = 48;
my $compile_only = 0;
my $target = "mos.dsk";
my $debug_mode;
#my $target = "mgentoo.dsk";
my $iter = 0;

#constants
my $PANIC_EXITCODE = 12;
my $MISMATCH_EXITCODE = 13;
my $INSN_COUNT_ERROR_EXITCODE = 14;

GetOptions(
  "c" => \$compile_only,
  "replay=s" => \$replay_file,
  "target=s" => \$target,
  "d=s" => \$debug_mode,
	"freq=o" => \$rec_freq,
	"thresh=o" => \$rec_threshold,
);

sub build_qemu
{
  #unlink("../qemu/x86_64-softmmu/vl.o");
  my $ec = system("make -C ../qemu RR_FLAGS=\"\"");
  return $ec >> 8;
}

sub build_monitor
{
  my $rec_print_freq = shift;
  my $max_tu_size_str = ($max_tu_size==0)?"":" -DTU_SIZE=$max_tu_size";

  unlink("../monee-build/mon/sys/rr_log.o");
  my @rr_flags = ("-DREC_PRINT_FREQ=$rec_print_freq");
  push(@rr_flags, "-DREC_THRESHOLD=$rec_threshold");
	#push(@rr_flags, "-DRECORD_DISK=QEMU:record.wr.fifo");
	#push(@rr_flags, "-DTRANSLATE") if $debug_mode eq "asm";
  push(@rr_flags, $log_flags);
  push(@rr_flags, $max_tu_size_str);
	if ($replay_file ne "") {
    my $truncated_replay_file = sprintf("%.16s", $replay_file);
		push(@rr_flags, "-DREPLAY_DISK=QEMU:$truncated_replay_file");
	}
  my $flags = "RR_FLAGS=\"@rr_flags\"";
	print "make $target $flags\n";
  my $ec = system("make $target $flags");
  return $ec >> 8;
}

sub log_tail_diff
{
  my $log1 = shift;
  my $log2 = shift;
  my $num_states1 = 0, my $num_states2 = 0;
  my @start_state1, my @stop_state1;
  my @start_state2, my @stop_state2;

  if (!(-e $log1) || !(-e $log2)) {
    return 1;
  }

  open(LOG1, "<$log1");
  while (my $line = <LOG1>) {
    if ($line =~ /machine_state_start/) {
      $start_state1[$num_states1] = $line;
    } elsif ($line =~ /machine_state_stop/) {
      $stop_state1[$num_states1] = $line;
      $num_states1++;
    }
  }
  close(LOG1);

  open(LOG2, "<$log2");
  while (my $line = <LOG2>) {
    if ($line =~ /machine_state_start/) {
      $start_state2[$num_states2] = $line;
    } elsif ($line =~ /machine_state_stop/) {
      $stop_state2[$num_states2] = $line;
      $num_states2++;
    }
  }
  close(LOG2);

#  for (my $i = 0; $i < 2; $i++) {
#    print "start_state1[$i] = $start_state1[$i]\n";
#    print "start_state2[$i] = $start_state2[$i]\n";
#    print "start_stop1[$i] = $stop_state1[$i]\n";
#    print "start_stop2[$i] = $stop_state2[$i]\n";
#  }

  return 1 if ((defined $start_state1[0]) != (defined $start_state2[0]));
  return 1 if ((defined $start_state1[1]) != (defined $start_state2[1]));
  return 1 if ((defined $stop_state1[0]) != (defined $stop_state2[0]));
  return 1 if ((defined $stop_state1[1]) != (defined $stop_state2[1]));

  return 1 if (defined $start_state1[0] && $start_state1[0]ne$start_state2[0]);
  return 1 if (defined $start_state1[1] && $start_state1[1]ne$start_state2[1]);
  return 1 if (defined $stop_state1[0] && $stop_state1[0] ne $stop_state2[0]);
  return 1 if (defined $stop_state1[1] && $stop_state1[1] ne $stop_state2[1]);

  return 0;
}

sub killme
{
  while (my $pid = shift) {
    kill(1, $pid);
    kill(9, $pid);
  }
}

sub qemu_debug_mode
{
	my $debug_mode = shift;
	return "in_asm" if $debug_mode eq "asm";
	return "all" if $debug_mode eq "all";
	die;
}

sub test
{
  my $rec_print_freq = shift;

  build_monitor($rec_print_freq) == 0 or die;

  if ($compile_only) {
    exit(0);
  }

  my $pid_record = fork();
  if ($pid_record == 0) {
    my @diskargs;
    @diskargs = ("$target");
    my @args = ("./qemu", @diskargs, "-m 12");
		push (@args, "-d", qemu_debug_mode($debug_mode)) if (defined $debug_mode);
		push (@args, "-nographic -monitor null");
		push (@args, "-mdisk record.wr.fifo");
		push (@args, "-mdisk $replay_file") if $replay_file ne "";
		push (@args, "-serial /dev/null");
		push (@args, "-serial file:/tmp/serial");
    print "Exec'ing @args.\n";
    exec("@args");
  }
  print "Forked monitor[$pid_record]...\n";

  my $pid_tee = fork();

  if ($pid_tee == 0) {
		my @args = ("tee", "log1.fifo", ">log.dsk", "<record.wr.fifo");
    print "Exec'ing @args.\n";
    exec("@args");
  }
  print "Forked tee[$pid_tee]...\n";

  my $pid_qemu = fork();

  if ($pid_qemu == 0) {
    my @args = ("./qemu", "--use_replay_log", "log1.fifo");
		push (@args, "-nographic -monitor null");
		push (@args, "-serial /dev/null");
		push (@args, "-serial file:/tmp/serial.$iter.replay");
		push (@args, "-hda dummy.dsk");
    print "Exec'ing @args.\n";
    exec("@args");
  }
  print "Forked qemu[$pid_qemu]...\n";

#  my $pid_tail_state = fork();
#
#  if ($pid_tail_state == 0) {
#    my @args = ("./tail_state.pl", "<log2.fifo", ">log.tail.tmp");
#    print "Exec'ing @args.\n";
#    exec("@args");
#  }
#  print "Forked tail_state[$pid_tail_state]...\n";

  waitpid($pid_qemu, 0);
	copy("/tmp/serial", "/tmp/serial.$iter");
  my $ec = $?;
  if ($ec == -1) {
    print "failed to execute qemu\n";
    #print "Killing $pid_record,$pid_tee($pid_tail_state should die itself)\n";
    print "Killing $pid_record,$pid_tee..\n";
    killme($pid_record, $pid_tee);
    exit(1);
  } elsif ($ec & 127) {
    printf "Child[qemu] died with signal %d, %s coredump\n", ($ec & 127),
    ($ec & 128) ? 'with' : 'without';
    #print "Killing $pid_record,$pid_tee($pid_tail_state should die itself)\n";
    print "Killing $pid_record,$pid_tee..\n";
    killme($pid_record, $pid_tee);
    exit(1);
  } else {
    printf "Child[qemu] exited with value %d.\n", $ec >> 8;
    #print "Killing $pid_record,$pid_tee($pid_tail_state should die itself)\n";
    print "Killing $pid_record,$pid_tee..\n";
    do {
      killme($pid_record, $pid_tee);
    } while (wait() != -1); 
    my $exitcode = $ec >> 8;
    ($exitcode == 0)
			or ($exitcode == $PANIC_EXITCODE)
			or ($exitcode == $MISMATCH_EXITCODE)
			or ($exitcode == $INSN_COUNT_ERROR_EXITCODE) or die;
    return $exitcode;
  }
}

my $exitcode;
my $done = 0;

unlink("replay.dsk", "replay.dsk.bak");
build_qemu() == 0 or die;

do {
  print "\n\n====== ITER #$iter [rec $rec_freq] =====\n\n";
  $exitcode = test($rec_freq);
	move("/tmp/qemu.log", "/tmp/qemu.log.$iter") if (-e "/tmp/qemu.log");
  if ($rec_freq > 1) {
    my $tail_state_args = "";
		if ($exitcode == 0) {
			print "Success!\n";
			#$tail_state_args = "-n 2";
			exit 0;
		} elsif ($exitcode != $MISMATCH_EXITCODE) {
      #use only the last state
      $tail_state_args = "-n 1";
    } else {
      $tail_state_args = "-n 2";
    }
    print "./tail_state.pl $tail_state_args < log.dsk > replay.dsk";
    system("./tail_state.pl $tail_state_args < log.dsk > replay.dsk");
    $replay_file = "replay.dsk";
    $rec_freq = $rec_freq/16;
  } else {
    if ($max_tu_size == 0) {
      $max_tu_size = 1;
    } elsif ($log_flags eq "") {
      $log_flags = " -DTRANSLATE";
    } else {
      $done = 1;
    }
  }
  copy("replay.dsk", "replay.dsk.$iter");
  copy("log.dsk", "log.dsk.$iter");
  copy("../lemon-build/mon/monitor.o", "monitor.o.$iter");
  copy("/tmp/qemu.log", "/tmp/qemu.log.$iter");
  $iter++;
} while (!$done);
