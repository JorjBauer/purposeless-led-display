#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $d = Display->new(destNode => 3);
$d->init();

$d->{port}->purge_all();
print $d->sendCommand("~~~Rset"), "\n";
exit(0);
