#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $destNode = 3;

my $d = Display->new( destNode => $destNode );
print("starting up\n");

my $resp = $d->sendCommand('~~~Fus+');
print "$resp\n";
