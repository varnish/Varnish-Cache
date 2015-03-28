/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Dag-Erling Smørgrav <des@des.no>
 * Author: Guillaume Quintard <guillaume.quintard@gmail.com>
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
 * Log tailer for Varnish
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vcurses.h"
#include "vapi/vsm.h"
#include "vapi/vsl.h"
#include "vapi/voptget.h"
#include "vas.h"
#include "vdef.h"
#include "vcs.h"
#include "vtree.h"
#include "vsb.h"
#include "vut.h"

#if 0
#define AC(x) assert((x) != ERR)
#else
#define AC(x) x
#endif

static const char progname[] = "varnishtop";
static float period = 60; /* seconds */
static int end_of_file = 0;

struct top {
	uint8_t			tag;
	const char		*rec_data;
	char			*rec_buf;
	int			clen;
	unsigned		hash;
	VRB_ENTRY(top)		e_order;
	VRB_ENTRY(top)		e_key;
	double			count;
};

static VRB_HEAD(t_order, top) h_order = VRB_INITIALIZER(&h_order);
static VRB_HEAD(t_key, top) h_key = VRB_INITIALIZER(&h_key);

static inline int
cmp_key(const struct top *a, const struct top *b)
{
	if (a->hash != b->hash)
		return (a->hash - b->hash);
	if (a->tag != b->tag)
		return (a->tag - b->tag);
	if (a->clen != b->clen)
		return (a->clen - b->clen);
	return (memcmp(a->rec_data, b->rec_data, a->clen));
}

static inline int
cmp_order(const struct top *a, const struct top *b)
{
	if (a->count > b->count)
		return (-1);
	else if (a->count < b->count)
		return (1);
	return (cmp_key(a, b));
}

static unsigned ntop;

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static int f_flag = 0;

static unsigned maxfieldlen = 0;

VRB_PROTOTYPE(t_order, top, e_order, cmp_order);
VRB_GENERATE(t_order, top, e_order, cmp_order);
VRB_PROTOTYPE(t_key, top, e_key, cmp_key);
VRB_GENERATE(t_key, top, e_key, cmp_key);

static int __match_proto__(VSLQ_dispatch_f)
accumulate(struct VSL_data *vsl, struct VSL_transaction * const pt[],
	void *priv)
{
	struct top *tp, t;
	unsigned int u;
	unsigned tag;
	const char *b, *e, *p;
	unsigned len;
	struct VSL_transaction *tr;

	(void)priv;

	for (tr = pt[0]; tr != NULL; tr = *++pt) {
		while ((1 == VSL_Next(tr->c))) {
			if (!VSL_Match(vsl, tr->c))
				continue;
			tag = VSL_TAG(tr->c->rec.ptr);
			b = VSL_CDATA(tr->c->rec.ptr);
			e = b + VSL_LEN(tr->c->rec.ptr);
			u = 0;
			for (p = b; p <= e; p++) {
				if (*p == '\0')
					break;
				if (f_flag && (*p == ':' || isspace(*p)))
					break;
				u += *p;
			}
			len = p - b;
			if (len == 0)
				continue;

			t.hash = u;
			t.tag = tag;
			t.clen = len;
			t.rec_data = VSL_CDATA(tr->c->rec.ptr);

			AZ(pthread_mutex_lock(&mtx));
			tp = VRB_FIND(t_key, &h_key, &t);
			if (tp) {
				VRB_REMOVE(t_order, &h_order, tp);
				tp->count += 1.0;
				/* Reinsert to rebalance */
				VRB_INSERT(t_order, &h_order, tp);
			} else {
				ntop++;
				tp = calloc(sizeof *tp, 1);
				assert(tp != NULL);
				tp->hash = u;
				tp->count = 1.0;
				tp->clen = len;
				tp->tag = tag;
				tp->rec_buf = strdup(t.rec_data);
				tp->rec_data = tp->rec_buf;
				AN(tp->rec_data);
				VRB_INSERT(t_key, &h_key, tp);
				VRB_INSERT(t_order, &h_order, tp);
			}
			AZ(pthread_mutex_unlock(&mtx));

		}
	}

	return (0);
}

