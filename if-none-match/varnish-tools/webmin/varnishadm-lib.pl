#!/usr/local/bin/perl

do '../web-lib.pl';
&init_config();
do '../ui-lib.pl';

if (!$config{'address'}) {
  $config{'address'} = 'localhost';
}
if (!$config{'port'}) {
  $config{'port'} = 23;
}

return 1;
