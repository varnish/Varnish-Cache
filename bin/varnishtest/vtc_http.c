/*-
 * Copyright (c) 2008-2014 Varnish Software AS
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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "vtc.h"

#include "vct.h"
#include "vgz.h"
#include "vre.h"
#include "vtcp.h"

#define MAX_HDR		50

struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x2f02169c
	int			fd;
	int			*sfd;
	int			timeout;
	struct vtclog		*vl;

	struct vsb		*vsb;

	int			nrxbuf;
	char			*rxbuf;
	int			prxbuf;
	char			*body;
	unsigned		bodyl;
	char			bodylen[20];
	char			chunklen[20];

	char			*req[MAX_HDR];
	char			*resp[MAX_HDR];

	int			gziplevel;
	int			gzipresidual;

	int			fatal;
};

#define ONLY_CLIENT(hp, av)						\
	do {								\
		if (hp->sfd != NULL)					\
			vtc_log(hp->vl, 0,				\
			    "\"%s\" only possible in client", av[0]);	\
	} while (0)

#define ONLY_SERVER(hp, av)						\
	do {								\
		if (hp->sfd == NULL)					\
			vtc_log(hp->vl, 0,				\
			    "\"%s\" only possible in server", av[0]);	\
	} while (0)


/* XXX: we may want to vary this */
static const char * const nl = "\r\n";

/**********************************************************************
 * Generate a synthetic body
 */

static char *
synth_body(const char *len, int rnd)
{
	int i, j, k, l;
	char *b;


	AN(len);
	i = strtoul(len, NULL, 0);
	assert(i > 0);
	b = malloc(i + 1L);
	AN(b);
	l = k = '!';
	for (j = 0; j < i; j++) {
		if ((j % 64) == 63) {
			b[j] = '\n';
			k++;
			if (k == '~')
				k = '!';
			l = k;
		} else if (rnd) {
			b[j] = (random() % 95) + ' ';
		} else {
			b[j] = (char)l;
			if (++l == '~')
				l = '!';
		}
	}
	b[i - 1] = '\n';
	b[i] = '\0';
	return (b);
}

/**********************************************************************
 * Finish and write the vsb to the fd
 */

static void
http_write(const struct http *hp, int lvl, const char *pfx)
{
	ssize_t l;

	AZ(VSB_finish(hp->vsb));
	vtc_dump(hp->vl, lvl, pfx, VSB_data(hp->vsb), VSB_len(hp->vsb));
	l = write(hp->fd, VSB_data(hp->vsb), VSB_len(hp->vsb));
	if (l != VSB_len(hp->vsb))
		vtc_log(hp->vl, hp->fatal, "Write failed: (%zd vs %zd) %s",
		    l, VSB_len(hp->vsb), strerror(errno));
}

/**********************************************************************
 * find header
 */

static char *
http_find_header(char * const *hh, const char *hdr)
{
	int n, l;
	char *r;

	l = strlen(hdr);

	for (n = 3; hh[n] != NULL; n++) {
		if (strncasecmp(hdr, hh[n], l) || hh[n][l] != ':')
			continue;
		for (r = hh[n] + l + 1; vct_issp(*r); r++)
			continue;
		return (r);
	}
	return (NULL);
}

/**********************************************************************
 * Expect
 */

static const char *
cmd_var_resolve(struct http *hp, char *spec)
{
	char **hh, *hdr;

	if (!strcmp(spec, "req.method"))
		return(hp->req[0]);
	if (!strcmp(spec, "req.url"))
		return(hp->req[1]);
	if (!strcmp(spec, "req.proto"))
		return(hp->req[2]);
	if (!strcmp(spec, "resp.proto"))
		return(hp->resp[0]);
	if (!strcmp(spec, "resp.status"))
		return(hp->resp[1]);
	if (!strcmp(spec, "resp.msg"))
		return(hp->resp[2]);
	if (!strcmp(spec, "resp.chunklen"))
		return(hp->chunklen);
	if (!strcmp(spec, "req.bodylen"))
		return(hp->bodylen);
	if (!strcmp(spec, "resp.bodylen"))
		return(hp->bodylen);
	if (!strcmp(spec, "resp.body"))
		return(hp->body != NULL ? hp->body : spec);
	if (!memcmp(spec, "req.http.", 9)) {
		hh = hp->req;
		hdr = spec + 9;
	} else if (!memcmp(spec, "resp.http.", 10)) {
		hh = hp->resp;
		hdr = spec + 10;
	} else
		return (spec);
	hdr = http_find_header(hh, hdr);
	if (hdr != NULL)
		return (hdr);
	return ("<undef>");
}

