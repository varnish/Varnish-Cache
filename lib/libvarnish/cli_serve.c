/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 * Stuff for handling the CLI protocol
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vdef.h"
#include "vas.h"
#include "miniobj.h"

#include "vav.h"
#include "vcli.h"
#include "vcli_common.h"
#include "vcli_priv.h"
#include "vcli_serve.h"
#include "vlu.h"
#include "vqueue.h"
#include "vsb.h"

struct VCLS_func {
	unsigned			magic;
#define VCLS_FUNC_MAGIC			0x7d280c9b
	VTAILQ_ENTRY(VCLS_func)		list;
	unsigned			auth;
	struct cli_proto		*clp;
};

struct VCLS_fd {
	unsigned			magic;
#define VCLS_FD_MAGIC			0x010dbd1e
	VTAILQ_ENTRY(VCLS_fd)		list;
	int				fdi, fdo;
	struct VCLS			*cls;
	struct cli			*cli, clis;
	cls_cb_f			*closefunc;
	void				*priv;
	struct vsb			*last_arg;
	int				last_idx;
	char				**argv;
};

struct VCLS {
	unsigned			magic;
#define VCLS_MAGIC			0x60f044a3
	VTAILQ_HEAD(,VCLS_fd)		fds;
	unsigned			nfd;
	VTAILQ_HEAD(,VCLS_func)		funcs;
	cls_cbc_f			*before, *after;
	volatile unsigned		*maxlen;
	volatile unsigned		*limit;
};

/*--------------------------------------------------------------------*/

void
VCLS_func_close(struct cli *cli, const char *const *av, void *priv)
{

	(void)av;
	(void)priv;
	VCLI_Out(cli, "Closing CLI connection");
	VCLI_SetResult(cli, CLIS_CLOSE);
}

/*--------------------------------------------------------------------*/

void
VCLS_func_ping(struct cli *cli, const char * const *av, void *priv)
{
	time_t t;

	(void)priv;
	(void)av;
	t = time(NULL);
	VCLI_Out(cli, "PONG %jd 1.0", (intmax_t)t);
}

/*--------------------------------------------------------------------*/

void
VCLS_func_help(struct cli *cli, const char * const *av, void *priv)
{
	struct cli_proto *cp;
	struct VCLS_func *cfn;
	unsigned all, debug, u, d, h, i, wc;
	struct VCLS *cs;

	(void)priv;
	cs = cli->cls;
	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);

	if (av[2] == NULL) {
		all = debug = 0;
	} else if (!strcmp(av[2], "-a")) {
		all = 1;
		debug = 0;
	} else if (!strcmp(av[2], "-d")) {
		all = 0;
		debug = 1;
	} else {
		VTAILQ_FOREACH(cfn, &cs->funcs, list) {
			if (cfn->auth > cli->auth)
				continue;
			for (cp = cfn->clp; cp->request != NULL; cp++) {
				if (!strcmp(cp->request, av[2])) {
					VCLI_Out(cli, "%s\n%s\n",
					    cp->syntax, cp->help);
					return;
				}
				for (u = 0; u < sizeof cp->flags; u++) {
					if (cp->flags[u] == '*') {
						cp->func(cli,av,priv);
						return;
					}
				}
			}
		}
		VCLI_Out(cli, "Unknown request.\nType 'help' for more info.\n");
		VCLI_SetResult(cli, CLIS_UNKNOWN);
		return;
	}
	VTAILQ_FOREACH(cfn, &cs->funcs, list) {
		if (cfn->auth > cli->auth)
			continue;
		for (cp = cfn->clp; cp->request != NULL; cp++) {
			d = 0;
			h = 0;
			i = 0;
			wc = 0;
			for (u = 0; u < sizeof cp->flags; u++) {
				if (cp->flags[u] == '\0')
					continue;
				if (cp->flags[u] == 'd')
					d = 1;
				if (cp->flags[u] == 'h')
					h = 1;
				if (cp->flags[u] == 'i')
					i = 1;
				if (cp->flags[u] == '*')
					wc = 1;
			}
			if (i)
				continue;
			if (wc) {
				cp->func(cli, av, priv);
				continue;
			}
			if (debug != d)
				continue;
			if (h && !all)
				continue;
			if (cp->syntax != NULL)
				VCLI_Out(cli, "%s\n", cp->syntax);
		}
	}
}

