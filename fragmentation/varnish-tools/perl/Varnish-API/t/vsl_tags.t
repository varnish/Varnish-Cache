
use Test::More tests => 2;
BEGIN { use_ok('Varnish::API') };
use Devel::Peek;



my $foo = Varnish::API::VSL_tags(45);
is($foo, "Hit");
