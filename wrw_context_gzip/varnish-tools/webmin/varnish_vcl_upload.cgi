#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

require './varnishadm-lib.pl';
use Varnish::CLI;
use File::Temp qw (mkstemp);
use Fcntl qw (F_SETFD F_GETFD :mode);

&ReadParseMime();
&error_setup("Failed");

my %attrs = ('address' => $config{'address'}, 'port' => $config{'port'},
			'command' => 'vcl.load');

my ($fh, $file) = mkstemp("/tmp/tmp.vcl.XXXXXXX");
print $fh $in{'vcl_file'};
close($fh);

chmod S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, $file;

$attrs{'params'} = "$in{'vcl_name'} $file";

my ($status, $res) = Varnish::CLI::send_command(\%attrs);

if (!($status eq '200')) {
	&error("$status - $res");
}

&redirect("varnish_vcl.cgi");
