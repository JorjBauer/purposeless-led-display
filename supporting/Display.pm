#!/usr/bin/perl

package Display;

use strict;
use warnings;
use IO::Socket;
use Device::SerialPort;
use Time::HiRes qw/sleep/;

sub new {
    my $me = shift;
    my %opts = @_;

    my @port_names = ('/dev/tty.usbserial', '/dev/tty.KeySerial1', '/dev/ttyUSB0', '/dev/tty.usbserial-A9014RER', '/dev/tty.usbmodem1411', '/dev/tty.usbmodem1421', "/dev/ttyUSB1");

    my $default_delay = 2;

    my $sport;
    foreach my $port_name (@port_names) {
	$sport = Device::SerialPort->new( $port_name, 1, undef);

	# The /dev/tty.usbserial device doesn't reset the device, so we don't
	# have to have a startup delay
	$default_delay = 0
	    if ($sport && $port_name =~ /usbserial$/);

	last if $sport;
    }
    die "Unable to open serial device" unless $sport;
    
    $sport->user_msg(1);
    $sport->error_msg(1);
    $sport->databits(8);
    $sport->baudrate(115200);
    $sport->parity("none");
    $sport->stopbits(1);
    $sport->handshake("none");
    $sport->reset_error();
    
    $sport->purge_all();
    
    my $opts = {
	port => $sport,
	destNode => 3,
	delay => $default_delay, # delay, in seconds, while starting up
	%opts,

	textmode => 0,
	};

    if ($opts->{delay}) {
	sleep($opts->{delay});
    }

    return bless $opts, $me;
}

sub init {
    my ($this) = @_;

    # Init: \0\0 to exit text mode, and six spaces to finish any other command
    # followed by \0\0 to exit text mode and r to go in to raw mode, in case 
    # the first chunk was missed
    print("writing init sequence to be sure we're out of text mode\n");
    $this->sendCommand(chr(5)); # ENQ: what mode are you in?

    my ($c, $r);
    my $e = time() + 5; # allow 5 seconds for reply
    while (time() <= $e) {
	($c, $r) = $this->opportunisticResponse();
	if ($c == 1 && defined($r)) {
	    if ($r eq 'T') {
		$this->{textmode} = 1;
		print "In text mode\n";
		last;
	    } elsif ($r eq 'R') {
		$this->{textmode} = 0;
		print "In raw mode\n";
		last;
	    } else {
		print "unknown reply - ", ord($r), "?\n";
	    }
	}
    }
    unless ($c == 1 && $r && (($r eq 'T') || ($r eq 'R'))) {
	die "Receiver did not respond to ENQ";
    }
}

sub off {
    my ($this) = @_;

    $this->sendCommand('0');
}

