#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $destNode = 3;

my $d = Display->new( destNode => $destNode );
print("starting up\n");

my $resp = $d->sendCommand('~~~FuS+' . chr(0xDB)); # Set high fuse to enable the bootloader (0xDA) or disable it (0xDB)
print "$resp\n";
