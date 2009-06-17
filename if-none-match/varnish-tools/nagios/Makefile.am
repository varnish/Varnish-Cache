# $Id$

libexec_PROGRAMS = check_varnish

check_varnish_SOURCES = check_varnish.c

check_varnish_CFLAGS = -include config.h ${VARNISHAPI_CFLAGS}

check_varnish_LDADD = ${VARNISHAPI_LIBS}

EXTRA_DIST = LICENSE autogen.sh
