#!/usr/bin/env perl
#***************************************************************************
#                                  _   _ ____  _
#  Project                     ___| | | |  _ \| |
#                             / __| | | | |_) | |
#                            | (__| |_| |  _ <| |___
#                             \___|\___/|_| \_\_____|
#
# Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution. The terms
# are also available at https://curl.se/docs/copyright.html.
#
# You may opt to use, copy, modify, merge, publish, distribute and/or sell
# copies of the Software, and permit persons to whom the Software is
# furnished to do so, under the terms of the COPYING file.
#
# This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
# KIND, either express or implied.
#
# SPDX-License-Identifier: curl
#
###########################################################################
use strict;
use warnings;
use Cwd qw(abs_path);

# Set up and verify a symlink planted at a --ssl-sessions save path

if($#ARGV < 2) {
    print "Usage: $0 prepare|check sessionsfile sentinelfile\n";
    exit 1;
}

my $sentinel = "sentinel content that must survive the ssl-sessions save\n";

# <precheck> expects an error message on stdout
sub errout {
    print $_[0] . "\n";
    exit 1;
}

my $cmd = shift @ARGV;
my $sessionsfile = shift @ARGV;
my $sentinelfile = shift @ARGV;

if($cmd eq "prepare") {
    open(my $fh, '>', $sentinelfile) or errout "$!";
    print $fh $sentinel;
    close($fh);
    unlink($sessionsfile);
    symlink(abs_path($sentinelfile), $sessionsfile) or errout "$!";
    exit 0;
}
elsif($cmd eq "check") {
    open(my $fh, '<', $sentinelfile) or die "$!";
    my $content = do { local $/; <$fh> };
    close($fh);
    if($content ne $sentinel) {
        print "sentinel file content was modified\n";
        exit 1;
    }
    if(-l $sessionsfile) {
        print "session file is still a symlink\n";
        exit 1;
    }
    exit 0;
}
print "Unsupported command $cmd\n";
exit 1;