static void
cmd_http_expect(CMD_ARGS)
{
	struct http *hp;
	const char *lhs;
	char *cmp;
	const char *rhs;
	vre_t *vre;
	const char *error;
	int erroroffset;
	int i, retval = -1;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(strcmp(av[0], "expect"));
	av++;

	AN(av[0]);
	AN(av[1]);
	AN(av[2]);
	AZ(av[3]);
	lhs = cmd_var_resolve(hp, av[0]);
	if (lhs == NULL)
		lhs = "<missing>";
	cmp = av[1];
	rhs = cmd_var_resolve(hp, av[2]);
	if (rhs == NULL)
		rhs = "<missing>";
	if (!strcmp(cmp, "==")) {
		retval = strcmp(lhs, rhs) == 0;
	} else if (!strcmp(cmp, "<")) {
		retval = strcmp(lhs, rhs) < 0;
	} else if (!strcmp(cmp, "<=")) {
		retval = strcmp(lhs, rhs) <= 0;
	} else if (!strcmp(cmp, ">=")) {
		retval = strcmp(lhs, rhs) >= 0;
	} else if (!strcmp(cmp, ">")) {
		retval = strcmp(lhs, rhs) > 0;
	} else if (!strcmp(cmp, "!=")) {
		retval = strcmp(lhs, rhs) != 0;
	} else if (!strcmp(cmp, "~") || !strcmp(cmp, "!~")) {
		vre = VRE_compile(rhs, 0, &error, &erroroffset);
		if (vre == NULL)
			vtc_log(hp->vl, 0, "REGEXP error: %s (@%d) (%s)",
			    error, erroroffset, rhs);
		i = VRE_exec(vre, lhs, strlen(lhs), 0, 0, NULL, 0, 0);
		retval = (i >= 0 && *cmp == '~') || (i < 0 && *cmp == '!');
		VRE_free(&vre);
	}
	if (retval == -1)
		vtc_log(hp->vl, 0,
		    "EXPECT %s (%s) %s %s (%s) test not implemented",
		    av[0], lhs, av[1], av[2], rhs);
	else
		vtc_log(hp->vl, retval ? 4 : 0, "EXPECT %s (%s) %s \"%s\" %s",
		    av[0], lhs, cmp, rhs, retval ? "match" : "failed");
}

/**********************************************************************
 * Split a HTTP protocol header
 */

static void
http_splitheader(struct http *hp, int req)
{
	char *p, *q, **hh;
	int n;
	char buf[20];

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (req) {
		memset(hp->req, 0, sizeof hp->req);
		hh = hp->req;
	} else {
		memset(hp->resp, 0, sizeof hp->resp);
		hh = hp->resp;
	}

	n = 0;
	p = hp->rxbuf;

	/* REQ/PROTO */
	while (vct_islws(*p))
		p++;
	hh[n++] = p;
	while (!vct_islws(*p))
		p++;
	AZ(vct_iscrlf(p));
	*p++ = '\0';

	/* URL/STATUS */
	while (vct_issp(*p))		/* XXX: H space only */
		p++;
	AZ(vct_iscrlf(p));
	hh[n++] = p;
	while (!vct_islws(*p))
		p++;
	if (vct_iscrlf(p)) {
		hh[n++] = NULL;
		q = p;
		p += vct_skipcrlf(p);
		*q = '\0';
	} else {
		*p++ = '\0';
		/* PROTO/MSG */
		while (vct_issp(*p))		/* XXX: H space only */
			p++;
		hh[n++] = p;
		while (!vct_iscrlf(p))
			p++;
		q = p;
		p += vct_skipcrlf(p);
		*q = '\0';
	}
	assert(n == 3);

	while (*p != '\0') {
		assert(n < MAX_HDR);
		if (vct_iscrlf(p))
			break;
		hh[n++] = p++;
		while (*p != '\0' && !vct_iscrlf(p))
			p++;
		q = p;
		p += vct_skipcrlf(p);
		*q = '\0';
	}
	p += vct_skipcrlf(p);
	assert(*p == '\0');

	for (n = 0; n < 3 || hh[n] != NULL; n++) {
		sprintf(buf, "http[%2d] ", n);
		vtc_dump(hp->vl, 4, buf, hh[n], -1);
	}
}


/**********************************************************************
 * Receive another character
 */

