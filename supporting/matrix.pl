#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $d = Display->new(destNode => 3);
$d->init();
print("starting\n");
$d->matrix('Jorj!',
	   255, 0, 255, # purple
	   0, 160, 40); # pale green
exit(0);