/*--------------------------------------------------------------------
 * Look for a CLI command to execute
 */

static int
cls_dispatch(struct cli *cli, struct cli_proto *clp, char * const * av,
    unsigned ac)
{
	struct cli_proto *cp;

	AN(av);
	for (cp = clp; cp->request != NULL; cp++) {
		if (!strcmp(av[1], cp->request))
			break;
		if (!strcmp("*", cp->request))
			break;
	}
	if (cp->request == NULL)
		return (0);

	if (cp->func == NULL) {
		VCLI_Out(cli, "Unimplemented\n");
		VCLI_SetResult(cli, CLIS_UNIMPL);
		return(1);
	}

	if (ac - 1 < cp->minarg) {
		VCLI_Out(cli, "Too few parameters\n");
		VCLI_SetResult(cli, CLIS_TOOFEW);
		return(1);
	}

	if (ac - 1> cp->maxarg) {
		VCLI_Out(cli, "Too many parameters\n");
		VCLI_SetResult(cli, CLIS_TOOMANY);
		return(1);
	}

	cli->result = CLIS_OK;
	VSB_clear(cli->sb);
	cp->func(cli, (const char * const *)av, cp->priv);
	return (1);
}

/*--------------------------------------------------------------------
 * We have collected a full cli line, parse it and execute, if possible.
 */

static int
cls_vlu2(void *priv, char * const *av)
{
	struct VCLS_fd *cfd;
	struct VCLS *cs;
	struct VCLS_func *cfn;
	struct cli *cli;
	unsigned na;
	ssize_t len;
	char *s;
	unsigned lim;
	const char *trunc = "!\n[response was truncated]\n";

	CAST_OBJ_NOTNULL(cfd, priv, VCLS_FD_MAGIC);
	cs = cfd->cls;
	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);

	cli = cfd->cli;
	CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);
	AN(cli->cmd);

	cli->cls = cs;

	cli->result = CLIS_UNKNOWN;
	VSB_clear(cli->sb);
	VCLI_Out(cli, "Unknown request.\nType 'help' for more info.\n");

	if (cs->before != NULL)
		cs->before(cli);

	do {
		if (av[0] != NULL) {
			VCLI_Out(cli, "Syntax Error: %s\n", av[0]);
			VCLI_SetResult(cli, CLIS_SYNTAX);
			break;
		}

		if (isupper(av[1][0])) {
			VCLI_Out(cli, "all commands are in lower-case.\n");
			VCLI_SetResult(cli, CLIS_UNKNOWN);
			break;
		}

		if (!islower(av[1][0]))
			break;

		for (na = 0; av[na + 1] != NULL; na++)
			continue;

		VTAILQ_FOREACH(cfn, &cs->funcs, list) {
			if (cfn->auth > cli->auth)
				continue;
			if (cls_dispatch(cli, cfn->clp, av, na))
				break;
		}
	} while (0);

	AZ(VSB_finish(cli->sb));

	if (cs->after != NULL)
		cs->after(cli);

	cli->cls = NULL;

	s = VSB_data(cli->sb);
	len = VSB_len(cli->sb);
	lim = *cs->limit;
	if (len > lim) {
		if (cli->result == CLIS_OK)
			cli->result = CLIS_TRUNCATED;
		strcpy(s + (lim - strlen(trunc)), trunc);
		assert(strlen(s) <= lim);
	}
	if (VCLI_WriteResult(cfd->fdo, cli->result, s) ||
	    cli->result == CLIS_CLOSE)
		return (1);

	return (0);
}

