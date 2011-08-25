#!/usr/bin/perl

my $ldr = $ARGV[0];
my $size = (-s $ldr);
my $max_size = 0x60000;

#print "loader_size=$size. max allowed=$max_size\n";
if ($size > $max_size) {
  print "Loader too big: $size. max allowed: $max_size.\n";
  exit(1);
} else {
  exit(0);
}
