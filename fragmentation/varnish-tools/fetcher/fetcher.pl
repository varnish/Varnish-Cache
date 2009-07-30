#!/usr/bin/perl -w
#-
# Copyright (c) 2007-2009 Linpro AS
# Copyright (c) 2009 Dag-Erling Coïdan Smørgrav
# All rights reserved.
#
# Author: Dag-Erling Smørgrav <des@des.no>
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

our $VERSION = '$Id$';

package Varnish::Fetcher;

use strict;
use File::Basename;
use File::Path;
use Getopt::Long qw(:config bundling require_order auto_version);
use IO::Handle;
use IO::Multiplex;
use LWP::UserAgent;
use Socket;
use Time::HiRes qw(gettimeofday tv_interval);
use URI;

our %URI;
our %BANNED;
our @TODO;
our %CHILD;
our $BUSY;
our $DONE;

our $continue = 0;
our $delay = 0;
our $jobs = 1;
our $quiet = 0;
our $random = 0;
our $save = 0;

our $url_re = qr{
    \b(?:href|src)=[\'\"]\s*
	([^\'\"\?\#]+)		# capture URL
	(?:[\?\#][^\'\"]*)?	# discard fragment / query
	\s*[\'\"]
    }iox;

sub new($$) {
    my ($this, $mux, $fh) = @_;
    my $class = ref($this) || $this;

    bless {
	'mux' => $mux,
	'fh' => $fh,
	'url' => undef,
	'uri' => undef,
    };
}

sub info(@) {

    print(STDOUT "[$$] ", join(' ', @_), "\n")
	unless ($quiet);
}

sub error(@) {

    print(STDERR "[$$] ", join(' ', @_), "\n")
	unless ($quiet);
}

# Child
sub run($$) {
    my ($self, $s) = @_;

    my $check = 1;
    my $ua = new LWP::UserAgent('keep_alive' => 3);
    $ua->requests_redirectable([]);
    for (;;) {
	$0 = "[fetcher] idle";
	my $url = <$s>;
	exit(0)
	    unless defined($url);
	chomp($url);
	if ($url eq "done") {
	    last;
	} elsif ($url eq "check") {
	    $check = 1;
	    next;
	} elsif ($url eq "no check") {
	    $check = 0;
	    next;
	}
	$0 = "[fetcher] requesting $url";
	info("Retrieving $url");
	my $resp = $ua->get($url);
	if ($check) {
	    $0 = "[fetcher] checking $url";
	    if ($resp->is_redirect()) {
		$s->write("ban $url\n");
		$url = $resp->header('Location') ||
		    $resp->header('Content-Location');
		info("Redirected to $url");
		$s->write("add $url\n")
		    if $url;
	    } elsif ($resp->is_success()) {
		if ($resp->header('Content-Type') =~ m/^text\//) {
		    my %urls = map({ $_ => 1 } ($resp->content =~ m/$url_re/g));
		    foreach (keys(%urls)) {
			$s->write("add $_\n");
		    }
		}
		if ($save && $url !~ m/\/$/) {
		    my $uri = URI->new($url);
		    my $path = "." . $uri->path;
		    my $dir = dirname($path);
		    eval {
			mkpath($dir);
			if (open(my $fh, ">", $path)) {
			    info("Saving $url to $path");
			    print($fh $resp->content);
			    close($fh);
			} else {
			    die("open($path): $!\n");
			}
		    };
		    if ($@) {
			print(STDERR $@);
		    }
		}
	    } elsif ($resp->is_error()) {
		error("Failed to retrieve $url");
		$s->write("ban $url\n");
	    } else {
		error("Unsupported response type:",
		    $resp->status_line(), "\n");
	    }
	}
	select(undef, undef, undef, $delay)
	    if $delay;
	$0 = "[fetcher] ready";
	$s->write("ready\n");
    }
}

# Send a command for which we don't expect a response
sub send($) {
    my ($child, $msg) = @_;

    die "child busy\n"
	if $$child{'url'};
    $$child{'fh'}->write("$msg\n");
}

# Send a URL and mark the child as busy
sub send_url($) {
    my ($child) = @_;

    die "child busy\n"
	if $$child{'url'};
    return undef
	unless (@TODO);
    my $url = shift(@TODO);
    $$child{'url'} = $url;
    $$child{'uri'} = $URI{$url};
    $$child{'fh'}->write("$url\n");
    ++$BUSY;
}

# Convert relative to absolute and add to blacklist
sub ban_url($$) {
    my ($child, $url) = @_;

    die "child not busy\n"
	unless $$child{'url'};
    my $uri = URI->new_abs($1, $$child{'url'});
    $url = $uri->canonical;
    $BANNED{$url} = 1;
    delete $URI{$url};
    print(STDERR "Banned $url\n")
	unless ($quiet > 2);
}

# Convert relative to absolute, check if valid, and add to list
sub get_url($$) {
    my ($child, $url) = @_;

    die "child not busy\n"
	unless $$child{'url'};
    my $uri = URI->new_abs($1, $$child{'url'});
    $url = $uri->canonical;
    if ($BANNED{$url}) {
	info("Rejected (banned): $url");
	return;
    }
    if ($uri->scheme() ne 'http') {
	info("Rejected (not HTTP): $url");
	return;
    }
    if ($uri->host_port() ne $$child{'uri'}->host_port()) {
	info("Rejected (other server): $url");
    }
    if (index($url, $$child{'url'}) != 0) {
	info("Rejected (upstream): $url");
	return;
    }
    return if $URI{$url};
    $URI{$url} = $uri;
    push(@TODO, $url);
}

# Called when mux gets data from a client
sub mux_input($$$$) {
    my ($child, $mux, $fh, $input) = @_;

    die "unknown child\n"
	unless $child;

    while ($$input =~ s/^(.*?)\n//) {
	my $line = $1;
	if ($line eq "ready") {
	    $$child{'url'} = '';
	    $$child{'uri'} = undef;
	    --$BUSY;
	    ++$DONE;
	    $mux->endloop();
	} elsif ($line =~ m/^add (.*?)$/) {
	    get_url($child, $1);
	} elsif ($line =~ m/^ban (.*?)$/) {
	    ban_url($child, $1);
	} else {
	    die "can't grok [$line]\n";
	}
    }
}

my $mux = new IO::Multiplex;

sub breed() {

    # start children
    $BUSY = 0;
    for (my $i = 0; $i < $jobs; ++$i) {
	my ($s1, $s2);
	socketpair($s1, $s2, AF_UNIX, SOCK_STREAM, PF_UNSPEC);
	$s1->autoflush(1);
	$s2->autoflush(1);
	my $child = __PACKAGE__->new($mux, $s1);
	my $pid = fork();
	last unless defined($pid);
	if ($pid == 0) {
	    close($s1);
	    $child->run($s2);
	    die "not reachable";
	} else {
	    close($s2);
	    $CHILD{$i} = $child;
	    $mux->add($s1);
	    $mux->set_callback_object($child, $s1);
	}
    }
}

sub infanticide() {

    foreach my $child (values(%CHILD)) {
	$child->send("done");
	$$child{'fh'}->close();
    }
}

sub harvest(@) {
    my (@urls) = @_;

    # prepare work queue
    foreach my $url (@urls) {
	my $uri = URI->new($url);
	$url = $uri->canonical;
	$URI{$url} = $uri;
	push(@TODO, $url);
    }

    $DONE = 0;
    for (;;) {
	foreach my $child (values(%CHILD)) {
	    $child->send_url()
		unless $$child{'url'};
	}
	printf(STDERR " %d/%d \r",
	       int(keys(%URI)) - @TODO, int(keys(%URI)))
	    unless ($quiet > 3);
	last unless $BUSY;
	$mux->loop();
    }
}

sub summarize($$$) {
    my ($count, $t0, $t1) = @_;

    my $dt = tv_interval($t0, $t1);
    printf(STDERR "retrieved %d documents in %.2f seconds - %.2f tps\n",
	   $count, $dt, $count / $dt)
	unless ($quiet > 3);
}

sub fetch_random() {

    my $t0 = [gettimeofday()];
    my @urls = keys(%URI);
    @TODO = @urls;
    $DONE = 0;

    while (@TODO) {
	foreach my $child (values(%CHILD)) {
	    $child->send_url()
		unless $$child{'url'};
	    push(@TODO, $urls[rand(@urls)]);
	}
	$mux->loop();
	printf(STDERR " %d \r", $DONE)
	    unless ($quiet > 3);
	if ($DONE > 0 && ($DONE % 1000) == 0) {
	    my $t1 = [gettimeofday()];
	    summarize(1000, $t0, $t1);
	    $t0 = $t1;
	}
    }
}

sub fetch_sequential() {

    my $t0 = [gettimeofday()];
    for (;;) {
	@TODO = keys(%URI);
	$DONE = 0;

	while (@TODO) {
	    foreach my $child (values(%CHILD)) {
		$child->send_url()
		    unless $$child{'url'};
	    }
	    printf(STDERR " %d/%d \r", $DONE, int(keys(%URI)))
		unless ($quiet > 3);
	    last unless $BUSY;
	    $mux->loop();
	}
	my $t1 = [gettimeofday()];
	summarize(int(keys(%URI)), $t0, $t1);
	$t0 = $t1;
    }
}

sub fetcher(@) {
    my (@urls) = @_;

    breed();

    my $t0 = [gettimeofday()];
    harvest(@urls);
    my $t1 = [gettimeofday()];
    summarize(int(keys(%URI)), $t0, $t1);

    foreach my $child (values(%CHILD)) {
	$child->send("no check");
    }

    if ($random) {
	fetch_random();
    } elsif ($continue) {
	fetch_sequential();
    }

    infanticide();
}

sub usage() {

    print(STDERR "usage: $0 [-cqr] [-d n] [-j n] URL ...\n");
    exit(1);
}

MAIN:{
    GetOptions("c|continue!" => \$continue,
	       "d|delay=i" => \$delay,
	       "j|jobs=i" => \$jobs,
	       "q|quiet+" => \$quiet,
	       "r|random!" => \$random,
	       "s|save-files!" => \$save)
	or usage();
    $jobs > 0
	or usage();
    @ARGV
	or usage();
    fetcher(@ARGV);
}