static int
cls_vlu(void *priv, const char *p)
{
	struct VCLS_fd *cfd;
	struct cli *cli;
	int i;
	char **av;

	CAST_OBJ_NOTNULL(cfd, priv, VCLS_FD_MAGIC);
	AN(p);

	cli = cfd->cli;
	CHECK_OBJ_NOTNULL(cli, CLI_MAGIC);

	if (cfd->argv == NULL) {
		/*
		 * Lines with only whitespace are simply ignored, in order
		 * to not complicate CLI-client side scripts and TELNET users
		 */
		for (; isspace(*p); p++)
			continue;
		if (*p == '\0')
			return (0);
		REPLACE(cli->cmd, p);

		av = VAV_Parse(p, NULL, 0);
		AN(av);
		if (av[0] != NULL) {
			i = cls_vlu2(priv, av);
			VAV_Free(av);
			free(cli->cmd);
			cli->cmd = NULL;
			return (i);
		}
		for (i = 1; av[i] != NULL; i++)
			continue;
		if (i < 3 || cli->auth == 0 || strcmp(av[i - 2], "<<")) {
			i = cls_vlu2(priv, av);
			VAV_Free(av);
			free(cli->cmd);
			cli->cmd = NULL;
			return (i);
		}
		cfd->argv = av;
		cfd->last_idx = i - 2;
		cfd->last_arg = VSB_new_auto();
		AN(cfd->last_arg);
		return (0);
	} else {
		AN(cfd->argv[cfd->last_idx]);
		AZ(strcmp(cfd->argv[cfd->last_idx], "<<"));
		AN(cfd->argv[cfd->last_idx + 1]);
		if (strcmp(p, cfd->argv[cfd->last_idx + 1])) {
			VSB_cat(cfd->last_arg, p);
			VSB_cat(cfd->last_arg, "\n");
			return (0);
		}
		AZ(VSB_finish(cfd->last_arg));
		free(cfd->argv[cfd->last_idx]);
		cfd->argv[cfd->last_idx] = NULL;
		free(cfd->argv[cfd->last_idx + 1]);
		cfd->argv[cfd->last_idx + 1] = NULL;
		cfd->argv[cfd->last_idx] = VSB_data(cfd->last_arg);
		i = cls_vlu2(priv, cfd->argv);
		cfd->argv[cfd->last_idx] = NULL;
		VAV_Free(cfd->argv);
		cfd->argv = NULL;
		free(cli->cmd);
		cli->cmd = NULL;
		VSB_delete(cfd->last_arg);
		cfd->last_arg = NULL;
		cfd->last_idx = 0;
		return (i);
	}
}

struct VCLS *
VCLS_New(cls_cbc_f *before, cls_cbc_f *after, volatile unsigned *maxlen,
    volatile unsigned *limit)
{
	struct VCLS *cs;

	ALLOC_OBJ(cs, VCLS_MAGIC);
	AN(cs);
	VTAILQ_INIT(&cs->fds);
	VTAILQ_INIT(&cs->funcs);
	cs->before = before;
	cs->after = after;
	cs->maxlen = maxlen;
	cs->limit = limit;
	return (cs);
}

struct cli *
VCLS_AddFd(struct VCLS *cs, int fdi, int fdo, cls_cb_f *closefunc, void *priv)
{
	struct VCLS_fd *cfd;

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	assert(fdi >= 0);
	assert(fdo >= 0);
	ALLOC_OBJ(cfd, VCLS_FD_MAGIC);
	AN(cfd);
	cfd->cls = cs;
	cfd->fdi = fdi;
	cfd->fdo = fdo;
	cfd->cli = &cfd->clis;
	cfd->cli->magic = CLI_MAGIC;
	cfd->cli->vlu = VLU_New(cfd, cls_vlu, *cs->maxlen);
	cfd->cli->sb = VSB_new_auto();
	cfd->cli->limit = cs->limit;
	cfd->closefunc = closefunc;
	cfd->priv = priv;
	AN(cfd->cli->sb);
	VTAILQ_INSERT_TAIL(&cs->fds, cfd, list);
	cs->nfd++;
	return (cfd->cli);
}

