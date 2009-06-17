#!/usr/bin/perl -w
#-
# Copyright (c) 2007-2009 Linpro AS
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer
#    in this position and unchanged.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $Id$
#

use strict;

use Template;

my %cache = ();

foreach my $dir (@ARGV) {
    open(STATUS, $dir . '/regress.status') or next;
    while (<STATUS>) {
	my ($name, $value) = split(/: /);
	chomp($value) if defined($value);
	$cache{$dir}{$name} = $value;
    }
    close(STATUS);
}

my @tests = ();

foreach my $dir (sort({ $b cmp $a } keys(%cache))) {
    if ($dir =~ /^varnish-(\d\d\d\d)(\d\d)(\d\d)-(\d\d)(\d\d)(\d\d)Z$/) {
	$cache{$dir}{'title'} = "$1-$2-$3 $4:$5:$6 UTC";
	$cache{$dir}{'link'} = $dir;
	push(@tests, $cache{$dir});
    }
}

my $template = new Template('TAG_STYLE' => 'html');
$template->process('summary.html', { 'tests' => \@tests });