static int
http_rxchar(struct http *hp, int n, int eof)
{
	int i;
	struct pollfd pfd[1];

	while (n > 0) {
		pfd[0].fd = hp->fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		i = poll(pfd, 1, hp->timeout);
		if (i == 0)
			vtc_log(hp->vl, hp->fatal,
			    "HTTP rx timeout (fd:%d %u ms)",
			    hp->fd, hp->timeout);
		if (i < 0)
			vtc_log(hp->vl, hp->fatal,
			    "HTTP rx failed (fd:%d poll: %s)",
			    hp->fd, strerror(errno));
		assert(i > 0);
		assert(hp->prxbuf + n < hp->nrxbuf);
		i = read(hp->fd, hp->rxbuf + hp->prxbuf, n);
		if (!(pfd[0].revents & POLLIN))
			vtc_log(hp->vl, 4,
			    "HTTP rx poll (fd:%d revents: %x n=%d, i=%d)",
			    hp->fd, pfd[0].revents, n, i);
		if (i == 0 && eof)
			return (i);
		if (i == 0)
			vtc_log(hp->vl, hp->fatal,
			    "HTTP rx EOF (fd:%d read: %s)",
			    hp->fd, strerror(errno));
		if (i < 0)
			vtc_log(hp->vl, hp->fatal,
			    "HTTP rx failed (fd:%d read: %s)",
			    hp->fd, strerror(errno));
		hp->prxbuf += i;
		hp->rxbuf[hp->prxbuf] = '\0';
		n -= i;
	}
	return (1);
}

static int
http_rxchunk(struct http *hp)
{
	char *q;
	int l, i;

	l = hp->prxbuf;
	do
		(void)http_rxchar(hp, 1, 0);
	while (hp->rxbuf[hp->prxbuf - 1] != '\n');
	vtc_dump(hp->vl, 4, "len", hp->rxbuf + l, -1);
	i = strtoul(hp->rxbuf + l, &q, 16);
	bprintf(hp->chunklen, "%d", i);
	if ((q == hp->rxbuf + l) ||
		(*q != '\0' && !vct_islws(*q))) {
		vtc_log(hp->vl, hp->fatal, "chunked fail %02x @ %td",
		    *q, q - (hp->rxbuf + l));
	}
	assert(q != hp->rxbuf + l);
	assert(*q == '\0' || vct_islws(*q));
	hp->prxbuf = l;
	if (i > 0) {
		(void)http_rxchar(hp, i, 0);
		vtc_dump(hp->vl, 4, "chunk",
		    hp->rxbuf + l, i);
	}
	l = hp->prxbuf;
	(void)http_rxchar(hp, 2, 0);
	if(!vct_iscrlf(hp->rxbuf + l))
		vtc_log(hp->vl, hp->fatal,
		    "Wrong chunk tail[0] = %02x",
		    hp->rxbuf[l] & 0xff);
	if(!vct_iscrlf(hp->rxbuf + l + 1))
		vtc_log(hp->vl, hp->fatal,
		    "Wrong chunk tail[1] = %02x",
		    hp->rxbuf[l + 1] & 0xff);
	hp->prxbuf = l;
	hp->rxbuf[l] = '\0';
	return (i);
}

/**********************************************************************
 * Swallow a HTTP message body
 */

static void
http_swallow_body(struct http *hp, char * const *hh, int body)
{
	char *p;
	int i, l, ll;

	ll = 0;
	p = http_find_header(hh, "content-length");
	if (p != NULL) {
		hp->body = hp->rxbuf + hp->prxbuf;
		l = strtoul(p, NULL, 0);
		(void)http_rxchar(hp, l, 0);
		vtc_dump(hp->vl, 4, "body", hp->body, l);
		hp->bodyl = l;
		sprintf(hp->bodylen, "%d", l);
		return;
	}
	p = http_find_header(hh, "transfer-encoding");
	if (p != NULL && !strcasecmp(p, "chunked")) {
		while (http_rxchunk(hp) != 0)
			continue;
		vtc_dump(hp->vl, 4, "body", hp->body, ll);
		ll = hp->rxbuf + hp->prxbuf - hp->body;
		hp->bodyl = ll;
		sprintf(hp->bodylen, "%d", ll);
		return;
	}
	if (body) {
		hp->body = hp->rxbuf + hp->prxbuf;
		do  {
			i = http_rxchar(hp, 1, 1);
			ll += i;
		} while (i > 0);
		vtc_dump(hp->vl, 4, "rxeof", hp->body, ll);
	}
	hp->bodyl = ll;
	sprintf(hp->bodylen, "%d", ll);
}

/**********************************************************************
 * Receive a HTTP protocol header
 */

