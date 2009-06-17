#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

require './varnishadm-lib.pl';
use Varnish::CLI;

&ReadParse();
&error_setup("Failed");

&ui_print_header(undef, "vcl file: $in{'vcl_file'}", "", undef, 1, 1);

my %attrs = ('address' => $config{'address'}, 'port' => $config{'port'},
			'command' => 'vcl.show');

$attrs{'params'} = "$in{'vcl_file'}";

my ($status, $res) = Varnish::CLI::send_command(\%attrs);

if (!($status eq '200')) {
	&error("$status - $res");
}

$res =~ s/ /&nbsp;/g;
$res =~ s/\n/<br \/>/g;

print $res;

&ui_print_footer("varnish_vcl.cgi", 'vcl index');
