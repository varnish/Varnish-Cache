#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

require './varnishadm-lib.pl';
use Varnish::CLI;

&ui_print_header(undef, "VCL Admin", "", undef, 1, 1);
&error_setup("Failed");

my %attrs = ('address' => $config{'address'}, 'port' => $config{'port'},
			'command' => 'vcl.list');
my ($status, $res) = Varnish::CLI::send_command(\%attrs);

if (!($status eq '200')) {
	&error("$status - $res");
}

print "<table border width=100%>";
print "<tr $tb> <td><b>Available VCL scripts</b></td> </tr>\n";
print "<tr $cb><td>";
print "<form action=varnish_vcl_use.cgi method=post>";
my $confname = "";
my $set = 0;
foreach my $line (split(/\n/, $res)) {
  if ($line =~ /\*?\s+\d\s+(\w+)/) {
    $confname = $1;
    if ($line =~ /^\*/) {
      $set = 1;
    }
  }
  print "<input type=radio name=vcl_name value=$confname" .
    ($set ? " checked" : "") . " />\n";
  print "<a href=varnish_vcl_view.cgi?vcl_file=$confname>$confname</a>";
  print "<br />\n";
  $set = 0;
}

print "<input type=submit name=use value='Switch to selected' />\n";
print "</form>";

print "</td></tr>";
print "<tr $tb> <td><b>Upload new VCL file</b></td> </tr>\n";
print "<tr $cb><td><table width=100%>";
print "<form action=varnish_vcl_upload.cgi enctype=multipart/form-data method=post>";
print "<tr><td>";
print "Config name: <input name=vcl_name size=25 /> ";
print "</td></tr><tr><td>";
print "VCL file: ";
print &ui_upload("vcl_file", 25);
print "</td></tr><tr><td>";
print "<input type=submit name=upload value='Upload file'>\n";
print "</td></tr>";
print "</table>";
print "</form>";

print "</td></tr>";
print "</table>";

&ui_print_footer("", 'module index');