static void
http_rxhdr(struct http *hp)
{
	int i;
	char *p;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	hp->prxbuf = 0;
	hp->body = NULL;
	while (1) {
		(void)http_rxchar(hp, 1, 0);
		p = hp->rxbuf + hp->prxbuf - 1;
		for (i = 0; p > hp->rxbuf; p--) {
			if (*p != '\n')
				break;
			if (p - 1 > hp->rxbuf && p[-1] == '\r')
				p--;
			if (++i == 2)
				break;
		}
		if (i == 2)
			break;
	}
	vtc_dump(hp->vl, 4, "rxhdr", hp->rxbuf, -1);
}


/**********************************************************************
 * Receive a response
 */

static void
cmd_http_rxresp(CMD_ARGS)
{
	struct http *hp;
	int has_obj = 1;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	ONLY_CLIENT(hp, av);
	AZ(strcmp(av[0], "rxresp"));
	av++;

	for(; *av != NULL; av++)
		if (!strcmp(*av, "-no_obj"))
			has_obj = 0;
		else
			vtc_log(hp->vl, 0,
			    "Unknown http rxresp spec: %s\n", *av);
	http_rxhdr(hp);
	http_splitheader(hp, 0);
	hp->body = hp->rxbuf + hp->prxbuf;
	if (!has_obj)
		return;
	else if (!strcmp(hp->resp[1], "200"))
		http_swallow_body(hp, hp->resp, 1);
	else
		http_swallow_body(hp, hp->resp, 0);
	vtc_log(hp->vl, 4, "bodylen = %s", hp->bodylen);
}

static void
cmd_http_rxresphdrs(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	ONLY_CLIENT(hp, av);
	AZ(strcmp(av[0], "rxresphdrs"));
	av++;

	for(; *av != NULL; av++)
		vtc_log(hp->vl, 0, "Unknown http rxreq spec: %s\n", *av);
	http_rxhdr(hp);
	http_splitheader(hp, 0);
}


/**********************************************************************
 * Ungzip rx'ed body
 */

#define TRUST_ME(ptr)   ((void*)(uintptr_t)(ptr))

#define OVERHEAD 64L


static void
cmd_http_gunzip_body(CMD_ARGS)
{
	int i;
	z_stream vz;
	struct http *hp;
	char *p;
	unsigned l;

	(void)av;
	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);

	memset(&vz, 0, sizeof vz);

	AN(hp->body);
	if (hp->body[0] != (char)0x1f || hp->body[1] != (char)0x8b)
		vtc_log(hp->vl, hp->fatal,
		    "Gunzip error: Body lacks gzip magics");
	vz.next_in = TRUST_ME(hp->body);
	vz.avail_in = hp->bodyl;

	l = hp->bodyl * 10;
	p = calloc(l, 1);
	AN(p);

	vz.next_out = TRUST_ME(p);
	vz.avail_out = l;

	assert(Z_OK == inflateInit2(&vz, 31));
	i = inflate(&vz, Z_FINISH);
	hp->bodyl = vz.total_out;
	memcpy(hp->body, p, hp->bodyl);
	free(p);
	vtc_log(hp->vl, 3, "new bodylen %u", hp->bodyl);
	vtc_dump(hp->vl, 4, "body", hp->body, hp->bodyl);
	bprintf(hp->bodylen, "%u", hp->bodyl);
	vtc_log(hp->vl, 4, "startbit = %ju %ju/%ju",
	    (uintmax_t)vz.start_bit,
	    (uintmax_t)vz.start_bit >> 3, (uintmax_t)vz.start_bit & 7);
	vtc_log(hp->vl, 4, "lastbit = %ju %ju/%ju",
	    (uintmax_t)vz.last_bit,
	    (uintmax_t)vz.last_bit >> 3, (uintmax_t)vz.last_bit & 7);
	vtc_log(hp->vl, 4, "stopbit = %ju %ju/%ju",
	    (uintmax_t)vz.stop_bit,
	    (uintmax_t)vz.stop_bit >> 3, (uintmax_t)vz.stop_bit & 7);
	if (i != Z_STREAM_END)
		vtc_log(hp->vl, hp->fatal,
		    "Gunzip error = %d (%s) in:%jd out:%jd",
		    i, vz.msg, (intmax_t)vz.total_in, (intmax_t)vz.total_out);
	assert(Z_OK == inflateEnd(&vz));
}

/**********************************************************************
 * Create a gzip'ed body
 */

