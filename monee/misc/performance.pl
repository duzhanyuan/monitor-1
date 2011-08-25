#!/usr/bin/perl -w
use strict;
use warnings;
use File::Copy;
use POSIX ":sys_wait_h";
use Getopt::Long;
use Switch;

my $machine = "tapas";
#my $machine = "qemu";
my $monitor_dir = "../monitor";
my $MAX_TU_SIZE = 24;

GetOptions(
  "m=s" => \$machine,
);

my @cache_sizes = (128, 96, 64, 32, 24, 22, 20, 18, 17, 16);

#system("make pintos_old.dsk");
#system("make pintos_new.dsk");

#run("pintos_new.dsk", "serial.pintos");
test("default");
#test("no_jumptable1");
#test("tc_random");
#test("tc_clock");
#test("tu_size");

output_results("tc_random", "tc_clock", "native", "default", "no_chaining",
  "no_jumptable1", "tu_size", "rr");

#test("no_chaining");
#test("record_replay");
#build_monitor("tc_random.1");

sub make_uniform_length
{
  my $input = shift;
  my $length = shift;
  while (length($input) < $length) {
    $input = "$input ";
  }
  return $input;
}

sub output_variant_results
{
  my $arg = shift;
  my $param = shift;
  my $fp;
  if ($param != -1) {
    open($fp, "<serial.mpintos.$arg.$param") or return;
  } elsif ($arg eq "native") {
    open($fp, "<serial.pintos") or return;
  } else {
    open($fp, "<serial.mpintos.$arg") or return;
  }
  while (my $line = <$fp>) {
    if ($line =~ /^([^ ()]*)\(?(\d*)\)?: elapsed=(\d*)$/) {
      my $testname = $1;
      my $testvariant = $2;
      my $elapsed = $3;
      $testname =~ s/_test$//;
      $testname = "$testname$testvariant";
      $arg = make_uniform_length($arg, 16);
      $param = make_uniform_length($param,4);
      $testname = make_uniform_length($testname,20);
      print "$arg\t$param\t$testname\t$elapsed\n";
    }
  }
  close($fp);
}

sub output_results
{
  my @args = @_;
  foreach my $arg (@args) {
    if ($arg =~ /^tc_/) {
      foreach my $cache_size (@cache_sizes) {
        output_variant_results($arg, $cache_size);
      }
    } elsif ($arg =~ /^tu_size$/) {
      for (my $tu_size = 1; $tu_size <= $MAX_TU_SIZE; $tu_size++) {
        output_variant_results($arg, $tu_size);
      }
    } else {
      output_variant_results($arg, -1);
    }
  }
}

sub test
{
  my $variant = shift;
  switch ($variant) {
    case ["no_chaining","no_jumptable1","default","record_replay"] {
      build_monitor($variant);
      run("mpintos.dsk", "serial.mpintos.$variant");
    }
    case ["tc_random","tc_clock"] {
      foreach my $cache_size (@cache_sizes) {
        my $cvariant = "$variant.$cache_size";
        build_monitor($cvariant);
        run("mpintos.dsk", "serial.mpintos.$cvariant");
      }
    }
    case ["tu_size"] {
      for (my $tu_size = 1; $tu_size <= $MAX_TU_SIZE; $tu_size++) {
        my $tu_variant = "$variant.$tu_size";
        build_monitor($tu_variant);
        run("mpintos.dsk", "serial.mpintos.$tu_variant");
      }
    }
    die;
  }
}

sub build_monitor
{
  my $variant = shift;
  my $cflags;
  if ($variant =~ /tc_clock.(\d*)/) {
    my $tc_pool_size = $1;
    $cflags = "-DTB_REPLACEMENT_CLOCK -DMAX_NUM_TC_PAGES=$tc_pool_size";
  } elsif ($variant =~ /tc_random.(\d*)/) {
    my $tc_pool_size = $1;
    $cflags = "-DTB_REPLACEMENT_RANDOM -DMAX_NUM_TC_PAGES=$tc_pool_size";
  } elsif ($variant =~ /tu_size.(\d*)/) {
    my $tu_size = $1;
    $cflags = "-DMAX_TU_SIZE=$tu_size";
  } else {
    switch ($variant) {
      case "no_chaining" { $cflags = "-DNO_CHAINING";}
      case "no_jumptable1" { $cflags = "-DNO_JUMPTABLE1";}
      case "default" { $cflags = ""; }
      case "record_replay" { die "not implemented"; }
      die;
    }
  }
  system("make -C $monitor_dir clean");
  system("make -C $monitor_dir INPUT_CFLAGS=\" -DNDEBUG $cflags \"");
  system("make mpintos.dsk");
}

sub run
{
  if ($machine eq "tapas") {
    run_on_tapas(@_);
  } elsif ($machine eq "vmware") {
    run_on_vmware(@_);
  } elsif ($machine eq "qemu") {
    run_on_qemu(@_);
  } else {
    die;
  }
}

sub run_on_tapas
{
  my $disk_image = shift;
  my $serial_file = shift;
  system("./run_tapas.sh $disk_image | tee $serial_file");
}

sub run_on_vmware
{
  die "not implemented";
}

sub run_on_qemu
{
  die "not implemented";
}
