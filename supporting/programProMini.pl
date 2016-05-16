#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $file = shift || die "No .hex file path given";
open(FH, $file) || die "Couldn't open .hex file $file\n";

my $destNode = 3;

my $d = Display->new( destNode => $destNode );
print("starting up\n");

$d->sendCommand("\0\0    ");
sleep(1);
my $resp = $d->sendCommand('~~~Flsh');
die "Remote failed to confirm programming mode"
    unless ($resp eq 'Flsh!!');

print "Sending flash data\n";
my $count = 0;
while (<FH>) {
#    die "Invalid line; can't be > 61 chars"
#	if (length($_) > 61);

    my $orig = prepData($_);
    $resp = $d->sendCommand($orig);
    die "failed to get proper ack"
	unless ($resp eq $orig);

    print($count, "\n");

    $count++;
}
print "Waiting for confirmation of end of flash...\n";
expect('Flsh..');
print "Success!\n";

exit 0;

sub prepData {
    my ($l) = @_;

#    print "$l\n";

    # Turn the line $l (Intel HEX format data) in to a binary blob to send
    die "invalid data"
	unless ($l =~ /^:/ && length($_) >= 9);
    # Data looks like this:
    # :100000000C9466010C948E010C948E010C948E015C
    # :100010000C948E010C948E010C948E010C948E0124
    # Count: 1 byte (2 digits)
    # address: 2 bytes (4 digits), big-endian
    # type: 1 byte (2 digits)
    # ... so the end line of a hex file, which looks like
    # :00000001FF
    # ... will pack up as these bytes:
    #   ( 0x03, 0x00, 0x00, 0x01)
    # because the checksum is discarded.

    my @data;
    my $count = hex(substr($l, 1, 2)); # number of bytes of data on this line
    my $pos = 0;
    while ($pos < ($count + 3) * 2) { # +3 for the two address bytes and type; *2 for each digit
	push (@data, hex(substr($l, 3 + $pos, 2))); # Pull the two digits out of the string, read as hex
	$pos+=2;
    }

    # @data now contains the data to send to the remote end. Let's pack it up with a length header
#    use Data::Dumper;
#    print Dumper(\@data);

    my $d = pack("C*", scalar @data, @data);
    return $d;
}

sub expect {
    my ($what, $verbose) = @_;

    $what =~ s/[\r\n]//g; # Remove newlines

    my $resp = '';
    while (1) {
	my ($c, $r) = $d->opportunisticResponse();
	if ($c) {
	    $resp .= $r;
	    $resp =~ s/[\r\n]//g; # Remove newlines
	    if ($verbose) {
		print "$resp\n"
		    if ($resp);
	    }
	    last
		if ($resp =~ /$what/);
	}
    }
    
}
