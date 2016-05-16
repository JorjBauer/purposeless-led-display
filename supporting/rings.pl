#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $d = Display->new(destNode => 3);
$d->init();

# Set the fade timer to half its normal value
$d->fade(50, 16);
$d->ring(127, 
	 0, 0, 255,
	 1);
