===========
varnishncsa
===========

---------------------------------------------------------
Display Varnish logs in Apache / NCSA combined log format
---------------------------------------------------------

:Author: Dag-Erling Smørgrav
:Date:   2010-05-31
:Version: 1.0
:Manual section: 1


SYNOPSIS
========

varnishncsa [-a] [-C] [-D] [-d] [-f] [-F format] [-I regex]
[-i tag] [-n varnish_name] [-m tag:regex ...] [-P file] [-r file] [-V] [-w file] 
[-X regex] [-x tag]


DESCRIPTION
===========

The varnishncsa utility reads varnishd(1) shared memory logs and
presents them in the Apache / NCSA "combined" log format.

The following options are available:

-a          When writing to a file, append to it rather than overwrite it.

-C          Ignore case when matching regular expressions.

-D          Daemonize.

-d          Process old log entries on startup.  Normally, varnishncsa 
	    will only process entries which are written to the log 
	    after it starts.

-f          Prefer the X-Forwarded-For HTTP header over client.ip in 
	    the log output.

-F format   Specify the log format used. If no format is specified the  
   	    default log format is used. Currently it is:

            %h %l %u %t "%r" %s %b "%{Referer}i" "%{User-agent}i"

	    Escape sequences \\n and \\t are supported.

	    Supported formatters are:

	      %b 
	         Size of response in bytes, excluding HTTP headers.
   	         In CLF format, i.e. a '-' rather than a 0 when no
   	         bytes are sent.

	      %H 
	         The request protocol. Defaults to HTTP/1.0 if not
                 known.

              %h
	         Remote host. Defaults to '-' if not known.
                 Defaults to 127.0.0.1 for backend requests.

	      %{X}i
	         The contents of request header X.

	      %l
	         Remote logname (always '-')

	      %m
	         Request method. Defaults to '-' if not known.

	      %q
	         The query string, if no query string exists, an
                 empty string.

	      %{X}o
	         The contents of response header X.

	      %r
	         The first line of the request. Synthesized from other
                 fields, so it may not be the request verbatim.

	      %s
	         Status sent to the client

	      %t
	         Time when the request was received, in HTTP date/time
	         format.

	      %{X}t
	         Time when the request was received, in the format
		 specified by X.  The time specification format is the
		 same as for strftime(3).

	      %U
	         The request URL without any query string. Defaults to
                 '-' if not known.

	      %u
	         Remote user from auth

	      %{X}x
	         Extended variables.  Supported variables are:

		   Varnish:time_firstbyte
		     Time to the first byte from the backend arrived

		   Varnish:hitmiss
		     Whether the request was a cache hit or miss. Pipe
		     and pass are considered misses.

		   Varnish:handling
		     How the request was handled, whether it was a
		     cache hit, miss, pass, pipe or error.
	
		   VCL_Log:key
		     Output value set by std.log("key:value") in VCL.
		     

-m tag:regex only list records where tag matches regex. Multiple
            -m options are AND-ed together.

-n          Specifies the name of the varnishd instance to get logs 
	    from.  If -n is not specified, the host name is used.

-P file     Write the process's PID to the specified file.

-r file     Read log entries from file instead of shared memory.

-V          Display the version number and exit.

-w file     Write log entries to file instead of displaying them.  
   	    The file will be overwritten unless the -a
	    option was specified. UDP IPV4 socket in the format of
	    "udp://aaa.bbb.ccc.ddd:port" is allowed.
	    
	    If varnishncsa receives a SIGHUP while writing to a file, 
	    it will reopen the file, allowing the old one to be 
	    rotated away.

-X regex    Exclude log entries which match the specified 
   	    regular expression.

-x tag      Exclude log entries with the specified tag.

If the -o option was specified, a tag and a regex argument must be given.
varnishncsa will then only log for request groups which include that tag
and the regular expression matches on that tag.

SEE ALSO
========

* varnishd(1)
* varnishhist(1)
* varnishlog(1)
* varnishstat(1)
* varnishtop(1)

HISTORY
=======

The varnishncsa utility was developed by Poul-Henning Kamp in
cooperation with Verdens Gang AS and Varnish Software AS.  This manual page was
written by Dag-Erling Smørgrav ⟨des@des.no⟩.


COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2006 Verdens Gang AS
* Copyright (c) 2006-2011 Varnish Software AS
