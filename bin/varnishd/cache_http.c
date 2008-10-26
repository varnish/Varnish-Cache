/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 *
 * HTTP request storage and manipulation
 */

#include "config.h"

#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config.h"
#include "shmlog.h"
#include "vct.h"
#include "cache.h"

#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif

#define HTTPH(a, b, c, d, e, f, g) char b[] = "*" a ":";
#include "http_headers.h"
#undef HTTPH

/*lint -save -e773 not () */
#define LOGMTX2(ax, bx, cx) 	[bx] = SLT_##ax##cx

#define LOGMTX1(ax) { 		\
	LOGMTX2(ax, HTTP_HDR_REQ,	Request),	\
	LOGMTX2(ax, HTTP_HDR_RESPONSE,	Response),	\
	LOGMTX2(ax, HTTP_HDR_STATUS,	Status),	\
	LOGMTX2(ax, HTTP_HDR_URL,	URL),		\
	LOGMTX2(ax, HTTP_HDR_PROTO,	Protocol),	\
	LOGMTX2(ax, HTTP_HDR_FIRST,	Header),	\
	}

static enum shmlogtag logmtx[][HTTP_HDR_FIRST + 1] = {
	[HTTP_Rx] = LOGMTX1(Rx),
	[HTTP_Tx] = LOGMTX1(Tx),
	[HTTP_Obj] = LOGMTX1(Obj)
};
/*lint -restore */

static enum shmlogtag
http2shmlog(const struct http *hp, int t)
{

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	if (t > HTTP_HDR_FIRST)
		t = HTTP_HDR_FIRST;
	assert(hp->logtag >= HTTP_Rx && hp->logtag <= HTTP_Obj); /*lint !e685*/
	assert(t >= HTTP_HDR_REQ && t <= HTTP_HDR_FIRST);
	return (logmtx[hp->logtag][t]);
}

static void
WSLH(struct worker *w, int fd, const struct http *hp, unsigned hdr)
{

	WSLR(w, http2shmlog(hp, hdr), fd, hp->hd[hdr]);
}

/*--------------------------------------------------------------------*/
/* List of canonical HTTP response code names from RFC2616 */

static struct http_msg {
	unsigned	nbr;
	const char	*txt;
} http_msg[] = {
	{ 101, "Switching Protocols" },
	{ 200, "OK" },
	{ 201, "Created" },
	{ 202, "Accepted" },
	{ 203, "Non-Authoritative Information" },
	{ 204, "No Content" },
	{ 205, "Reset Content" },
	{ 206, "Partial Content" },
	{ 300, "Multiple Choices" },
	{ 301, "Moved Permanently" },
	{ 302, "Found" },
	{ 303, "See Other" },
	{ 304, "Not Modified" },
	{ 305, "Use Proxy" },
	{ 306, "(Unused)" },
	{ 307, "Temporary Redirect" },
	{ 400, "Bad Request" },
	{ 401, "Unauthorized" },
	{ 402, "Payment Required" },
	{ 403, "Forbidden" },
	{ 404, "Not Found" },
	{ 405, "Method Not Allowed" },
	{ 406, "Not Acceptable" },
	{ 407, "Proxy Authentication Required" },
	{ 408, "Request Timeout" },
	{ 409, "Conflict" },
	{ 410, "Gone" },
	{ 411, "Length Required" },
	{ 412, "Precondition Failed" },
	{ 413, "Request Entity Too Large" },
	{ 414, "Request-URI Too Long" },
	{ 415, "Unsupported Media Type" },
	{ 416, "Requested Range Not Satisfiable" },
	{ 417, "Expectation Failed" },
	{ 500, "Internal Server Error" },
	{ 501, "Not Implemented" },
	{ 502, "Bad Gateway" },
	{ 503, "Service Unavailable" },
	{ 504, "Gateway Timeout" },
	{ 505, "HTTP Version Not Supported" },
	{ 0, NULL }
};

const char *
http_StatusMessage(unsigned status)
{
	struct http_msg *mp;

	assert(status >= 100 && status <= 999);
	for (mp = http_msg; mp->nbr != 0 && mp->nbr <= status; mp++)
		if (mp->nbr == status)
			return (mp->txt);
	return ("Unknown Error");
}