static void
gzip_body(const struct http *hp, const char *txt, char **body, int *bodylen)
{
	int l, i;
	z_stream vz;

	memset(&vz, 0, sizeof vz);

	l = strlen(txt);
	*body = calloc(l + OVERHEAD, 1);
	AN(*body);

	vz.next_in = TRUST_ME(txt);
	vz.avail_in = l;

	vz.next_out = TRUST_ME(*body);
	vz.avail_out = l + OVERHEAD;

	assert(Z_OK == deflateInit2(&vz,
	    hp->gziplevel, Z_DEFLATED, 31, 9, Z_DEFAULT_STRATEGY));
	assert(Z_STREAM_END == deflate(&vz, Z_FINISH));
	i = vz.stop_bit & 7;
	if (hp->gzipresidual >= 0 && hp->gzipresidual != i)
		vtc_log(hp->vl, hp->fatal,
		    "Wrong gzip residual got %d wanted %d",
		    i, hp->gzipresidual);
	*bodylen = vz.total_out;
	vtc_log(hp->vl, 4, "startbit = %ju %ju/%ju",
	    (uintmax_t)vz.start_bit,
	    (uintmax_t)vz.start_bit >> 3, (uintmax_t)vz.start_bit & 7);
	vtc_log(hp->vl, 4, "lastbit = %ju %ju/%ju",
	    (uintmax_t)vz.last_bit,
	    (uintmax_t)vz.last_bit >> 3, (uintmax_t)vz.last_bit & 7);
	vtc_log(hp->vl, 4, "stopbit = %ju %ju/%ju",
	    (uintmax_t)vz.stop_bit,
	    (uintmax_t)vz.stop_bit >> 3, (uintmax_t)vz.stop_bit & 7);
	assert(Z_OK == deflateEnd(&vz));
}

/**********************************************************************
 * Handle common arguments of a transmited request or response
 */

static char* const *
http_tx_parse_args(char * const *av, struct vtclog *vl, struct http *hp,
    char* body)
{
	int bodylen = 0;
	char *b, *c;
	char *nullbody = NULL;
	int nolen = 0;

	(void)vl;
	nullbody = body;

	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-nolen")) {
			nolen = 1;
		} else if (!strcmp(*av, "-hdr")) {
			VSB_printf(hp->vsb, "%s%s", av[1], nl);
			av++;
		} else
			break;
	}
	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-body")) {
			assert(body == nullbody);
			REPLACE(body, av[1]);

			AN(body);
			av++;
			bodylen = strlen(body);
			for (b = body; *b != '\0'; b++) {
				if(*b == '\\' && b[1] == '0') {
					*b = '\0';
					for(c = b+1; *c != '\0'; c++) {
						*c = c[1];
					}
					b++;
					bodylen--;
				}
			}
		} else if (!strcmp(*av, "-bodylen")) {
			assert(body == nullbody);
			body = synth_body(av[1], 0);
			bodylen = strlen(body);
			av++;
		} else if (!strcmp(*av, "-gzipresidual")) {
			hp->gzipresidual = strtoul(av[1], NULL, 0);
			av++;
		} else if (!strcmp(*av, "-gziplevel")) {
			hp->gziplevel = strtoul(av[1], NULL, 0);
			av++;
		} else if (!strcmp(*av, "-gziplen")) {
			assert(body == nullbody);
			b = synth_body(av[1], 1);
			gzip_body(hp, b, &body, &bodylen);
			VSB_printf(hp->vsb, "Content-Encoding: gzip%s", nl);
			// vtc_hexdump(hp->vl, 4, "gzip", (void*)body, bodylen);
			av++;
		} else if (!strcmp(*av, "-gzipbody")) {
			assert(body == nullbody);
			gzip_body(hp, av[1], &body, &bodylen);
			VSB_printf(hp->vsb, "Content-Encoding: gzip%s", nl);
			// vtc_hexdump(hp->vl, 4, "gzip", (void*)body, bodylen);
			av++;
		} else
			break;
	}
	if (body != NULL && !nolen)
		VSB_printf(hp->vsb, "Content-Length: %d%s", bodylen, nl);
	VSB_cat(hp->vsb, nl);
	if (body != NULL) {
		VSB_bcat(hp->vsb, body, bodylen);
		free(body);
	}
	return (av);
}

/**********************************************************************
 * Transmit a response
 */

static void
cmd_http_txresp(CMD_ARGS)
{
	struct http *hp;
	const char *proto = "HTTP/1.1";
	const char *status = "200";
	const char *msg = "OK";
	char* body = NULL;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	ONLY_SERVER(hp, av);
	AZ(strcmp(av[0], "txresp"));
	av++;

	VSB_clear(hp->vsb);

	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-proto")) {
			proto = av[1];
			av++;
		} else if (!strcmp(*av, "-status")) {
			status = av[1];
			av++;
		} else if (!strcmp(*av, "-msg")) {
			msg = av[1];
			av++;
			continue;
		} else
			break;
	}

	VSB_printf(hp->vsb, "%s %s %s%s", proto, status, msg, nl);

	/* send a "Content-Length: 0" header unless something else happens */
	REPLACE(body, "");

	av = http_tx_parse_args(av, vl, hp, body);
	if (*av != NULL)
		vtc_log(hp->vl, 0, "Unknown http txresp spec: %s\n", *av);

	http_write(hp, 4, "txresp");
}

