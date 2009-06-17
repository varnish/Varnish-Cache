#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

require './varnishadm-lib.pl';
use Varnish::CLI;

&ui_print_header(undef, "Purge", "", undef, 1, 1);

&ReadParse();
&error_setup("Purge failed");

my %attrs = ('address' => $config{'address'}, 'port' => $config{'port'},
			'command' => 'url.purge');
if ($in{'regexp'}) {
  $attrs{'params'} = $in{'regexp'};
  my ($status, $res) = Varnish::CLI::send_command(\%attrs);

  if ($status eq '200') {
    print "Purging succesful<br />";
    print "$res<br />";
    print "<hr />";
  }
  else {
    &error("$status - $res");
  }
}

print "<form action=varnish_purge.cgi>\n";
print "What to purge (regexp): <input name=regexp size=10 /><br />\n";
print "<input type=submit value=Purge /><br />\n";
print "</form>";

&ui_print_footer("", 'module index');
