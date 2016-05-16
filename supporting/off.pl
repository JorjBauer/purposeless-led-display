#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

srand(time());

my $destNode = 3;

my $d = Display->new( destNode => $destNode );
$d->init();
$d->off();