/**********************************************************************
 * Receive a request
 */

static void
cmd_http_rxreq(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	ONLY_SERVER(hp, av);
	AZ(strcmp(av[0], "rxreq"));
	av++;

	for(; *av != NULL; av++)
		vtc_log(hp->vl, 0, "Unknown http rxreq spec: %s\n", *av);
	http_rxhdr(hp);
	http_splitheader(hp, 1);
	hp->body = hp->rxbuf + hp->prxbuf;
	http_swallow_body(hp, hp->req, 0);
	vtc_log(hp->vl, 4, "bodylen = %s", hp->bodylen);
}

static void
cmd_http_rxreqhdrs(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(strcmp(av[0], "rxreqhdrs"));
	av++;

	for(; *av != NULL; av++)
		vtc_log(hp->vl, 0, "Unknown http rxreq spec: %s\n", *av);
	http_rxhdr(hp);
	http_splitheader(hp, 1);
}

static void
cmd_http_rxbody(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	ONLY_SERVER(hp, av);
	AZ(strcmp(av[0], "rxbody"));
	av++;

	for(; *av != NULL; av++)
		vtc_log(hp->vl, 0, "Unknown http rxreq spec: %s\n", *av);
	http_swallow_body(hp, hp->req, 0);
	vtc_log(hp->vl, 4, "bodylen = %s", hp->bodylen);
}

static void
cmd_http_rxchunk(CMD_ARGS)
{
	struct http *hp;
	int ll, i;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	ONLY_CLIENT(hp, av);

	i = http_rxchunk(hp);
	if (i == 0) {
		ll = hp->rxbuf + hp->prxbuf - hp->body;
		hp->bodyl = ll;
		sprintf(hp->bodylen, "%d", ll);
		vtc_log(hp->vl, 4, "bodylen = %s", hp->bodylen);
	}
}

/**********************************************************************
 * Transmit a request
 */

static void
cmd_http_txreq(CMD_ARGS)
{
	struct http *hp;
	const char *req = "GET";
	const char *url = "/";
	const char *proto = "HTTP/1.1";

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	ONLY_CLIENT(hp, av);
	AZ(strcmp(av[0], "txreq"));
	av++;

	VSB_clear(hp->vsb);

	for(; *av != NULL; av++) {
		if (!strcmp(*av, "-url")) {
			url = av[1];
			av++;
		} else if (!strcmp(*av, "-proto")) {
			proto = av[1];
			av++;
		} else if (!strcmp(*av, "-req")) {
			req = av[1];
			av++;
		} else
			break;
	}
	VSB_printf(hp->vsb, "%s %s %s%s", req, url, proto, nl);

	av = http_tx_parse_args(av, vl, hp, NULL);
	if (*av != NULL)
		vtc_log(hp->vl, 0, "Unknown http txreq spec: %s\n", *av);
	http_write(hp, 4, "txreq");
}

/**********************************************************************
 * Send a string
 */

static void
cmd_http_send(CMD_ARGS)
{
	struct http *hp;
	int i;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(av[1]);
	AZ(av[2]);
	vtc_dump(hp->vl, 4, "send", av[1], -1);
	i = write(hp->fd, av[1], strlen(av[1]));
	if (i != strlen(av[1]))
		vtc_log(hp->vl, hp->fatal, "Write error in http_send(): %s",
		    strerror(errno));
}

/**********************************************************************
 * Send a hex string
 */

static void
cmd_http_sendhex(CMD_ARGS)
{
	struct http *hp;
	char buf[3], *q;
	uint8_t *p;
	int i, j, l;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(av[1]);
	AZ(av[2]);
	l = strlen(av[1]) / 2;
	p = malloc(l);
	AN(p);
	q = av[1];
	for (i = 0; i < l; i++) {
		while (vct_issp(*q))
			q++;
		if (*q == '\0')
			break;
		memcpy(buf, q, 2);
		q += 2;
		buf[2] = '\0';
		if (!vct_ishex(buf[0]) || !vct_ishex(buf[1]))
			vtc_log(hp->vl, 0, "Illegal Hex char \"%c%c\"",
			    buf[0], buf[1]);
		p[i] = (uint8_t)strtoul(buf, NULL, 16);
	}
	vtc_hexdump(hp->vl, 4, "sendhex", (void*)p, i);
	j = write(hp->fd, p, i);
	assert(j == i);
	free(p);

}

