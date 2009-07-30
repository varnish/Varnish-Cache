package Varnish::API;

use 5.008008;
use strict;
use warnings;
use Carp;

require Exporter;
use AutoLoader;

our @ISA = qw(Exporter);

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.

# This allows declaration	use Varnish::API ':all';
# If you do not need this, moving things directly into @EXPORT or @EXPORT_OK
# will save memory.
our %EXPORT_TAGS = ( 'all' => [ qw(
	VSL_S_BACKEND
	VSL_S_CLIENT
	V_DEAD
	VSL_Arg
	VSL_Dispatch
	VSL_Name
	VSL_New
	VSL_NextLog
	VSL_NonBlocking
	VSL_OpenLog
	VSL_OpenStats
	VSL_Select
	asctime
	asctime_r
	base64_decode
	base64_init
	clock
	clock_getcpuclockid
	clock_getres
	clock_gettime
	clock_nanosleep
	clock_settime
	ctime
	ctime_r
	difftime
	dysize
	getdate
	getdate_r
	gmtime
	gmtime_r
	localtime
	localtime_r
	mktime
	nanosleep
	stime
	strftime
	strftime_l
	strptime
	strptime_l
	time
	timegm
	timelocal
	timer_create
	timer_delete
	timer_getoverrun
	timer_gettime
	timer_settime
	tzset
	varnish_instance
) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
	VSL_S_BACKEND
	VSL_S_CLIENT
	V_DEAD
);

our $VERSION = '1.99';

sub AUTOLOAD {
    # This AUTOLOAD is used to 'autoload' constants from the constant()
    # XS function.

    my $constname;
    our $AUTOLOAD;
    ($constname = $AUTOLOAD) =~ s/.*:://;
    croak "&Varnish::API::constant not defined" if $constname eq 'constant';
    my ($error, $val) = constant($constname);
    if ($error) { croak $error; }
    {
	no strict 'refs';
	# Fixed between 5.005_53 and 5.005_61
#XXX	if ($] >= 5.00561) {
#XXX	    *$AUTOLOAD = sub () { $val };
#XXX	}
#XXX	else {
	    *$AUTOLOAD = sub { $val };
#XXX	}
    }
    goto &$AUTOLOAD;
}

require XSLoader;
XSLoader::load('Varnish::API', $VERSION);

# Preloaded methods go here.

# Autoload methods go after =cut, and are processed by the autosplit program.

1;
__END__
# Below is stub documentation for your module. You'd better edit it!

=head1 NAME

Varnish::API - Perl extension for accessing varnish stats and logs

=head1 SYNOPSIS

  use Varnish::API;
  use Sys::Hostname qw(hostname);

  my $vd = Varnish::API::VSL_New();
  Varnish::API::VSL_OpenLog($vd, hostname);

  Varnish::API::VSL_Dispatch($vd, 
    sub { my ($tag, $id, $spec, $text) = @_; return 1 });

  my $log = Varnish::API::VSL_NextLog($vd);
  my $tag = Varnish::API::VSL_tags(Varnish::API::SHMLOG_TAG($log));
  my $fd  = Varnish::API::SHMLOG_ID($log);
  my $text = Varnish::API::SHMLOG_DATA($log);

  my $stats = Varnish::API::VSL_OpenStats(hostname);
  my $fields = Varnish::API::VSL_GetStatFieldTypes();
  my $description = Varnish::API::VSL_GetStatFieldDescriptions();
  my $client_conn = Varnish::API::VSL_GetStat($stats, "client_conn");


=head1 DESCRIPTION

This module allows access to the data that varnishlog and varnishstats can read.

=head2 EXPORT

None by default.

=head1 SEE ALSO

=head1 AUTHOR

Artur Bergman E<lt>sky+cpan@crucially.netE<gt>

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2009 by Artur Bergman

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself, either Perl version 5.8.8 or,
at your option, any later version of Perl 5 you may have available.


=cut
