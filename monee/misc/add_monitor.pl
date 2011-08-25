#!/usr/bin/perl -w
use strict;

our $SECTOR_SIZE = 512;

if ($#ARGV != 3) {
  print "Usage: add_monitor.pl <target-disk> <monitor-disk> <monitor-ofs> <orig-disk>\n";
  exit(1);
}

my $target = $ARGV[0];
my $monitor = $ARGV[1];
my $monitor_ofs = $ARGV[2];
my $orig_disk = $ARGV[3];

my $monitor_size = (-s $monitor)/$SECTOR_SIZE;

#my $target_size = (stat($orig_disk))[7]/$SECTOR_SIZE or die;
my $target_size = (stat($target))[7]/$SECTOR_SIZE or die;

$monitor_ofs >= 1 or die;
$monitor_size + ($monitor_ofs - 1) <= $target_size or die "Error: monitor_size=$monitor_size, monitor_ofs=$monitor_ofs, target_size=$target_size\n";

open(my $target_fp, "+<$target") or die "an error occured: $!";
open(my $monitor_fp, "+<$monitor") or die "an error occured: $!";
open(my $orig_fp, "+<$orig_disk") or die "an error occured: $!";
binmode $target_fp;
binmode $monitor_fp;
binmode $orig_fp;
for (my $sec = 0; $sec < $monitor_size; $sec++) {
  my $sector = read_sector($monitor_fp, $sec);
  write_sector($target_fp, $sec + ($monitor_ofs - 1), $sector);
}

#my $target_bootsector = read_sector($target_fp, 0);
my $monitor_bootsector = read_sector($monitor_fp, 0);
my $orig_bootsector = read_sector($orig_fp, 0);

write_sector($target_fp, 0, $monitor_bootsector);
write_sector($target_fp, $monitor_ofs-1, $orig_bootsector);

close($target_fp);
close($monitor_fp);

sub read_sector {
  my $fp = shift;
  my $secnum = shift;
  my $sector="";

  seek $fp, $secnum*$SECTOR_SIZE, 0;
  read($fp, $sector, $SECTOR_SIZE);
  return $sector;
}

sub write_sector {
  my $fp = shift;
  my $secnum = shift;
  my $sector = shift;

  seek $fp, $secnum*$SECTOR_SIZE, 0;
  print $fp $sector;
  #for (my $ofs = 0; $ofs < $SECTOR_SIZE; $ofs++) {
  #print $fp $$sector[$ofs];
  #}
}