static void
cls_close_fd(struct VCLS *cs, struct VCLS_fd *cfd)
{

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	CHECK_OBJ_NOTNULL(cfd, VCLS_FD_MAGIC);

	VTAILQ_REMOVE(&cs->fds, cfd, list);
	cs->nfd--;
	VLU_Destroy(cfd->cli->vlu);
	VSB_delete(cfd->cli->sb);
	if (cfd->closefunc == NULL) {
		(void)close(cfd->fdi);
		if (cfd->fdo != cfd->fdi)
			(void)close(cfd->fdo);
	} else {
		cfd->closefunc(cfd->priv);
	}
	if (cfd->cli->ident != NULL)
		free(cfd->cli->ident);
	FREE_OBJ(cfd);
}


int
VCLS_AddFunc(struct VCLS *cs, unsigned auth, struct cli_proto *clp)
{
	struct VCLS_func *cfn;

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	ALLOC_OBJ(cfn, VCLS_FUNC_MAGIC);
	AN(cfn);
	cfn->clp = clp;
	cfn->auth = auth;
	VTAILQ_INSERT_TAIL(&cs->funcs, cfn, list);
	return (0);
}

int
VCLS_PollFd(struct VCLS *cs, int fd, int timeout)
{
	struct VCLS_fd *cfd;
	struct pollfd pfd[1];
	int i, j, k;

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	if (cs->nfd == 0) {
		errno = 0;
		return (-1);
	}
	assert(cs->nfd > 0);

	i = 0;
	VTAILQ_FOREACH(cfd, &cs->fds, list) {
		if (cfd->fdi != fd)
			continue;
		pfd[i].fd = cfd->fdi;
		pfd[i].events = POLLIN;
		pfd[i].revents = 0;
		i++;
		break;
	}
	assert(i == 1);
	CHECK_OBJ_NOTNULL(cfd, VCLS_FD_MAGIC);

	j = poll(pfd, 1, timeout);
	if (j <= 0)
		return (j);
	if (pfd[0].revents & POLLHUP)
		k = 1;
	else
		k = VLU_Fd(cfd->fdi, cfd->cli->vlu);
	if (k)
		cls_close_fd(cs, cfd);
	return (k);
}

int
VCLS_Poll(struct VCLS *cs, int timeout)
{
	struct VCLS_fd *cfd, *cfd2;
	int i, j, k;

	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	if (cs->nfd == 0) {
		errno = 0;
		return (-1);
	}
	assert(cs->nfd > 0);
	{
		struct pollfd pfd[cs->nfd];

		i = 0;
		VTAILQ_FOREACH(cfd, &cs->fds, list) {
			pfd[i].fd = cfd->fdi;
			pfd[i].events = POLLIN;
			pfd[i].revents = 0;
			i++;
		}
		assert(i == cs->nfd);

		j = poll(pfd, cs->nfd, timeout);
		if (j <= 0)
			return (j);
		i = 0;
		VTAILQ_FOREACH_SAFE(cfd, &cs->fds, list, cfd2) {
			assert(pfd[i].fd == cfd->fdi);
			if (pfd[i].revents & POLLHUP)
				k = 1;
			else
				k = VLU_Fd(cfd->fdi, cfd->cli->vlu);
			if (k)
				cls_close_fd(cs, cfd);
			i++;
		}
		assert(i == j);
	}
	return (j);
}

void
VCLS_Destroy(struct VCLS **csp)
{
	struct VCLS *cs;
	struct VCLS_fd *cfd, *cfd2;
	struct VCLS_func *cfn;

	cs = *csp;
	*csp = NULL;
	CHECK_OBJ_NOTNULL(cs, VCLS_MAGIC);
	VTAILQ_FOREACH_SAFE(cfd, &cs->fds, list, cfd2)
		cls_close_fd(cs, cfd);

	while (!VTAILQ_EMPTY(&cs->funcs)) {
		cfn = VTAILQ_FIRST(&cs->funcs);
		VTAILQ_REMOVE(&cs->funcs, cfn, list);
		FREE_OBJ(cfn);
	}
	FREE_OBJ(cs);
}
