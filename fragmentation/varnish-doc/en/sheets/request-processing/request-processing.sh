#!/bin/sh
#
# This script generates a sheet with the Varnish request processing
# state-graph.
#
# $Id$

echo '
linethick=1.5
boxht=.38
boxwid=1.1
dist=.23i
down
#######################################################################
# The hit path

define style_hit { outline "hit_color" }

A: box "request received" 
		arrow down dist style_hit
C: box "\f(CBvcl_recv{}\fP" "\f(PIreq\fP"
		arrow "  \f(CIlookup\fP" ljust down dist style_hit
D: box "\f(CBvcl_hash{}\fP" "\f(PIreq\fP"
		arrow "  \f(CIhash\fP" ljust down dist style_hit
E: box "obj in cache ?"
		arrow "  [yes]" ljust down dist style_hit
F: box "obj.pass ?"
		arrow "  [no]" ljust down dist style_hit
G: box "\f(CBvcl_hit{}\fP" "\f(PIreq obj\fP"
		line invis down dist
H:		box invis
		line invis down dist
I:		box invis
		line invis down dist
J:		box invis
		line invis down dist
K:		box invis
		line invis down dist
L:		box invis
		line invis down dist
M: box "filter" "obj\[->]resp"
		arrow down dist style_hit
N: box "\f(CBvcl_deliver{}\fP" "\f(PIresp\fP"
		arrow "  \f(CIdeliver\fP" ljust down dist style_hit
O: box "send resp+body"
		arrow down dist style_hit
P: box "request" "completed"
		arrow "  \f(CIdeliver\fP" ljust from G.s to M.n style_hit

A0: circle invis rad .1 with .c at A.w - (.7,0)
A2: box invis with .w at A.e + (.5,0)
A3: box invis with .w at A2.e + (.5,0)
A4: circle invis rad .1 with .c at A3.e + (.5,0)
A5: box invis with .w at A3.e + (1.0,0)

define style_sleep { outline "sleep_color" }

T2: box "sleep on busy" with .c at (A2.c, E.c)
T1: spline from E.ne to 1/2 <E.ne,T2.n> + (0, .5) then to T2.n -> style_sleep
arrow from T2.w to E.e style_sleep

' > /tmp/_hit

echo '
#######################################################################
# The miss path

define style_miss { outline "miss_color" }

G2: box "filter" "req\[->]bereq" with .n at (A2.n, G.n)
	line from E.se down .2 right .2 style_miss
	line "[no]" above to (G2.n,last line .s) style_miss
	arrow to G2.n style_miss

H2: box "\f(CBvcl_miss{}\fP" "\f(PIreq bereq\fP" with .n at (A2.n, H.n)
	arrow from G2.s to H2.n style_miss

J2: box "fetch bereq" "from backend" with .n at (A2.n, J.n)
	line "  \f(CIfetch\fP" ljust from H2.s down .3 style_miss
	arrow to J2.n style_miss
K2: box "fetch successful ?" with .n at (A2.n, K.n)
	arrow ljust from J2.s to K2.n style_miss
L2: box "\f(CBvcl_fetch{}\fP" "\f(PIreq bereq obj\fP" with .n at (A2.n, L.n)
	arrow "  [yes]" ljust from K2.s to L2.n style_miss
	line "  \f(CIdeliver\fP" ljust from L2.s to (L2.s, M.e) style_miss
	arrow to M.e style_miss
' > /tmp/_miss

echo '
#######################################################################
# The pass path

define style_pass { outline "pass_color" }

H3: box "filter" "req\[->]bereq" with .w at (A3.w,H.w)
	line from C.se down .2 right .2 style_pass
	arrow "\f(CIpass\fP" below to (H3.n, last line .s) style_pass
	arrow to H3.n style_pass
	arrow "[yes]" above from F.e to (H3.n, F.e) style_pass
	line up .2 right .2 from G.ne style_pass
	arrow "\f(CIpass\fP" below to (H3.n,last line.e) style_pass
	arrow "\f(CIpass\fP" above from H2.e to H3.w style_pass

I3: box "\f(CBvcl_pass{}\fP" "\f(PIreq bereq\fP" with .n at (A3.n, I.n)
	arrow from H3.s to I3.n style_pass
	
J3: box "create anon obj" with .n at (A3.n, J.n)
	arrow "  \f(CIpass\fP" ljust from I3.s to J3.n style_pass
	arrow from J3.w to J2.e style_pass