/*--------------------------------------------------------------------*/

void
http_Setup(struct http *hp, struct ws *ws)
{

	memset(hp, 0, sizeof *hp);
	hp->magic = HTTP_MAGIC;
	hp->ws = ws;
	hp->nhd = HTTP_HDR_FIRST;
}

/*--------------------------------------------------------------------*/


static int
http_IsHdr(const txt *hh, const char *hdr)
{
	unsigned l;

	Tcheck(*hh);
	AN(hdr);
	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	return (!strncasecmp(hdr, hh->b, l));
}

/*--------------------------------------------------------------------*/

static unsigned
http_findhdr(const struct http *hp, unsigned l, const char *hdr)
{
	unsigned u;

	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		Tcheck(hp->hd[u]);
		if (hp->hd[u].e < hp->hd[u].b + l + 1)
			continue;
		if (hp->hd[u].b[l] != ':')
			continue;
		if (strncasecmp(hdr, hp->hd[u].b, l))
			continue;
		return (u);
	}
	return (0);
}

int
http_GetHdr(const struct http *hp, const char *hdr, char **ptr)
{
	unsigned u, l;
	char *p;

	l = hdr[0];
	diagnostic(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	u = http_findhdr(hp, l - 1, hdr);
	if (u == 0) {
		if (ptr != NULL)
			*ptr = NULL;
		return (0);
	}
	if (ptr != NULL) {
		p = hp->hd[u].b + l;
		while (vct_issp(*p))
			p++;
		*ptr = p;
	}
	return (1);
}

/*--------------------------------------------------------------------
 * Find a given headerfield, and if present and wanted, the beginning 
 * of its value.
 */

int
http_GetHdrField(const struct http *hp, const char *hdr, const char *field, char **ptr)
{
	char *h, *e;
	unsigned fl;

	if (ptr != NULL)
		*ptr = NULL;
	if (!http_GetHdr(hp, hdr, &h))
		return (0);
	AN(h);
	e = strchr(h, '\0');
	fl = strlen(field);
	while (h + fl <= e) {
		/* Skip leading separators */
		if (vct_issepctl(*h)) {
			h++;
			continue;
		}
		/* Check for substrings before memcmp() */
		if ((h + fl == e || vct_issepctl(h[fl])) &&
		    !memcmp(h, field, fl)) {
			/* got it */
			h += fl;
			if (ptr != NULL) {
				/* Skip whitespace, looking for '=' */
				while (*h && vct_issp(*h))
					h++;
				if (*h == '=') {
					h++;
					while (*h && vct_issp(*h))
						h++;
					*ptr = h;
				}
			}
			return (1);
		}
		/* Skip token */
		while (*h && !vct_issepctl(*h)) 
			h++;
	}
	return (0);
}

/*--------------------------------------------------------------------
 * XXX: redo with http_GetHdrField() ?
 */

const char *
http_DoConnection(struct http *hp)
{
	char *p, *q;
	const char *ret;
	unsigned u;

	if (!http_GetHdr(hp, H_Connection, &p)) {
		if (strcmp(hp->hd[HTTP_HDR_PROTO].b, "HTTP/1.1"))
			return ("not HTTP/1.1");
		return (NULL);
	}
	ret = NULL;
	for (; *p; p++) {
		if (vct_issp(*p))
			continue;
		if (*p == ',')
			continue;
		for (q = p + 1; *q; q++)
			if (*q == ',' || vct_issp(*q))
				break;
		u = pdiff(p, q);
		if (u == 5 && !strncasecmp(p, "close", u))
			ret = "Connection: close";
		u = http_findhdr(hp, u, p);
		if (u != 0)
			hp->hdf[u] |= HDF_FILTER;
		if (!*q)
			break;
		p = q;
	}
	return (ret);
}

/*--------------------------------------------------------------------*/

int
http_HdrIs(const struct http *hp, const char *hdr, const char *val)
{
	char *p;

	if (!http_GetHdr(hp, hdr, &p))
		return (0);
	AN(p);
	if (!strcasecmp(p, val))
		return (1);
	return (0);
}

/*--------------------------------------------------------------------*/

int
http_GetStatus(const struct http *hp)
{

	Tcheck(hp->hd[HTTP_HDR_STATUS]);
	return (hp->status);
}

const char *
http_GetProto(const struct http *hp)
{

	Tcheck(hp->hd[HTTP_HDR_PROTO]);
	return (hp->hd[HTTP_HDR_PROTO].b);
}

const char *
http_GetReq(const struct http *hp)
{

	Tcheck(hp->hd[HTTP_HDR_REQ]);
	return (hp->hd[HTTP_HDR_REQ].b);
}

/*--------------------------------------------------------------------
 * Dissect the headers of the HTTP protocol message.
 * Detect conditionals (headers which start with '^[Ii][Ff]-')
 */

static int
http_dissect_hdrs(struct worker *w, struct http *hp, int fd, char *p, txt t)
{
	char *q, *r;

	if (*p == '\r')
		p++;

	hp->nhd = HTTP_HDR_FIRST;
	hp->conds = 0;
	r = NULL;		/* For FlexeLint */
	for (; p < t.e; p = r) {
		/* XXX: handle continuation lines */
		q = strchr(p, '\n');
		assert(q != NULL);
		r = q + 1;
		if (q > p && q[-1] == '\r')
			q--;
		if (p == q)
			break;

		if ((p[0] == 'i' || p[0] == 'I') &&
		    (p[1] == 'f' || p[1] == 'F') &&
		    p[2] == '-')
			hp->conds = 1;

		while (q > p && vct_issp(q[-1]))
			q--;
		*q = '\0';

		if (hp->nhd < HTTP_HDR_MAX) {
			hp->hdf[hp->nhd] = 0;
			hp->hd[hp->nhd].b = p;
			hp->hd[hp->nhd].e = q;
			WSLH(w, fd, hp, hp->nhd);
			hp->nhd++;
		} else {
			VSL_stats->losthdr++;
			WSL(w, SLT_LostHeader, fd, "%.*s", q - p, p);
		}
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Deal with first line of HTTP protocol message.
 */

static int
http_splitline(struct worker *w, int fd, struct http *hp, const struct http_conn *htc, int h1, int h2, int h3)
{
	char *p;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	/* XXX: Assert a NUL at rx.e ? */
	Tcheck(htc->rxbuf);

	/* Skip leading LWS */
	for (p = htc->rxbuf.b ; vct_islws(*p); p++)
		continue;

	/* First field cannot contain SP, CRLF or CTL */
	hp->hd[h1].b = p;
	for (; !vct_issp(*p); p++)
		if (vct_isctl(*p))
			return (400);
	hp->hd[h1].e = p;

	/* Skip SP */
	for (; vct_issp(*p); p++)
		;

	/* Second field cannot contain LWS */
	hp->hd[h2].b = p;
	for (; !vct_islws(*p); p++)
		;
	hp->hd[h2].e = p;

	if (!Tlen(hp->hd[h2]))
		return (400);

	/* Skip SP */
	for (; vct_issp(*p); p++)
		;

	/* Third field is optional and cannot contain CTL */
	if (!vct_iscrlf(*p)) {
		hp->hd[h3].b = p;
		for (; !vct_iscrlf(*p); p++)
			if (vct_isctl(*p))
				return (400);
		hp->hd[h3].e = p;
	} else {
		hp->hd[h3].b = p;
		hp->hd[h3].e = p;
	}

	/* Skip CRLF */
	p += vct_skipcrlf(p);

	*hp->hd[h1].e = '\0';
	WSLH(w, fd, hp, h1);

	*hp->hd[h2].e = '\0';
	WSLH(w, fd, hp, h2);

	if (hp->hd[h3].e != NULL) {
		*hp->hd[h3].e = '\0';
		WSLH(w, fd, hp, h3);
	}

	return (http_dissect_hdrs(w, hp, fd, p, htc->rxbuf));
}

/*--------------------------------------------------------------------*/

int
http_DissectRequest(struct sess *sp)
{
	struct http_conn *htc;
	struct http *hp;
	int i;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	htc = sp->htc;
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	hp = sp->http;
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	hp->logtag = HTTP_Rx;

	i = http_splitline(sp->wrk, sp->fd, hp, htc,
	    HTTP_HDR_REQ, HTTP_HDR_URL, HTTP_HDR_PROTO);
	if (i != 0) {
		WSPR(sp, SLT_HttpGarbage, htc->rxbuf);
		return (i);
	}

	if (!strcmp(hp->hd[HTTP_HDR_PROTO].b, "HTTP/1.0"))
		hp->protover = 1.0;
	else if (!strcmp(hp->hd[HTTP_HDR_PROTO].b, "HTTP/1.1"))
		hp->protover = 1.1;
	else
		hp->protover = 0.9;
	return (i);
}

/*--------------------------------------------------------------------*/

int
http_DissectResponse(struct worker *w, const struct http_conn *htc, struct http *hp)
{
	int i;

	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	hp->logtag = HTTP_Rx;

	i = http_splitline(w, htc->fd, hp, htc,
	    HTTP_HDR_PROTO, HTTP_HDR_STATUS, HTTP_HDR_RESPONSE);

	if (i != 0 || memcmp(hp->hd[HTTP_HDR_PROTO].b, "HTTP/1.", 7))
		WSLR(w, SLT_HttpGarbage, htc->fd, htc->rxbuf);
	if (i != 0) {
		if (hp->status == 0)
			hp->status = i;
	} else {
		hp->status = 
		    strtoul(hp->hd[HTTP_HDR_STATUS].b, NULL /* XXX */, 10);
	}
	if (hp->hd[HTTP_HDR_RESPONSE].b == NULL ||
	    !Tlen(hp->hd[HTTP_HDR_RESPONSE])) {
		/* Backend didn't send a response string, use the standard */
		hp->hd[HTTP_HDR_RESPONSE].b = 
		    TRUST_ME(http_StatusMessage(hp->status));
		hp->hd[HTTP_HDR_RESPONSE].e =
		    strchr(hp->hd[HTTP_HDR_RESPONSE].b, '\0');
	}
	return (i);
}

/*--------------------------------------------------------------------*/

void
http_SetH(struct http *to, unsigned n, const char *fm)
{

	assert(n < HTTP_HDR_MAX);
	AN(fm);
	to->hd[n].b = TRUST_ME(fm);
	to->hd[n].e = strchr(to->hd[n].b, '\0');
	to->hdf[n] = 0;
}

static void
http_copyh(struct http *to, const struct http *fm, unsigned n)
{

	assert(n < HTTP_HDR_MAX);
	Tcheck(fm->hd[n]);
	to->hd[n] = fm->hd[n];
	to->hdf[n] = fm->hdf[n];
}

static void
http_copyreq(struct http *to, const struct http *fm, int how)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);

	if ((how == HTTPH_R_PIPE) || (how == HTTPH_R_PASS)) {
		http_copyh(to, fm, HTTP_HDR_REQ);
		http_copyh(to, fm, HTTP_HDR_PROTO);
	} else {
		http_SetH(to, HTTP_HDR_REQ, "GET");
		http_SetH(to, HTTP_HDR_PROTO, "HTTP/1.1");
	}
	http_copyh(to, fm, HTTP_HDR_URL);
}

void
http_ForceGet(struct http *to)
{
	if (strcmp(http_GetReq(to), "GET"))
		http_SetH(to, HTTP_HDR_REQ, "GET");
}

void
http_CopyResp(struct http *to, const struct http *fm)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (params->client_http11)
		http_SetH(to, HTTP_HDR_PROTO, "HTTP/1.1");
	else
		http_copyh(to, fm, HTTP_HDR_PROTO);
	http_copyh(to, fm, HTTP_HDR_STATUS);
	http_copyh(to, fm, HTTP_HDR_RESPONSE);
}

