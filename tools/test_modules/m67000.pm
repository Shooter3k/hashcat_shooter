#!/usr/bin/env perl

##
## Mode 67000 is the legacy number for the yescrypt implementation now at
## mode 36100. Reuse the same generator and verifier for compatibility tests.
##

use strict;
use warnings;
use File::Basename qw(dirname);
use File::Spec;

require File::Spec->catfile (dirname (__FILE__), "m36100.pm");

1;
