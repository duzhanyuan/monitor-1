#!/usr/bin/perl -w
use strict;
use warnings;
use Getopt::Long;

my $index = 0;
my @lines, my @state_start, my @state_stop;
my $num_states = 0;
my @mem;
my $num_out_states = 2;

GetOptions(
  "n=i" => \$num_out_states
);

$num_out_states >= 1 or die;

$SIG{'HUP'} = 'HUP_handler';

while (my $line = <STDIN>) {
  $lines[$index] = $line;
  if ($line =~ /^MS: /) {
    $state_start[$num_states] = $index;
  } elsif ($line =~ /machine_state_stop/) {
    $state_stop[$num_states] = $index;
    $num_states++;
  } elsif ($line =~ /^\tmem\[([0-9a-f]*)\]:/) {
    my $mem_size = hex($1);
    my $num_read;
    $num_read = read(STDIN, $mem[$num_states], $mem_size+1);
  }
  $num_states <= ($num_out_states + 1) or die;
  $state_start[0] == 0 or die;

  if ($num_states == ($num_out_states + 1)) {
    for (my $i = 0; $i < $state_start[1]; $i++) {
      shift @lines;
    }
    $state_start[0] = 0;
    $state_stop[0] = $state_stop[1] - $state_start[1];
    $mem[0] = $mem[1];

    $state_stop[$num_out_states] == $index or die;
    $index -= $state_start[1];

    for (my $n = 2; $n <= $num_out_states; $n++) {
      $state_start[$n-1] = $state_start[$n] - $state_start[1];
      $state_stop[$n-1] = $state_stop[$n] - $state_start[1];
      $mem[$n-1] = $mem[$n];
    }
    $num_states = $num_out_states;
  }
  $index++;
}

#print the lines array as it is (includes MS: and other entries).
for (my $i = 0; $i < $index; $i++) {
  print $lines[$i];
  if ($lines[$i] =~ /^\tmem\[([0-9a-f]*)\]:/) {
    if (!(defined $state_start[1]) || $i < $state_start[1]) {
      print $mem[0];
    } elsif (!(defined $state_start[2]) || $i < $state_start[2]) {
			if (!defined $mem[1]) {
				print "i=$i, state_start[1]=$state_start[1]\n";
			}
      print $mem[1];
    } else {
      print $mem[2];
    }
  }
}
for (my $i = 0; $i < 1024*1024; $i++) {
  print "0";
}
exit(0);

#sub decrement_n_exec_and_print {
#  #print initial state with n_exec decremented.
#  my $line = shift;
#  my $header=shift;
#  my $pattern = shift;
#  $line =~ /$header([0-9a-f]*) (.*)$pattern(.*)/ or die "line=$line";
#  my $n_exec = $1;
#  my $prefix = $2;
#  my $suffix = $3;
#  $n_exec--;
#  printf("$header%016llx $2$pattern$3\n", $n_exec);
#}


sub HUP_handler {
}
