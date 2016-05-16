#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $d = Display->new(destNode => 3);
$d->init();

$d->{port}->purge_all();

my @t = localtime();
my $a = $t[2]; # Hour (0..23), 5 bits of data
my $b = $t[1]; # Minute (0..59), 6 bits of data
my $c = $t[0]; # second (0..59), 6 bits of data
$d->sendCommand("~~~Ck" . chr($a) . chr($b) . chr($c));
print $d->getResponse();
exit(0);