void
http_SetResp(struct http *to, const char *proto, const char *status, const char *response)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	http_SetH(to, HTTP_HDR_PROTO, proto);
	http_SetH(to, HTTP_HDR_STATUS, status);
	http_SetH(to, HTTP_HDR_RESPONSE, response);
}

static void
http_copyheader(struct worker *w, int fd, struct http *to, const struct http *fm, unsigned n)
{

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	assert(n < HTTP_HDR_MAX);
	Tcheck(fm->hd[n]);
	if (to->nhd < HTTP_HDR_MAX) {
		to->hd[to->nhd] = fm->hd[n];
		to->hdf[to->nhd] = 0;
		to->nhd++;
	} else  {
		VSL_stats->losthdr++;
		WSLR(w, SLT_LostHeader, fd, fm->hd[n]);
	}
}

/*--------------------------------------------------------------------*/

void
http_FilterFields(struct worker *w, int fd, struct http *to, const struct http *fm, unsigned how)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(fm, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	to->nhd = HTTP_HDR_FIRST;
	to->status = fm->status;
	for (u = HTTP_HDR_FIRST; u < fm->nhd; u++) {
		if (fm->hdf[u] & HDF_FILTER)
			continue;
#define HTTPH(a, b, c, d, e, f, g) \
		if (((e) & how) && http_IsHdr(&fm->hd[u], (b))) \
			continue;
#include "http_headers.h"
#undef HTTPH
		http_copyheader(w, fd, to, fm, u);
	}
}