M3: box "obj.pass = 1" with .n at (A3.n, M.n)
	line "\f(CIpass\fP" below from L2.e to (M3.n,L2.e) style_pass
	arrow to M3.n style_pass
	line from M3.s to (M3.s, M.s) - (0,.2) style_pass
	line to M.se + (.2,-.2) style_pass
	arrow to M.se style_pass

' > /tmp/_pass

echo '
#######################################################################
# The pipe path

define style_pipe { outline "pipe_color" }

C5:	box "filter" "req\[->]bereq" with .n at (A5.n, C.n)
D5:	box "\f(CBvcl_pipe{}\fP" "\f(PIreq bereq\fP" with .n at (A5.n, D.n)
E5:	box "send bereq" "to backend" with .n at (A5.n, E.n)
F5:	box "move bytes" "client\[<>]backend" with .n at (A5.n, F.n)
	line from C.ne up .2 right .2 style_pipe
	line "\f(CIpipe\fP" above to C5.nw + (-.2, .2) style_pipe
	arrow to C5.nw style_pipe
	arrow from C5.s to D5.n style_pipe
	arrow "  \f(CIpipe\fP" ljust from D5.s to E5.n style_pipe
	arrow from E5.s to F5.n style_pipe
	line from F5.s to (F5.n,P.e) style_pipe
	arrow to P.e style_pipe

' > /tmp/_pipe

echo '	
#######################################################################
# The error path

define style_error { outline "error_color" }

N4: box "\f(CBvcl_error{}\fP" "\f(PIresp\fP" with .n at (A4.n, N.n)
	arrow "\f(CIdeliver\fP" below from N4.w to N.e style_error

	line from C.e to (A4.w,C.e) style_error
	arrow "\f(CIerror\fP" rjust above to (A4.n,last line.s) style_error
	arrow to N4.n style_error

	line from G.se down .1 right .1 style_error
	line to (A4.w,last line.s) style_error
	arrow "\f(CIerror\fP" rjust above to (A4.n,last line.s) style_error

	line from H2.se down .1 right .1 style_error
	line to (A4.w,last line.s) style_error
	arrow "\f(CIerror\fP" rjust above to (A4.n,last line.s) style_error

	arrow "\f(CIerror\fP" above from I3.e to (A4.n,I3.e) style_error

	line from K2.e to (A4.w,K2.e) style_error
	arrow "[no]" rjust above to (A4.n,last line.s) style_error

	line from L2.ne up .1 right .1 style_error
	line to (A4.w,last line.e) style_error
	arrow "\f(CIerror\fP" rjust above to (A4.n,last line.s) style_error

	arrow "\f(CIerror\fP" above from D5.w to (A4.n,D5.w) style_error
' > /tmp/_error

echo '
#######################################################################
# The restart path

define style_restart { outline "restart_color" }

	arrow "\f(CIrestart\fP" above from G.w to (A0.c, G.w) style_restart

	line left from H2.w to H.w style_restart
	arrow "\f(CIrestart\fP" above to (A0.c, H2.w) style_restart

	line left from I3.w to I.w style_restart
	arrow "\f(CIrestart\fP" above to (A0.c, I3.w) style_restart

	line left from L2.w to L.w style_restart
	arrow "\f(CIrestart\fP" above to (A0.c, L2.w) style_restart

	arrow "\f(CIrestart\fP" above from N.w to (A0.c, N.w) style_restart
	line to (A0.c, C.w) style_restart
	arrow to C.w style_restart

' > /tmp/_restart

(

rm -f /tmp/_

for i in hit miss pass pipe error restart
do
	cat /tmp/_$i >> /tmp/_
	echo '
.sp .1i
.mk
.PSPIC -L ../../../../varnish-logo/logo/varnish-logo.eps 3i
.rt
.sp .52i
.ft PB
.ps 30
.ll 8i
.ti 3.2i
Request processing
.ft PR
.ps 10
.sp .2i
'
	echo ".defcolor hit_color     rgb #009900"
	echo ".defcolor miss_color    rgb #0000cc"
	echo ".defcolor pass_color    rgb #cc0000"
	echo ".defcolor pipe_color    rgb #ff6633"
	echo ".defcolor error_color   rgb #666666"
	echo ".defcolor restart_color rgb #990099"
	echo ".defcolor sleep_color   rgb #cccc00"
	echo .PS
	cat /tmp/_
	echo .PE
	if [ $i != "restart" ] ; then
		echo .bp
	fi
done
) | pic | groff -o6 | tee /tmp/_2.ps |ps2pdf - /tmp/_.pdf

