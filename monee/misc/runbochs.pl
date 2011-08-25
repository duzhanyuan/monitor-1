#!/usr/bin/perl -w

use strict;
use warnings;
use POSIX;
use Getopt::Long qw(:config bundling);

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
	my ($file) = shift;
	my ($size) = -s $file;
	die "$file: stat: $!\n" if !defined $size;
	die "$file: size not a multiple of 512 bytes\n" if $size % 512;
	my ($cyl_size) = 512 * 16 * 63;
	my ($cylinders) = ceil ($size / $cyl_size);
	extend_disk ($file, $cylinders * $cyl_size) if $size % $cyl_size;

	print "CAPACITY => $size / 512, C => $cylinders, H => 16, S => 63\n";
}

sub usage {
	print "Usage: runbochs.pl <disk-name>\n";
	exit(1);
}

usage() if ($#ARGV != 0);
our($hda) = $ARGV[0];

disk_geometry($hda);