/*--------------------------------------------------------------------*/

void
http_FilterHeader(struct sess *sp, unsigned how)
{
	struct bereq *bereq;
	struct http *hp;

        bereq = VBE_new_bereq();
        AN(bereq);
        hp = bereq->http;
        hp->logtag = HTTP_Tx;

	http_copyreq(hp, sp->http, how);
	http_FilterFields(sp->wrk, sp->fd, hp, sp->http, how);
	http_PrintfHeader(sp->wrk, sp->fd, hp, "X-Varnish: %u", sp->xid);
	http_PrintfHeader(sp->wrk, sp->fd, hp,
	    "X-Forwarded-For: %s", sp->addr);

	sp->bereq = bereq;
}

/*--------------------------------------------------------------------
 * This function copies any header fields which reference foreign
 * storage into our own WS.
 */

void
http_CopyHome(struct worker *w, int fd, struct http *hp)
{
	unsigned u, l;
	char *p;

	for (u = 0; u < hp->nhd; u++) {
		if (hp->hd[u].b == NULL)
			continue;
		if (hp->hd[u].b >= hp->ws->s && hp->hd[u].e <= hp->ws->e) {
			WSLH(w, fd, hp, u);
			continue;
		}
		l = Tlen(hp->hd[u]);
		p = WS_Alloc(hp->ws, l + 1);
		if (p != NULL) {
			WSLH(w, fd, hp, u);
			memcpy(p, hp->hd[u].b, l + 1);
			hp->hd[u].b = p;
			hp->hd[u].e = p + l;
		} else {
			WSLR(w, SLT_LostHeader, fd, hp->hd[u]);
			hp->hd[u].b = NULL;
			hp->hd[u].e = NULL;
		}
	}
}