/**********************************************************************
 * Send a string as chunked encoding
 */

static void
cmd_http_chunked(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(av[1]);
	AZ(av[2]);
	VSB_clear(hp->vsb);
	VSB_printf(hp->vsb, "%jx%s%s%s",
	    (uintmax_t)strlen(av[1]), nl, av[1], nl);
	http_write(hp, 4, "chunked");
}

static void
cmd_http_chunkedlen(CMD_ARGS)
{
	unsigned len;
	unsigned u, v;
	char buf[16384];
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(av[1]);
	AZ(av[2]);
	VSB_clear(hp->vsb);

	len = atoi(av[1]);

	if (len == 0) {
		VSB_printf(hp->vsb, "0%s%s", nl, nl);
	} else {
		for (u = 0; u < sizeof buf; u++)
			buf[u] = (u & 7) + '0';

		VSB_printf(hp->vsb, "%x%s", len, nl);
		for (u = 0; u < len; u += v) {
			v = len - u;
			if (v > sizeof buf)
				v = sizeof buf;
			VSB_bcat(hp->vsb, buf, v);
		}
		VSB_printf(hp->vsb, "%s", nl);
	}
	http_write(hp, 4, "chunked");
}

/**********************************************************************
 * set the timeout
 */

static void
cmd_http_timeout(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(av[1]);
	AZ(av[2]);
	hp->timeout = (int)(strtod(av[1], NULL) * 1000.0);
}

/**********************************************************************
 * expect other end to close (server only)
 */

static void
cmd_http_expect_close(CMD_ARGS)
{
	struct http *hp;
	struct pollfd fds[1];
	char c;
	int i;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(av[1]);

	vtc_log(vl, 4, "Expecting close (fd = %d)", hp->fd);
	while (1) {
		fds[0].fd = hp->fd;
		fds[0].events = POLLIN | POLLERR;
		fds[0].revents = 0;
		i = poll(fds, 1, 1000);
		if (i == 0)
			vtc_log(vl, hp->fatal, "Expected close: timeout");
		if (i != 1 || !(fds[0].revents & POLLIN))
			vtc_log(vl, hp->fatal,
			    "Expected close: poll = %d, revents = 0x%x",
			    i, fds[0].revents);
		i = read(hp->fd, &c, 1);
		if (VTCP_Check(i))
			break;
		if (i == 1 && vct_islws(c))
			continue;
		vtc_log(vl, hp->fatal,
		    "Expecting close: read = %d, c = 0x%02x", i, c);
	}
	vtc_log(vl, 4, "fd=%d EOF, as expected", hp->fd);
}

/**********************************************************************
 * close a new connection  (server only)
 */

static void
cmd_http_close(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(av[1]);
	assert(hp->sfd != NULL);
	assert(*hp->sfd >= 0);
	VTCP_close(&hp->fd);
	vtc_log(vl, 4, "Closed");
}

/**********************************************************************
 * close and accept a new connection  (server only)
 */

static void
cmd_http_accept(CMD_ARGS)
{
	struct http *hp;

	(void)cmd;
	(void)vl;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AZ(av[1]);
	assert(hp->sfd != NULL);
	assert(*hp->sfd >= 0);
	if (hp->fd >= 0)
		VTCP_close(&hp->fd);
	vtc_log(vl, 4, "Accepting");
	hp->fd = accept(*hp->sfd, NULL, NULL);
	if (hp->fd < 0)
		vtc_log(vl, hp->fatal, "Accepted failed: %s", strerror(errno));
	vtc_log(vl, 3, "Accepted socket fd is %d", hp->fd);
}

/**********************************************************************
 * loop operator
 */

static void
cmd_http_loop(CMD_ARGS)
{
	struct http *hp;
	unsigned n, m;
	char *s;

	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);
	AN(av[1]);
	AN(av[2]);
	AZ(av[3]);
	n = strtoul(av[1], NULL, 0);
	for (m = 1 ; m <= n; m++) {
		vtc_log(vl, 4, "Loop #%u", m);
		s = strdup(av[2]);
		AN(s);
		parse_string(s, cmd, hp, vl);
	}
}

/**********************************************************************
 * Control fatality
 */

