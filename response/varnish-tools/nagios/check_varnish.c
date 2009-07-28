/*-
 * Copyright (c) 2007-2009 Linpro AS
 * All rights reserved.
 *
 * Author: Cecilie Fritzvold <cecilihf@linpro.no>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 *
 * Nagios plugin for Varnish
 */

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include "shmlog.h"
#include "varnishapi.h"

static int verbose = 0;

struct range {
	intmax_t	lo;
	intmax_t	hi;
	int		inverted:1;
	int		defined:1;
};

static struct range critical;
static struct range warning;

enum {
	NAGIOS_OK = 0,
	NAGIOS_WARNING = 1,
	NAGIOS_CRITICAL = 2,
	NAGIOS_UNKNOWN = 3,
};

static const char *status_text[] = {
	[NAGIOS_OK] = "OK",
	[NAGIOS_WARNING] = "WARNING",
	[NAGIOS_CRITICAL] = "CRITICAL",
	[NAGIOS_UNKNOWN] = "UNKNOWN",
};

/*
 * Parse a range specification
 */
static int
parse_range(const char *spec, struct range *range)
{
	const char *delim;
	char *end;

	/* @ means invert the range */
	if (*spec == '@') {
		++spec;
		range->inverted = 1;
	} else {
		range->inverted = 0;
	}

	/* empty spec... */
	if (*spec == '\0')
		return (-1);

	if ((delim = strchr(spec, ':')) != NULL) {
		/*
		 * The Nagios plugin documentation says nothing about how
		 * to interpret ":N", so we disallow it.  Allowed forms
		 * are "~:N", "~:", "M:" and "M:N".
		 */
		if (delim - spec == 1 && *spec == '~') {
			range->lo = INTMAX_MIN;
		} else {
			range->lo = strtoimax(spec, &end, 10);
			if (end != delim)
				return (-1);
		}
		if (*(delim + 1) != '\0') {
			range->hi = strtoimax(delim + 1, &end, 10);
			if (*end != '\0')
				return (-1);
		} else {
			range->hi = INTMAX_MAX;
		}
	} else {
		/*
		 * Allowed forms are N
		 */
		range->lo = 0;
		range->hi = strtol(spec, &end, 10);
		if (*end != '\0')
			return (-1);
	}

	/*
	 * Sanity
	 */
	if (range->lo > range->hi)
		return (-1);

	range->defined = 1;
	return (0);
}

/*
 * Check if a given value is within a given range.
 */
static int
inside_range(intmax_t value, const struct range *range)
{

	if (range->inverted)
		return (value < range->lo || value > range->hi);
	return (value >= range->lo && value <= range->hi);
}

/*
 * Check if the thresholds against the value and return the appropriate
 * status code.
 */
static int
check_thresholds(intmax_t value)
{

	if (!warning.defined && !critical.defined)
		return (NAGIOS_UNKNOWN);
	if (critical.defined && !inside_range(value, &critical))
		return (NAGIOS_CRITICAL);
	if (warning.defined && !inside_range(value, &warning))
		return (NAGIOS_WARNING);
	return (NAGIOS_OK);
}

/*
 * Check the statistics for the requested parameter.
 */
static void
check_stats(struct varnish_stats *VSL_stats, char *param)
{
	const char *info;
	struct timeval tv;
	double up;
	intmax_t value;
	int status;

	gettimeofday(&tv, NULL);
	up = tv.tv_sec - VSL_stats->start_time;
	if (strcmp(param, "uptime") == 0) {
		value = up;
		info = "Uptime";
	}
	else if (strcmp(param, "ratio") == 0) {
		intmax_t total = VSL_stats->cache_hit + VSL_stats->cache_miss;

		value = total ? (100 * VSL_stats->cache_hit / total) : 0;
		info = "Cache hit ratio";
	}
	else if (strcmp(param, "usage") == 0) {
		intmax_t total = VSL_stats->sm_balloc + VSL_stats->sm_bfree;

		value = total ? (100 * VSL_stats->sm_balloc / total) : 0;
		info = "Cache file usage";
	}
#define MAC_STAT(n, t, f, i, d)		   \
	else if (strcmp(param, #n) == 0) { \
		value = VSL_stats->n; \
		info = d; \
	}
#include "stat_field.h"
#undef MAC_STAT
	else
		printf("Unknown parameter '%s'\n", param);

	status = check_thresholds(value);
	printf("VARNISH %s: %s|%s=%jd\n", status_text[status], info, param, value);
	exit(status);
}

/*-------------------------------------------------------------------------------*/

static void
help(void)
{

	fprintf(stderr, "usage: "
	    "check_varnish [-lv] [-n varnish_name] [-p param_name [-c N] [-w N]]\n"
	    "\n"
	    "-v              Increase verbosity.\n"
	    "-n varnish_name Specify the Varnish instance name\n"
	    "-p param_name   Specify the parameter to check (see below).\n"
	    "                The default is 'ratio'.\n"
	    "-c [@][lo:]hi   Set critical threshold\n"
	    "-w [@][lo:]hi   Set warning threshold\n"
	    "\n"
	    "All items reported by varnishstat(1) are available - use the\n"
	    "identifier listed in the left column by 'varnishstat -l'.  In\n"
	    "addition, the following parameters are available:\n"
	    "\n"
	    "uptime  How long the cache has been running (in seconds)\n"
	    "ratio   The cache hit ratio expressed as a percentage of hits to\n"
	    "        hits + misses.  Default thresholds are 95 and 90.\n"
	    "usage   Cache file usage as a percentage of the total cache space.\n"
	);
	exit(0);
}

static void
usage(void)
{

	fprintf(stderr, "usage: "
	    "check_varnish [-lv] [-n varnish_name] [-p param_name [-c N] [-w N]]\n");
	exit(3);
}


int
main(int argc, char **argv)
{
	struct varnish_stats *VSL_stats;
	const char *n_arg = NULL;
	char *param = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "c:hn:p:vw:")) != -1) {
		switch (opt) {
		case 'c':
			if (parse_range(optarg, &critical) != 0)
				usage();
			break;
		case 'h':
			help();
			break;
		case 'n':
			n_arg = optarg;
			break;
		case 'p':
			param = strdup(optarg);
			break;
		case 'v':
			++verbose;
			break;
		case 'w':
			if (parse_range(optarg, &warning) != 0)
				usage();
			break;
		default:
			usage();
		}
	}

	if ((VSL_stats = VSL_OpenStats(n_arg)) == NULL)
		exit(1);

	/* Default: if no param specified, check hit ratio.  If no warning
	 * and critical values are specified either, set these to default.
	 */
	if (param == NULL) {
		param = strdup("ratio");
		if (!warning.defined)
			parse_range("95:", &warning);
		if (!critical.defined)
			parse_range("90:", &critical);
	}

	if (!param)
		usage();

	check_stats(VSL_stats, param);

	exit(0);
}
