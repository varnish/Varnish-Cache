#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

require './varnishadm-lib.pl';
use Varnish::CLI;

&ui_print_header(undef, "Status", "", undef, 1, 1);
&error_setup("Could not get status");

my %attrs = ('address' => $config{'address'}, 'port' => $config{'port'},
			'command' => 'ping');
my ($status, $res) = Varnish::CLI::send_command(\%attrs);
if ($status eq '200') {
	if ($res =~ /^PONG/) {
		print "Varnish is up<br />";
	}
	else {
		print "Varnish is down<br />";
	}
}
else {
	&error("$status - $res");
}	

$attrs{'command'} = 'status';
$status, $res = Varnish::CLI::send_command(\%attrs);
if ($status eq '200') {
	print "$res<br />";
}
else {
	&error("$status - $res");
}	

print "<hr>\n";
print &ui_buttons_start();

print &ui_buttons_row("varnish_stop.cgi","Stop varnish child");
print &ui_buttons_row("varnish_start.cgi", "Start varnish child");
	
print &ui_buttons_end();

&ui_print_footer("", 'module index');
