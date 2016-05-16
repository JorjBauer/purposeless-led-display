#!/usr/bin/perl

use strict;
use warnings;

my @hexdigits=('0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F');

# Take an intel hex file and double the number of bytes per line
# (as long as they are contiguous).

#:100000000C9466010C948E010C948E010C948E015C
#:100010000C948E010C948E010C948E010C948E0124

my $prevline;
my $prevaddr = 0;
my $prevtype = '';
my $prevsize;
my $prevdata;

while (<STDIN>) {
    while ($_ =~ /[\cJ\cM]$/m) {
	chop;
    }

    die "Bad intel hex file"
	unless (/^:/);
    my ($size, $addr, $type, $data, $checksum) = (/^:(..)(....)(..)(.+)?(..)$/);
    die "bad data"
	unless defined $checksum;
#    print "line $_ Checksum $checksum\n";
    $size = hex($size);
    $addr = hex($addr);

    if (defined($prevline) && 
	$prevaddr == $addr - $prevsize) {

	# Double the data
	print ":", dectohex($size + $prevsize, 2), dectohex($prevaddr, 4), $type, $prevdata, $data, "00\n"; # Fake 00 checksum; we don't actually care about them
	$prevline = undef;
    } elsif (defined($prevline)) {
	# Not a candidate for doubling
	print $prevline, "\n";
	$prevline = $_;
	$prevaddr = $addr;
	$prevtype = $type;
	$prevsize = $size;
	$prevdata = $data;
    } else {
	$prevline = $_;
	$prevaddr = $addr;
	$prevtype = $type;
	$prevsize = $size;
	$prevdata = $data;
    }
}

if ($prevline) {
    # Anything in the buffer @ the end of the file
    print $prevline, "\n";
}


exit 0;

sub dectohex {
    my ($dec, $numdigits) = @_;
    $numdigits ||= 2; # set a default
    my $hex = '';
    while ($dec) {
        $hex = $hexdigits[($dec) % 16] . $hex;
        $dec >>= 4;
    }
    while (length($hex) < $numdigits) {
        $hex = '0' . $hex;
    }
    return $hex;
}
