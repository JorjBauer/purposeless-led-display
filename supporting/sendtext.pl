#!/usr/bin/perl

use strict;
use warnings;
use Display;
use Time::HiRes qw/sleep/;

my $txt = shift || die "no text given";

my $destNode = 3;
my $d = Display->new( destNode => $destNode );
$d->init();
$d->text($txt);