sub writeWithReadback {
    my ($this, $cmd) = @_;
    my @chars = split(//, $cmd);
    foreach my $c (@chars) {
	$this->{port}->write($c);
	my $start = time();
	while (1) {
	    my ($count, $r) = $this->{port}->read(1);

	    last
		if ($count == 1 && $r eq $c);

	    if (time() > $start + 60) {
		die "Timeout trying to readback";
	    }
	}
    }
}

sub sendCommand {
    my ($this, $cmd, $l) = @_;

    $l ||= length($cmd);

    my $destNode = $this->{destNode};
#    print("sending command: '$cmd' l '$l' ");
    print("sending to dest $destNode length " . $l . "\n");
    $this->writeWithReadback(sprintf("%c%c", $destNode, $l) . $cmd);
    # Now get the ACK
    my $ack = '';
    my $start = time();
    while ($ack ne 'ACK' && $ack ne 'NAK') {
	my ($count, $r) = $this->{port}->read(1);
	if ($count) {
	    $ack .= $r;
	    $ack =~ s/[^NACK]//g;
	}
	if (time() > $start + 5) {
	    print("failed to ACK\n");
	    return undef;
	}
#	print "ACK is now '$ack'\n";
    }
    if ($ack eq 'ACK') {
#	print "Got ACK; reading ACK data\n";
	# Read back the ack packet's data
	my $size = ord($this->blockForChar());
#	print "Size is $size; reading those bytes\n";
	$ack = '';
	while ($size) {
	    $ack .= $this->blockForChar();
	    $size--;
#	    print "$ack\n";
	}
	my $l = length($ack);
#	print " done: final ack is '$ack' (len $l)\n";
	return $ack;
    }
    die "NAK"
	if ($ack eq 'NAK');
    return 1;
}

sub blockForChar {
    my ($this) = @_;

    while (1) {
	my ($count, $r) = $this->{port}->read(1);
	return $r
	    if ($count);
    }
    # NOTREACHED
}

sub getResponse {
    my ($this) = @_;
    my $str = '';
    while (1) {
	my ($count, $r) = $this->{port}->read(1);
	if ($count) {
	    $str .= $r;
	    last if ($r =~ /\n/);
	}
    }
    return $str;
}

sub opportunisticResponse {
    my ($this) = @_;
    my ($count, $r) = $this->{port}->read(1024);
    return ($count, $r);
}

sub endTextMode {
    my ($this) = @_;
    if ($this->{textmode}) {
	$this->sendCommand("\0\0");
    }
    $this->{textmode} = 0;
}

sub raw {
    my ($this) = @_;

    $this->endTextMode();
    $this->sendCommand('r');
}

sub text {
    my ($this, $txt) = @_;

    unless ($this->{textmode}) {
	$this->sendCommand("t");
	sleep(0.5);
	$this->{textmode} = 1;
    }

    # FIXME: limit $txt to 61
    $this->sendCommand($txt);
}

sub brightness {
    my ($this, $b) = @_;

    $this->endTextMode();
    $this->sendCommand("b" . chr($b));
}

sub chase {
    my ($this, $repeat, $r, $g, $b) = @_;

    $this->endTextMode();
    $this->sendCommand("!" . chr($repeat) . 
		       chr($r) .
		       chr($g) .
		       chr($b) );
}

sub ring {
    my ($this, $repeat, $r, $g, $b, $dir) = @_;
    $this->endTextMode();
    $this->sendCommand("R" . chr($repeat) . 
		       chr($r) .
		       chr($g) .
		       chr($b) .
		       chr($dir) );
}

sub twinkle {
    my ($this) = @_;

    $this->endTextMode();
    $this->sendCommand("T");
}

sub theater {
    my ($this) = @_;

    $this->endTextMode();
    $this->sendCommand("@");
}

sub matrix {
    my ($this, $txt5, $r1, $g1, $b1, $r2, $g2, $b2) = @_;

    die "Inappropriate text (must be 5 chars)"
	unless length($txt5) == 5;

    $this->endTextMode();
    $this->sendCommand('M' . $txt5 . 
		       chr($b1) .   # B
		       chr($g1) .   # G
		       chr($r1) . # R
		       chr($b2) .   # B
		       chr($g2) . # G
		       chr($r2));   # R
}

sub rainbow {
    my ($this) = @_;

    $this->endTextMode();
    $this->sendCommand('~');
}

sub tardis {
    my ($this) = @_;
    $this->endTextMode();
    $this->sendCommand('|');
}

sub tardisPillar {
    my ($this) = @_;
    $this->endTextMode();
    $this->sendCommand('/');
}

sub life {
    my ($this) = @_;
    $this->endTextMode();
    $this->sendCommand('l');
}

sub rotate {
    my ($this) = @_;
    $this->endTextMode();
    $this->sendCommand('$');
}

sub fade {
    my ($this, $delay, $num) = @_;

    $this->sendCommand('d' . chr($delay) . chr($num));
}

sub test {
    my ($this) = @_;

    $this->sendCommand('`');
}

1;