static void
cmd_http_fatal(CMD_ARGS)
{
	struct http *hp;
	CAST_OBJ_NOTNULL(hp, priv, HTTP_MAGIC);

	AZ(av[1]);
	if (!strcmp(av[0], "fatal"))
		hp->fatal = 0;
	else if (!strcmp(av[0], "non-fatal"))
		hp->fatal = -1;
	else {
		vtc_log(vl, 0, "XXX: fatal %s", cmd->name);
	}
}

/**********************************************************************
 * Execute HTTP specifications
 */

static const struct cmds http_cmds[] = {
	{ "timeout",		cmd_http_timeout },
	{ "txreq",		cmd_http_txreq },

	{ "rxreq",		cmd_http_rxreq },
	{ "rxreqhdrs",		cmd_http_rxreqhdrs },
	{ "rxchunk",		cmd_http_rxchunk },
	{ "rxbody",		cmd_http_rxbody },

	{ "txresp",		cmd_http_txresp },
	{ "rxresp",		cmd_http_rxresp },
	{ "rxresphdrs",		cmd_http_rxresphdrs },
	{ "gunzip",		cmd_http_gunzip_body },
	{ "expect",		cmd_http_expect },
	{ "send",		cmd_http_send },
	{ "sendhex",		cmd_http_sendhex },
	{ "chunked",		cmd_http_chunked },
	{ "chunkedlen",		cmd_http_chunkedlen },
	{ "delay",		cmd_delay },
	{ "sema",		cmd_sema },
	{ "expect_close",	cmd_http_expect_close },
	{ "close",		cmd_http_close },
	{ "accept",		cmd_http_accept },
	{ "loop",		cmd_http_loop },
	{ "fatal",		cmd_http_fatal },
	{ "non-fatal",		cmd_http_fatal },
	{ NULL,			NULL }
};

int
http_process(struct vtclog *vl, const char *spec, int sock, int *sfd)
{
	struct http *hp;
	char *s, *q;
	int retval;

	(void)sfd;
	ALLOC_OBJ(hp, HTTP_MAGIC);
	AN(hp);
	hp->fd = sock;
	hp->timeout = vtc_maxdur * 1000 / 2;
	hp->nrxbuf = 2048*1024;
	hp->vsb = VSB_new_auto();
	hp->rxbuf = malloc(hp->nrxbuf);		/* XXX */
	hp->sfd = sfd;
	hp->vl = vl;
	hp->gziplevel = 0;
	hp->gzipresidual = -1;
	AN(hp->rxbuf);
	AN(hp->vsb);

	s = strdup(spec);
	q = strchr(s, '\0');
	assert(q > s);
	AN(s);
	parse_string(s, http_cmds, hp, vl);
	retval = hp->fd;
	VSB_delete(hp->vsb);
	free(hp->rxbuf);
	free(hp);
	return (retval);
}

/**********************************************************************
 * Magic test routine
 *
 * This function brute-forces some short strings through gzip(9) to
 * find candidates for all possible 8 bit positions of the stopbit.
 *
 * Here is some good short output strings:
 *
 *	0 184 <e04c8d0fd604c>
 *	1 257 <1ea86e6cf31bf4ec3d7a86>
 *	2 106 <10>
 *	3 163 <a5e2e2e1c2e2>
 *	4 180 <71c5d18ec5d5d1>
 *	5 189 <39886d28a6d2988>
 *	6 118 <80000>
 *	7 151 <386811868>
 *
 */

#if 0
void xxx(void);

void
xxx(void)
{
	z_stream vz;
	int n;
	char ibuf[200];
	char obuf[200];
	int fl[8];
	int i, j;

	for (n = 0; n < 8; n++)
		fl[n] = 9999;

	memset(&vz, 0, sizeof vz);

	for(n = 0;  n < 999999999; n++) {
		*ibuf = 0;
		for (j = 0; j < 7; j++) {
			sprintf(strchr(ibuf, 0), "%x",
			    (unsigned)random() & 0xffff);
			vz.next_in = TRUST_ME(ibuf);
			vz.avail_in = strlen(ibuf);
			vz.next_out = TRUST_ME(obuf);
			vz.avail_out = sizeof obuf;
			assert(Z_OK == deflateInit2(&vz,
			    9, Z_DEFLATED, 31, 9, Z_DEFAULT_STRATEGY));
			assert(Z_STREAM_END == deflate(&vz, Z_FINISH));
			i = vz.stop_bit & 7;
			if (fl[i] > strlen(ibuf)) {
				printf("%d %jd <%s>\n", i, vz.stop_bit, ibuf);
				fl[i] = strlen(ibuf);
			}
			assert(Z_OK == deflateEnd(&vz));
		}
	}

	printf("FOO\n");
}
#endif
