#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $d = Display->new(destNode => 3);
$d->init();

print("starting\n");
$d->chase(127,
	  0, 0, 255);
exit(0);
