#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

require './varnishadm-lib.pl';

&ui_print_header(undef, $module_info{'name'}, "", undef, 1, 1);

@olinks = ("varnish_vcl.cgi", "varnish_params.cgi", "varnish_status.cgi", "varnish_purge.cgi");
@otitles = ("VCL", "Params", "Start/Stop", "Purge");
@oicons = ("images/vcl_icon.gif", "images/params_icon.gif", "images/status_icon.gif", "images/purge_icon.gif");

&icons_table(\@olinks, \@otitles, \@oicons);
