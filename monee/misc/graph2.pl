#!/usr/bin/perl -w

use warnings;
use strict;
use Switch;
use File::Copy;
use Net::Ping;

our $SRCDIR = "../monee";
our $tapas_windows = "10.20.254.105";
#The following variants require that no USB drive is connected at bootup
#our @variants = ("guest",	"monitor");

#The following variants require that no USB drive is connected at bootup
#time, and USB drive is connected after bootup
our @variants = ("vmware-auto",
	"vmware-auto-replay", "vmware-record", "vmware-bintrans",
  "vmware-auto-disable-accn", "vmware-auto-replay-disable-accn",
  "vmware-record-disable-accn", "vmware-bintrans-disable-accn");

#The following variants require a physical U3 Cruzer Micro disk inserted in
#the machine. The U3 Cruzer Micro disk must contain the loader image.
#our @variants = ("monitor-record");

usage() if ($#ARGV != 0);
my $vmname = $ARGV[0];

foreach my $variant (@variants) {
	run_variant($variant, $vmname);
}

sub run_variant
{
	my $variant = shift;
	my $vmname = shift;

	switch ($variant) {
		case "guest" {
			my $vmname_new = get_new_counterpart($vmname);
			system("make $vmname_new.dsk");
			system("./run_tapas.sh $vmname_new.dsk | tee serial.guest.$vmname");
		}
		case "monitor" {
			system("cp $vmname.dsk os.dsk");
			system("make -C $SRCDIR clean");
			system("make -C $SRCDIR BUILDFLAGS=\"-DNDEBUG\"");
			system("make $vmname.dsk");
			system("./run_tapas.sh mos.dsk | tee serial.monitor.$vmname");
		}
		case "monitor-record" {
			print "monitor-record: MAKE SURE THAT YOU HAVE A U3 Cruzer Micro ";
			print "disk physically inserted in the USB drive.\n";
			system("make $vmname.dsk");
			system("cp $vmname.dsk os.dsk");
			system("make -C $SRCDIR clean");
			system("make -C $SRCDIR BUILDFLAGS=\"-DNDEBUG -DRECORD_DISK=U3_Cruzer_Micro\"");
			system("make mos.dsk");
			system("./run_tapas.sh mos.dsk | tee serial.monitor-record.$vmname");
		}
		case /vmware/ {
			system("rm -f $vmname-s001.vmdk");
			system("make $vmname.dsk $vmname-s001.vmdk");
			construct_vmx_file($variant, $vmname);
			boot_tapas_into_windows();
			system("./run_tapas_vmware.sh $tapas_windows $vmname | tee serial.$variant.$vmname");
		}
	}
}

sub usage {
	print "Usage: graph2.pl <disk-name>\n";
	exit(1);
}

sub construct_vmx_file
{
	my $variant = shift;
	my $vmname = shift;

	system("rm -f $vmname.vmx && make $vmname.vmx");

	open(my $vmx_fp, "<$vmname.vmx") or die;
	open(my $variant_fp, ">$vmname.vmx.$variant") or die;
	while (my $line = <$vmx_fp>) {
		if ($line =~ /serial1.fileName = "serial.vmw.$vmname"/) {
			print $variant_fp "serial1.fileName = \"serial.$vmname\"\n";
		} else {
			print $variant_fp $line;
		}
	}
	close($vmx_fp);
	if ($variant eq "vmware-auto-replay" || $variant eq "vmware-record") {
		print $variant_fp "monitor.needreplay = \"TRUE\"\n";
	} elsif ($variant =~ /disable-accn/) {
		print $variant_fp "disable_acceleration = \"TRUE\"\n";
	}
	close($variant_fp);
	copy("$vmname.vmx.$variant", "$vmname.vmx");
}

sub boot_tapas_into_windows
{
	my $reachable = 0;
	my $p = Net::Ping->new();
	if ($p->ping($tapas_windows)) {
		$reachable = 1;
	}
	$p->close();
	return if ($reachable);
	system("./boot_tapas_into_windows.sh");
	local $| = 1;
	print "Waiting for tapas to boot into windows...";
	while (!$p->ping($tapas_windows)) {
		sleep(2);
		print ".";
	}
	print "done.\n";
}

sub get_new_counterpart
{
	my $vmname = shift;
	switch ($vmname) {
		case "threads_old" {return "threads";}
	}
	return $vmname;
}
