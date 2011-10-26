#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use TestLibFinder;
use lib test_lib_loc();


use CRI::Test;

plan('no_plan');
setDesc("Install 'Berkley Lab Checkpoint/Restart (BLCR) for Linux'");

my $testbase = $FindBin::Bin;


execute_tests(
        "$testbase/010_get_blcr_src.t",
        "$testbase/030_configure_blcr.t",
        "$testbase/040_make_blcr.t",
        "$testbase/050_make_insmod_blcr.t",
#        "$testbase/060_make_check_blcr.t",
        "$testbase/070_make_install_blcr.t", 
        );

execute_tests(
#        "$testbase/environment_config.bat", # This can be inconsistant
        );