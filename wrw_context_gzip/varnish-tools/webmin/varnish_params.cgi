#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

require './varnishadm-lib.pl';
use Varnish::CLI;

&ui_print_header(undef, "Params", "", undef, 1, 1);
&ReadParse();
&error_setup("Failed");

my %attrs = ('address' => $config{'address'}, 'port' => $config{'port'},
			'command' => 'param.show');
my ($status, $res) = Varnish::CLI::send_command(\%attrs);

if (!($status eq '200')) {
	&error("$status - $res");
}

my @params = ();
foreach my $line (split(/\n/, $res)) {
	if ($line =~ /(\w+)\s+([a-zA-z0-9.:]+)(\s+(\[|\()(\w+|%)(\]|\)))?/) {
		push @params, {'name' => $1, 'value' => $2, 'unit' => $5};
	}
}
if ($in{'save'}) {
	foreach $param (@params) {
		if ($in{$param->{'name'}} && !($in{$param->{'name'}} eq $param->{'value'})) {
			$attrs{'command'} = 'param.set';
			$attrs{'params'} = "$param->{'name'} $in{$param->{'name'}}";
			$status, $res = Varnish::CLI::send_command(\%attrs);
			if (!($status eq '200')) {
				&error("$status - $res");
			}
			$param->{'value'} = $in{$param->{'name'}};
		}
	}
}

my $cmd_num = 0;
print "<form action=varnish_params.cgi>";
print "<table border width=100%>\n";
print "<tr $tb> <td><b>params</b></td> </tr>\n";
print "<tr $cb> <td><table width=100%>\n";
foreach $param (@params) {
	if (($cmd_num % 2) == 0) {
		print "<tr>\n";
	}
	print "<td>$param->{'name'}</td><td nowrap>";
	if ($param->{'unit'} eq 'bool') {
		print "<input type=radio name=$param->{'name'} value=on " . 
			(($param->{'value'} eq 'on') ? "checked" : "") . " /> On\n";
		print "<input type=radio name=$param->{'name'} value=off " . 
			(($param->{'value'} eq 'off') ? "checked" : "") . " /> Off\n";
		 
	}
	else {
		print "<input name=$param->{'name'} value=$param->{'value'} size=25 /> $param->{'unit'}";
	}
	print "</td>\n";
	if (($cmd_num % 2) != 0) {
		print "</tr>\n";
	}
	$cmd_num++;
}
print "</tr></table>";
print "</table>";
print "<input type=submit name=save value=save />";
print "</form>";

&ui_print_footer("", 'module index');
