===========
varnishtest
===========

------------------------
Test program for Varnish
------------------------

:Author: Stig Sandbeck Mathisen
:Author: Kristian Lyngstøl
:Date:   2011-11-15
:Version: 1.2
:Manual section: 1


SYNOPSIS
========

     varnishtest [-iklLqv] [-n iter] [-D name=val] [-j jobs] [-t duration] file [file ...]

DESCRIPTION
===========

The varnishtest program is a script driven program used to test the
Varnish Cache.

The varnishtest program, when started and given one or more script
files, can create a number of threads representing backends, some
threads representing clients, and a varnishd process. This is then used to
simulate a transaction to provoke a specific behavior.

The following options are available:

-D name=val      Define macro for use in scripts

-i               Find varnishd in build tree

-j jobs          Run this many tests in parallel

-k               Continue on test failure

-l               Leave temporary vtc.* if test fails

-L               Always leave temporary vtc.*

-n iterations    Run tests this many times

-q               Quiet mode: report only failures

-t duration      Time tests out after this long

-v               Verbose mode: always report test log

-h               Show help

file             File to use as a script


Macro definitions that can be overridden.

varnishd         Path to varnishd to use [varnishd]

If `TMPDIR` is set in the environment, varnishtest creates temporary
`vtc.*` directories for each test in `$TMPDIR`, otherwise in `/tmp`.

SCRIPTS
=======

The script language used for Varnishtest is not a strictly defined
language. The best reference for writing scripts is the varnishtest program
itself. In the Varnish source code repository, under
`bin/varnishtest/tests/`, all the regression tests for Varnish are kept.

An example::

        varnishtest "#1029"

        server s1 {
                rxreq
                expect req.url == "/bar"
                txresp -gzipbody {[bar]}

                rxreq
                expect req.url == "/foo"
                txresp -body {<h1>FOO<esi:include src="/bar"/>BARF</h1>}

        } -start

        varnish v1 -vcl+backend {
                sub vcl_fetch {
                        set beresp.do_esi = true;
                        if (req.url == "/foo") {
                                set beresp.ttl = 0s;
                        } else {
                                set beresp.ttl = 10m;
                        }
                }
        } -start

        client c1 {
                txreq -url "/bar" -hdr "Accept-Encoding: gzip"
                rxresp
                gunzip
                expect resp.bodylen == 5

                txreq -url "/foo" -hdr "Accept-Encoding: gzip"
                rxresp
                expect resp.bodylen == 21
        } -run

When run, the above script will simulate a server (s1) that expects two
different requests. It will start a Varnish server (v1) and add the backend
definition to the VCL specified (-vcl+backend). Finally it starts the
c1-client, which is a single client sending two requests.

AVAILABLE COMMANDS
==================

**server**

Creates mock of a server that can accept requests from Varnish and send responses. Accepted parameters:
 
-wait
 (?)
-repeat
 (?)
-listen
 specifies address and port to listen on (e.g. "127.0.0.1:80")
-start
 starts the server

**client**

Creates a client instance that sends requests to Varnish and receives responses. Accepted parameters:

-wait
 waits for commands to complete
-connect
 specify where to connect to (e.g. "-connect ${s1_sock}").
-repeat
 (?)
-start
 start the client, and continue without waiting for completion
-run
 equivalent to -start then -wait
 
**varnish**

Starts Varnish instance. Accepted arguments:

-arg
 pass additional arguments to varnishd
-cli
 execute a command in CLI of running instance
-cliok
 (?) execute a command and expect it return OK status
-clierr
 (?) execute a command and expect it to error with given status (e.g. "-clierr 300 panic.clear")
-vcl+backend
 specify VCL for the instance, and automatically inject a backend into the VCL
-errvcl
 (?) tests that invalid VCL results in an error. Replaces -badvcl.
-vcl
 specify VCL for the instance
-stop
 stop the instance
-wait-stopped
 (?) wait for the server to stop?
-wait-running
 (?) wait for the server to start?
-wait
 (?)
-expect
 set up a test for asserting variables against expected results. Syntax: "-expect <var> <comparison> <const>"
 
See tests supplied with Varnish distribution for usage examples for all these directives.

**delay**

Sleeps for specified number of seconds. Can accept floating point numbers. 
 
Usage: ``delay 0.1``

**varnishtest**

Accepts a string as an only argument. This being a test name that is being output 
into the log. By default, test name is not shown, unless it fails.

**shell**

Executes a shell command. Accepts one argument as a string, and runs the command as is. 
 
Usage: ``shell "date"``

**sema**

(todo)
 
Semaphores mostly used to synchronize clients and servers "around"
varnish, so that the server will not send something particular
until the client tells it to, but it can also be used synchronize
multiple clients or servers running in parallel.

**random**

Initializes random generator

**feature**

Checks for features to be present in the test environment. If feature is not present, test is skipped. 
 
Usage: ``feature 64bit SO_RCVTIMEO_WORKS``

Possible checks:

SO_RCVTIMEO_WORKS
 runs the test only if SO_RCVTIMEO option works in the environment
64bit
 runs the test only if environment is 64 bit
!OSX
 skips the test if ran on OSX
topbuild
 (?)
logexpect
 This allows checking order and contents of VSL records in varnishtest.

SEE ALSO
========

* varnishtest source code repository with tests
* varnishhist(1)
* varnishlog(1)
* varnishncsa(1)
* varnishstat(1)
* varnishtop(1)
* vcl(7)

HISTORY
=======

The varnishtest program was developed by Poul-Henning Kamp
<phk@phk.freebsd.dk> in cooperation with Varnish Software AS.
This manual page was originally written by Stig Sandbeck Mathisen
<ssm@linpro.no> and updated by Kristian Lyngstøl
<kristian@varnish-cache.org>.

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2007-2014 Varnish Software AS