static void
update(int p)
{
	struct top *tp, *tp2;
	int l, len;
	double t = 0;
	static time_t last = 0;
	static unsigned n;
	time_t now;

	now = time(NULL);
	if (now == last)
		return;
	last = now;

	l = 1;
	if (n < p)
		n++;
	AC(erase());
	if (end_of_file)
		AC(mvprintw(0, COLS - 1 - strlen(VUT.name) - 5, "%s (EOF)",
			VUT.name));
	else
		AC(mvprintw(0, COLS - 1 - strlen(VUT.name), "%s", VUT.name));
	AC(mvprintw(0, 0, "list length %u", ntop));
	for (tp = VRB_MIN(t_order, &h_order); tp != NULL; tp = tp2) {
		tp2 = VRB_NEXT(t_order, &h_order, tp);

		if (++l < LINES) {
			len = tp->clen;
			if (len > COLS - 20)
				len = COLS - 20;
			AC(mvprintw(l, 0, "%9.2f %-*.*s %*.*s\n",
				tp->count, maxfieldlen, maxfieldlen,
				VSL_tags[tp->tag],
				len, len, tp->rec_data));
			t = tp->count;
		}
		if (end_of_file)
			continue;
		tp->count += (1.0/3.0 - tp->count) / (double)n;
		if (tp->count * 10 < t || l > LINES * 10) {
			VRB_REMOVE(t_key, &h_key, tp);
			VRB_REMOVE(t_order, &h_order, tp);
			free(tp->rec_buf);
			free(tp);
			ntop--;
		}
	}
	AC(refresh());
}

static void *
do_curses(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < 256; i++) {
		if (VSL_tags[i] == NULL)
			continue;
		if (maxfieldlen < strlen(VSL_tags[i]))
			maxfieldlen = strlen(VSL_tags[i]);
	}

	(void)initscr();
	AC(raw());
	AC(noecho());
	AC(nonl());
	AC(intrflush(stdscr, FALSE));
	(void)curs_set(0);
	AC(erase());
	for (;;) {
		AZ(pthread_mutex_lock(&mtx));
		update(period);
		AZ(pthread_mutex_unlock(&mtx));

		timeout(1000);
		switch (getch()) {
		case ERR:
			break;
#ifdef KEY_RESIZE
		case KEY_RESIZE:
			AC(erase());
			break;
#endif
		case '\014': /* Ctrl-L */
		case '\024': /* Ctrl-T */
			AC(redrawwin(stdscr));
			AC(refresh());
			break;
		case '\032': /* Ctrl-Z */
			AC(endwin());
			AZ(raise(SIGTSTP));
			break;
		case '\003': /* Ctrl-C */
		case '\021': /* Ctrl-Q */
		case 'Q':
		case 'q':
			AZ(raise(SIGINT));
			AC(endwin());
			return NULL;
		default:
			AC(beep());
			break;
		}
	}
	return NULL;

}

static void
dump(void)
{
	struct top *tp, *tp2;
	for (tp = VRB_MIN(t_order, &h_order); tp != NULL; tp = tp2) {
		tp2 = VRB_NEXT(t_order, &h_order, tp);
		if (tp->count <= 1.0)
			break;
		printf("%9.2f %s %*.*s\n",
			tp->count, VSL_tags[tp->tag],
			tp->clen, tp->clen, tp->rec_data);
	}
}

static void
usage(int status)
{
	const char **opt;

	fprintf(stderr, "Usage: %s <options>\n\n", progname);
	fprintf(stderr, "Options:\n");
	for (opt = vopt_usage; *opt != NULL; opt +=2)
		fprintf(stderr, " %-25s %s\n", *opt, *(opt + 1));
	exit(status);
}

int
main(int argc, char **argv)
{
	int o, once = 0;
	pthread_t thr;

	VUT_Init(progname);

	while ((o = getopt(argc, argv, vopt_optstring)) != -1) {
		switch (o) {
		case '1':
			AN(VUT_Arg('d', NULL));
			once = 1;
			break;
		case 'f':
			f_flag = 1;
			break;
		case 'h':
			/* Usage help */
			usage(0);
		case 'p':
			errno = 0;
			period = strtol(optarg, NULL, 0);
			if (errno != 0)  {
				fprintf(stderr,
				    "Syntax error, %s is not a number", optarg);
				exit(1);
			}
			break;
		default:
			if (!VUT_Arg(o, optarg))
				usage(1);
		}
	}

	VUT_Setup();
	if (!once) {
		if (pthread_create(&thr, NULL, do_curses, NULL) != 0) {
			fprintf(stderr, "pthread_create(): %s\n",
			    strerror(errno));
			exit(1);
		}
	}
	VUT.dispatch_f = &accumulate;
	VUT.dispatch_priv = NULL;
	VUT_Main();
	end_of_file = 1;
	if (once)
		dump();
	else
		pthread_join(thr, NULL);
	VUT_Fini();
	exit(0);
}
