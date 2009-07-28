#!/usr/local/bin/perl

# Author: Cecilie Fritzvold <cecilihf@linpro.no>

package Varnish::CLI;

use IO::Socket;

# Send a command to varnish over telnet.
# This function takes a hash as parameter.
# Valid keys for the hash are:
# * address : Address of varnish
# * port: port where varnish listens for telnet connections
# * command : the command to send
# * params : parameters to the command as a string. (optional)
# The function returns a tuple with the return code and the result.
# If missing or invalid parameters, 0 is returned.
sub send_command {
	my ($args) = shift;
	my $sock;
	my $line;
	my $status, $size, $read;
	my $response = "";
	
	if (!$args->{'address'} || !$args->{'port'} || !$args->{'command'}) {
		return 0;
	}
	
	$sock = IO::Socket::INET->new(
		Proto    => "tcp",
		PeerAddr => $args->{'address'},
		PeerPort => $args->{'port'},
	) or return 0;
	
	if ($args->{'params'}) {
		$args->{'command'} .= " $args->{'params'}";
	}
	print $sock "$args->{'command'}\r\n";
	
	if (!($line = <$sock>)) {
		return 0;
	}
	$line =~ /^(\d+) (\d+)/;
	$status = $1;
	$size = $2;
	
	while ($line = <$sock>) {
		$response .= $line;
		$read += length($line);
		if ($read >= $size) {
			last;
		}
	}
	
	close $sock;
	
	return ($status, $response);
}

return 1;