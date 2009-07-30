#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

require './varnishadm-lib.pl';
use Varnish::CLI;

&ReadParse();
&error_setup("Stop command failed: ");

my %attrs = ('address' => $config{'address'}, 'port' => $config{'port'},
			'command' => 'stop');
my ($status, $res) = Varnish::CLI::send_command(\%attrs);
if (!($status eq 200)) {
  &error("$status - $res");
}
&webmin_log("stop");
&redirect("varnish_status.cgi");

