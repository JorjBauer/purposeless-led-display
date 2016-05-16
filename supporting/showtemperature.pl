#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $d = Display->new(destNode => 3);
$d->init();

$d->{port}->purge_all();
$d->sendCommand("~~~Temp");
print $d->getResponse();
exit(0);