/*--------------------------------------------------------------------*/

void
http_ClrHeader(struct http *to)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	/* XXX: don't to->f = to->v;  it would kill pipelining */
	to->nhd = HTTP_HDR_FIRST;
	memset(to->hd, 0, sizeof to->hd);
}

/*--------------------------------------------------------------------*/

void
http_SetHeader(struct worker *w, int fd, struct http *to, const char *hdr)
{

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	if (to->nhd >= HTTP_HDR_MAX) {
		VSL_stats->losthdr++;
		WSL(w, SLT_LostHeader, fd, "%s", hdr);
		return;
	}
	http_SetH(to, to->nhd++, hdr);
}

/*--------------------------------------------------------------------*/

static void
http_PutField(struct worker *w, int fd, struct http *to, int field, const char *string)
{
	char *p;
	unsigned l;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	l = strlen(string);
	p = WS_Alloc(to->ws, l + 1);
	if (p == NULL) {
		WSL(w, SLT_LostHeader, fd, "%s", string);
		to->hd[field].b = NULL;
		to->hd[field].e = NULL;
		to->hdf[field] = 0;
	} else {
		memcpy(p, string, l + 1);
		to->hd[field].b = p;
		to->hd[field].e = p + l;
		to->hdf[field] = 0;
	}
}

