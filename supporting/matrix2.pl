#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $d = Display->new(destNode => 3);
$d->init();
print("starting\n");
$d->matrix('test!',
	   255, 255, 255,
	   255, 255, 255);
exit(0);
