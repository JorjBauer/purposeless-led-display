#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $d = Display->new(destNode => 3);
$d->init();
$d->raw();
sleep(3);

print("starting\n");
$d->sendCommand('@');
exit(0);
