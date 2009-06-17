#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

require './varnishadm-lib.pl';
use Varnish::CLI;

&ReadParse();
&error_setup("Failed");

my %attrs = ('address' => $config{'address'}, 'port' => $config{'port'},
			'command' => 'vcl.use');

$attrs{'params'} = "$in{'vcl_name'}";

my ($status, $res) = Varnish::CLI::send_command(\%attrs);

if (!($status eq '200')) {
	&error("$status - $res");
}

&redirect("varnish_vcl.cgi");