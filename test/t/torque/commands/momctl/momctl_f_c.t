#!/usr/bin/perl

use strict;
use warnings;

use FindBin;
use TestLibFinder;
use lib test_lib_loc();


# Test Modules
use CRI::Test;

# Test Description
plan('no_plan');
setDesc('Momctl -f <HOSTLIST_FILE> -c <JOBID>');

# Variables
my %momctl;
my $hostlistfile = $props->get_property('mom.host.list.file');
my @hosts        = split(/,|\s/, `cat $hostlistfile`);

# Create some stale jobs
my $job_id = 'all'; # stubed out

%momctl = runCommand("momctl -f $hostlistfile -c $job_id", test_success => 1);

# Check the stdout
my $stdout = $momctl{ 'STDOUT' };
foreach my $host (@hosts)
  {

  ok($stdout =~ /job clear request successful on ${host}/i, "Checking output of 'momctl -f $hostlistfile -c $job_id' for $host");

  }