void
http_PutProtocol(struct worker *w, int fd, struct http *to, const char *protocol)
{

	http_PutField(w, fd, to, HTTP_HDR_PROTO, protocol);
}

void
http_PutStatus(struct worker *w, int fd, struct http *to, int status)
{
	char stat[4];

	assert(status >= 0 && status <= 999);
	sprintf(stat, "%d", status);
	http_PutField(w, fd, to, HTTP_HDR_STATUS, stat);
	to->status = status;
}

void
http_PutResponse(struct worker *w, int fd, struct http *to, const char *response)
{

	http_PutField(w, fd, to, HTTP_HDR_RESPONSE, response);
}

void
http_PrintfHeader(struct worker *w, int fd, struct http *to, const char *fmt, ...)
{
	va_list ap;
	unsigned l, n;

	CHECK_OBJ_NOTNULL(to, HTTP_MAGIC);
	l = WS_Reserve(to->ws, 0);
	va_start(ap, fmt);
	n = vsnprintf(to->ws->f, l, fmt, ap);
	va_end(ap);
	if (n + 1 >= l || to->nhd >= HTTP_HDR_MAX) {
		VSL_stats->losthdr++;
		WSL(w, SLT_LostHeader, fd, "%s", to->ws->f);
		WS_Release(to->ws, 0);
	} else {
		to->hd[to->nhd].b = to->ws->f;
		to->hd[to->nhd].e = to->ws->f + n;
		to->hdf[to->nhd] = 0;
		WS_Release(to->ws, n + 1);
		to->nhd++;
	}
}
/*--------------------------------------------------------------------*/

void
http_Unset(struct http *hp, const char *hdr)
{
	unsigned u, v;

	for (v = u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		if (http_IsHdr(&hp->hd[u], hdr)) 
			continue;
		if (v != u) {
			memcpy(&hp->hd[v], &hp->hd[u], sizeof hp->hd[v]);
			memcpy(&hp->hdf[v], &hp->hdf[u], sizeof hp->hdf[v]);
		}
		v++;
	}
	hp->nhd = v;
}

/*--------------------------------------------------------------------*/

unsigned
http_Write(struct worker *w, const struct http *hp, int resp)
{
	unsigned u, l;

	if (resp) {
		AN(hp->hd[HTTP_HDR_STATUS].b);
		l = WRK_WriteH(w, &hp->hd[HTTP_HDR_PROTO], " ");
		WSLH(w, *w->wfd, hp, HTTP_HDR_PROTO);
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_STATUS], " ");
		WSLH(w, *w->wfd, hp, HTTP_HDR_STATUS);
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_RESPONSE], "\r\n");
		WSLH(w, *w->wfd, hp, HTTP_HDR_RESPONSE);
	} else {
		AN(hp->hd[HTTP_HDR_URL].b);
		l = WRK_WriteH(w, &hp->hd[HTTP_HDR_REQ], " ");
		WSLH(w, *w->wfd, hp, HTTP_HDR_REQ);
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_URL], " ");
		WSLH(w, *w->wfd, hp, HTTP_HDR_URL);
		l += WRK_WriteH(w, &hp->hd[HTTP_HDR_PROTO], "\r\n");
		WSLH(w, *w->wfd, hp, HTTP_HDR_PROTO);
	}
	for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
		AN(hp->hd[u].b);
		AN(hp->hd[u].e);
		l += WRK_WriteH(w, &hp->hd[u], "\r\n");
		WSLH(w, *w->wfd, hp, u);
	}
	l += WRK_Write(w, "\r\n", -1);
	return (l);
}

/*--------------------------------------------------------------------*/

void
HTTP_Init(void)
{

#define HTTPH(a, b, c, d, e, f, g) b[0] = (char)strlen(b + 1);
#include "http_headers.h"
#undef HTTPH
}
