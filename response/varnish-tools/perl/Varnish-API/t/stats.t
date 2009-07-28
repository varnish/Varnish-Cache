

#!/usr/bin/perl

use strict;
use warnings;


use Test::More tests => 4;
BEGIN { use_ok('Varnish::API') };

use Sys::Hostname qw(hostname);


my $stats = Varnish::API::VSL_OpenStats(hostname);
my $fields = Varnish::API::VSL_GetStatFieldTypes();
is($fields->{n_smf}, 'i');
my $description = Varnish::API::VSL_GetStatFieldDescriptions();
is($description->{n_smf}, 'N struct smf');
like(Varnish::API::VSL_GetStat($stats, "n_smf"), qr /^\d+$/);
