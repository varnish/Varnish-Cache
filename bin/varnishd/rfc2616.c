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
 */

#include "config.h"

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include "shmlog.h"
#include "cache.h"


/*--------------------------------------------------------------------
 * TTL and Age calculation in Varnish
 *
 * RFC2616 has a lot to say about how caches should calculate the TTL
 * and expiry times of objects, but it sort of misses the case that
 * applies to Varnish:  the server-side cache.
 *
 * A normal cache, shared or single-client, has no symbiotic relationship
 * with the server, and therefore must take a very defensive attitude
 * if the Data/Expiry/Age/max-age data does not make sense.  Overall
 * the policy described in section 13 of RFC 2616 results in no caching
 * happening on the first little sign of trouble.
 *
 * Varnish on the other hand tries to offload as many transactions from
 * the backend as possible, and therefore just passing through everything
 * if there is a clock-skew between backend and Varnish is not a workable
 * choice.
 *
 * Varnish implements a policy which is RFC2616 compliant when there
 * is no clockskew, and falls as gracefully as possible otherwise.
 * Our "clockless cache" model is syntehsized from the bits of RFC2616
 * that talks about how a cache should react to a clockless origin server,
 * and more or less uses the inverse logic for the opposite relationship.
 *
 */

static double
RFC2616_Ttl(const struct sess *sp, const struct http *hp, struct object *obj)
{
	int ttl;
	unsigned max_age, age;
	double h_date, h_expires, ttd;
	char *p;

	assert(obj->entered != 0.0 && !isnan(sp->obj->entered));
	/* If all else fails, cache using default ttl */
	ttl = params->default_ttl;

	max_age = age = 0;
	ttd = 0;
	h_expires = 0;
	h_date = 0;

	do {	/* Allows us to break when we want out */

		/*
		 * First find any relative specification from the backend
		 * These take precedence according to RFC2616, 13.2.4
		 */

		if ((http_GetHdrField(hp, H_Cache_Control, "s-maxage", &p) ||
		    http_GetHdrField(hp, H_Cache_Control, "max-age", &p)) &&
		    p != NULL) {

			max_age = strtoul(p, NULL, 0);
			if (http_GetHdr(hp, H_Age, &p)) {
				age = strtoul(p, NULL, 0);
				obj->age = age;
			}

			if (age > max_age)
				ttl = 0;
			else
				ttl = max_age - age;
			break;
		}

		/* Next look for absolute specifications from backend */

		if (http_GetHdr(hp, H_Expires, &p))
			h_expires = TIM_parse(p);
		if (h_expires == 0)
			break;

		if (http_GetHdr(hp, H_Date, &p))
			h_date = TIM_parse(p);

		/* If backend told us it is expired already, don't cache. */
		if (h_expires < h_date) {
			ttl = 0;
			break;
		}

		if (h_date == 0 ||
		    (h_date < obj->entered + params->clock_skew &&
		    h_date + params->clock_skew > obj->entered)) {
			/*
			 * If we have no Date: header or if it is
			 * sufficiently close to our clock we will
			 * trust Expires: relative to our own clock.
			 */
			if (h_expires < obj->entered)
				ttl = 0;
			else
				ttd = h_expires;
			break;
		}

		/*
		 * But even if the clocks are out of whack we can still
		 * derive a relative time from the two headers.
		 * (the negative ttl case is caught above)
		 */
		ttl = (h_expires - h_date);

	} while (0);

	if (ttl > 0 && ttd == 0)
		ttd = obj->entered + ttl;

	/* calculated TTL, Our time, Date, Expires, max-age, age */
	WSP(sp, SLT_TTL, "%u RFC %d %d %d %d %u %u", sp->xid,
	    ttd ? (int)(ttd - obj->entered) : 0,
	    (int)obj->entered, (int)h_date,
	    (int)h_expires, max_age, age);

	return (ttd);
}

/*
 * We could move this policy to vcl_fetch{} now but I have decided to leave
 * it here for the POLA principle.  It is not credible to think that a
 * majority of our uses will change the cacheability decision, so moving
 * it to VCL would just make the average and median vcl_fetch{} implementation
 * harder for people to write.  Instead the minority who want to override
 * the RFC2616 mandated behaviour, can do so in their vcl_fetch{}
 */
int
RFC2616_cache_policy(const struct sess *sp, const struct http *hp)
{
	int body = 0;

	/*
	 * Initial cacheability determination per [RFC2616, 13.4]
	 * We do not support ranges yet, so 206 is out.
	 */
	sp->obj->response = http_GetStatus(hp);
	switch (sp->obj->response) {
	case 200: /* OK */
	case 203: /* Non-Authoritative Information */
	case 300: /* Multiple Choices */
	case 301: /* Moved Permanently */
	case 302: /* Moved Temporarily */
	case 410: /* Gone */
	case 404: /* Not Found */
		sp->obj->cacheable = 1;
		body = 1;
		break;
	default:
		sp->obj->cacheable = 0;
		body = 0;
		break;
	}

	sp->obj->ttl = RFC2616_Ttl(sp, hp, sp->obj);
	if (sp->obj->ttl == 0)
		sp->obj->cacheable = 0;

	return (body);
}

