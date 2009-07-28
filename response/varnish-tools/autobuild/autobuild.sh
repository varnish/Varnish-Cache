#!/bin/sh -e

# Copyright (c) 2007-2009 Linpro AS
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer
#    in this position and unchanged.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $Id$
#

# Change to same directory as this script is located.
cd "`dirname "$0"`"

if [ "$1" != "" ]; then
    # Make and change to working directory.
    DIR="`date -u +varnish-%Y%m%d-%H%M%SZ`"
    mkdir "$DIR"
    cd "$DIR"

    # Check out from Subversion.
    svn co -r "$1" http://varnish.projects.linpro.no/svn/trunk > svn.log 2>&1

    # Use most recent revision of regression test framework.
    svn update trunk/varnish-tools/regress >> svn.log 2>&1

    # Make status file.
    echo "revision: $1" > regress.status
else
    # What is the current revision?
    CURR="`svn info http://varnish.projects.linpro.no/svn/trunk | grep 'Revision: ' | sed 's/Revision: //'`"

    # Compare to last tested revision and exit if we already tested
    # this revision.
    if [ -e LAST_TESTED_REVISION ]; then
	LAST="`cat LAST_TESTED_REVISION`"
	if [ "$CURR" = "$LAST" ]; then
	    exit 0;
	fi
    fi

    # Okay, new revision. Here we go...

    # Make and change to working directory.
    DIR="`date -u +varnish-%Y%m%d-%H%M%SZ`"
    mkdir "$DIR"
    cd "$DIR"

    # Check out from Subversion.
    svn co -r "$CURR" http://varnish.projects.linpro.no/svn/trunk > svn.log 2>&1

    # Make status file.
    echo "revision: $CURR" > regress.status

    # Update last revision status file.
    echo "$CURR" > ../LAST_TESTED_REVISION
fi

# Build.
(
    cd trunk/varnish-cache
    ./autogen.sh
    ./configure --enable-debugging-symbols --enable-developer-warnings --enable-dependency-tracking --prefix=/tmp/"$DIR"
    make
    make install
) > build.log 2>&1

# Run regression test framework.
PATH=/tmp/"$DIR"/sbin:"$PATH" trunk/varnish-tools/regress/bin/varnish-regress.pl > regress.html 2> regress.log

# Update status file.
grep -A 4 '<th class="name">Total</th>' regress.html \
    | sed 's/.*class="\([^"]*\)">\([^<]*\)<.*/\1: \2/' \
    | tail -n +2 >> regress.status

cd ..

# Make summary file.
./mksummary.pl varnish-*Z > index.html.new
mv -f index.html.new index.html

exit 0
