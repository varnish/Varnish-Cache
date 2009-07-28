#!/usr/bin/perl

use strict;
use warnings;


use Test::More tests => 6;
BEGIN { use_ok('Varnish::API') };
use Devel::Peek;

use Sys::Hostname qw(hostname);

my $vd = Varnish::API::VSL_New();
Varnish::API::VSL_OpenLog($vd, hostname);

my $log = Varnish::API::VSL_NextLog($vd);

$log =~/^(.)..(..)/;

is(Varnish::API::SHMLOG_TAG($log), ord($1));

my $tag = Varnish::API::VSL_tags(Varnish::API::SHMLOG_TAG($log));
my $fd  = Varnish::API::SHMLOG_ID($log);
my $text = Varnish::API::SHMLOG_DATA($log);

like($fd, qr/^\d+$/);
ok(1, "$tag");
ok(1, "$text");
use bytes;
is(Varnish::API::SHMLOG_LEN($log), length($text));

