/*-
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
 * Do Vary processing.
 *
 * When we insert an object into the cache which has a Vary: header,
 * we encode a vary matching string containing the headers mentioned
 * and their value.
 *
 * When we match an object in the cache, we check the present request
 * against the vary matching string.
 *
 * The only kind of header-munging we do is leading & trailing space
 * removal.  All the potential "q=foo" gymnastics is not worth the
 * effort.
 *
 * The vary matching string has the following format:
 *
 * Sequence of: {
 *	<length of header + 1>	\
 *	<header>		 \  Same format as argument to http_GetHdr()
 *	':'			 /
 *	'\0'			/
 *	<msb>			\   Length of header contents.
 *	<lsb>			/
 *      <header>		    Only present if length != 0xffff
 * }
 *      '\0'
 */

#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "cache.h"

void
VRY_Create(const struct sess *sp)
{
	char *v, *p, *q, *h, *e;
	struct vsb *sb, *sbh;
	int l;

	/* No Vary: header, no worries */
	if (!http_GetHdr(sp->obj->http, H_Vary, &v))
		return;

	/* For vary matching string */
	sb = vsb_newauto();
	AN(sb);

	/* For header matching strings */
	sbh = vsb_newauto();
	AN(sbh);

	for (p = v; *p; p++) {

		/* Find next header-name */
		if (isspace(*p))
			continue;
		for (q = p; *q && !isspace(*q) && *q != ','; q++)
			continue;

		/* Build a header-matching string out of it */
		vsb_clear(sbh);
		vsb_printf(sbh, "%c%.*s:%c", 1 + (q - p), q - p, p, 0);
		vsb_finish(sbh);
		AZ(vsb_overflowed(sbh));

		/* Append to vary matching string */
		vsb_bcat(sb, vsb_data(sbh), vsb_len(sbh));

		if (http_GetHdr(sp->http, vsb_data(sbh), &h)) {
			/* Trim leading and trailing space */
			while (isspace(*h))
				h++;
			e = strchr(h, '\0');
			while (e > h && isspace(e[-1]))
				e--;
			/* Encode two byte length and contents */
			l = e - h;
			assert(!(l & ~0xffff));
			vsb_printf(sb, "%c%c", (unsigned)l >> 8, l & 0xff);
			vsb_bcat(sb, h, e - h);
		} else {
			/* Mark as "not present" */
			vsb_printf(sb, "%c%c", 0xff, 0xff);
		}

		while (isspace(*q))
			q++;
		if (*q == '\0')
			break;
		xxxassert(*q == ',');
		p = q;
	}
	/* Terminate vary matching string */
	vsb_printf(sb, "%c", 0);

	vsb_finish(sb);
	AZ(vsb_overflowed(sb));
	l = vsb_len(sb);
	assert(l >= 0);
	sp->obj->vary = malloc(l);
	AN(sp->obj->vary);
	memcpy(sp->obj->vary, vsb_data(sb), l);

	vsb_delete(sb);
	vsb_delete(sbh);
}

int
VRY_Match(const struct sess *sp, const unsigned char *vary)
{
	char *h, *e;
	int i, l, lh;

	while (*vary) {

		/* Look for header */
		i = http_GetHdr(sp->http, (const char*)vary, &h);
		vary += *vary + 2;

		/* Expected length of header (or 0xffff) */
		l = vary[0] * 256 + vary[1];
		vary += 2;

		/* Fail if we have the header, but shouldn't */
		if (i && l == 0xffff)
			return (0);
		/* or if we don't when we should */
		if (l != 0xffff && !i)
			return (0);

		/* Nothing to match */
		if (!i)
			continue;

		/* Trim leading & trailing space */
		while (isspace(*h))
			h++;
		e = strchr(h, '\0');
		while (e > h && isspace(e[-1]))
			e--;

		/* Fail if wrong length */
		lh = e - h;
		if (lh != l)
			return (0);

		/* or if wrong contents */
		if (memcmp(h, vary, l))
			return (0);
		vary += l;
	}
	return (1);
}
