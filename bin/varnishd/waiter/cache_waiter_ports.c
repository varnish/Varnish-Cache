/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006 Varnish Software AS
 * Copyright (c) 2007 OmniTI Computer Consulting, Inc.
 * Copyright (c) 2007 Theo Schlossnagle
 * Copyright (c) 2010-2012 UPLEX, Nils Goroll
 * All rights reserved.
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
 */

#include "config.h"

#if defined(HAVE_PORT_CREATE)

#include <sys/time.h>

#include <port.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache/cache.h"

#include "waiter/waiter.h"
#include "waiter/waiter_priv.h"
#include "vtim.h"

#define MAX_EVENTS 256

struct vws {
	unsigned		magic;
#define VWS_MAGIC		0x0b771473
	struct waiter		*waiter;

	pthread_t		thread;
	int			dport;
};

static inline void
vws_add(struct vws *vws, int fd, void *data)
{
	/*
	 * POLLIN should be all we need here
	 *
	 */
	AZ(port_associate(vws->dport, PORT_SOURCE_FD, fd, POLLIN, data));
}

static inline void
vws_del(struct vws *vws, int fd)
{
	port_dissociate(vws->dport, PORT_SOURCE_FD, fd);
}

static inline void
vws_port_ev(struct vws *vws, port_event_t *ev, double now) {
	struct waited *sp;
	if(ev->portev_source == PORT_SOURCE_USER) {
		CAST_OBJ_NOTNULL(sp, ev->portev_user, WAITED_MAGIC);
		assert(sp->fd >= 0);
		VTAILQ_INSERT_TAIL(&vws->waiter->waithead, sp, list);
		vws_add(vws, sp->fd, sp);
	} else {
		assert(ev->portev_source == PORT_SOURCE_FD);
		CAST_OBJ_NOTNULL(sp, ev->portev_user, WAITED_MAGIC);
		assert(sp->fd >= 0);
		if(ev->portev_events & POLLERR) {
			vws_del(vws, sp->fd);
			Wait_Handle(vws->waiter, sp, WAITER_REMCLOSE, now);
			return;
		}

		/*
		 * note: the original man page for port_associate(3C) states:
		 *
		 *    When an event for a PORT_SOURCE_FD object is retrieved,
		 *    the object no longer has an association with the port.
		 *
		 * This can be read along the lines of sparing the
		 * port_dissociate after port_getn(), but in fact,
		 * port_dissociate should be used
		 *
		 * Ref: http://opensolaris.org/jive/thread.jspa?\
		 *          threadID=129476&tstart=0
		 */
		vws_del(vws, sp->fd);
		Wait_Handle(vws->waiter, sp, WAITER_ACTION, now);
	}
	return;
}

static void *
vws_thread(void *priv)
{
	struct waited *sp;
	struct vws *vws;

	CAST_OBJ_NOTNULL(vws, priv, VWS_MAGIC);
	/*
	 * timeouts:
	 *
	 * min_ts : Minimum timeout for port_getn
	 * min_t  : ^ equivalent in floating point representation
	 *
	 * max_ts : Maximum timeout for port_getn
	 * max_t  : ^ equivalent in floating point representation
	 *
	 * with (nevents == 1), we should always choose the correct port_getn
	 * timeout to check session timeouts, so max is just a safety measure
	 * (if this implementation is correct, it could be set to an "infinte"
	 *  value)
	 *
	 * with (nevents > 1), min and max define the acceptable range for
	 * - additional latency of keep-alive connections and
	 * - additional tolerance for handling session timeouts
	 *
	 */
	static struct timespec min_ts = {0L, 100L * 1000L * 1000L /*ns*/};
	static double          min_t  = 0.1; /* 100    ms*/
	static struct timespec max_ts = {1L, 0L};		/* 1 second */
	static double	       max_t  = 1.0;			/* 1 second */

	/* XXX: These should probably go in vws ? */
	struct timespec ts;
	struct timespec *timeout;

	timeout = &max_ts;

	while (!vws->waiter->dismantle) {
		port_event_t ev[MAX_EVENTS];
		u_int nevents;
		int ei, ret;
		double now, idle;

		/*
		 * XXX Do we want to scale this up dynamically to increase
		 *     efficiency in high throughput situations? - would need to
		 *     start with one to keep latency low at any rate
		 *
		 *     Note: when increasing nevents, we must lower min_ts
		 *	     and max_ts
		 */
		nevents = 1;

		/*
		 * see disucssion in
		 * - https://issues.apache.org/bugzilla/show_bug.cgi?id=47645
		 * - http://mail.opensolaris.org/pipermail/\
		 *       networking-discuss/2009-August/011979.html
		 *
		 * comment from apr/poll/unix/port.c :
		 *
		 * This confusing API can return an event at the same time
		 * that it reports EINTR or ETIME.
		 *
		 */

		ret = port_getn(vws->dport, ev, MAX_EVENTS, &nevents, timeout);
		now = VTIM_real();

		if (ret < 0 && errno == EBADF) {
			/* Our stop signal */
			AN(vws->waiter->dismantle);
			break;
		}

		if (ret < 0)
			assert((errno == EINTR) || (errno == ETIME));

		for (ei = 0; ei < nevents; ei++)
			vws_port_ev(vws, ev + ei, now);

		/* check for timeouts */
		idle = now - *vws->waiter->tmo;

		/*
		 * This loop assumes that the oldest sessions are always at the
		 * beginning of the list (which is the case if we guarantee to
		 * enqueue at the tail only
		 *
		 */

		for (;;) {
			sp = VTAILQ_FIRST(&vws->waiter->waithead);
			if (sp == NULL)
				break;
			if (sp->idle > idle) {
				break;
			}
			vws_del(vws, sp->fd);
			Wait_Handle(vws->waiter, sp, WAITER_TIMEOUT, now);
		}

		/*
		 * Calculate the timeout for the next get_portn
		 */

		if (sp) {
			double tmo = (sp->idle + *vws->waiter->tmo) - now;

			if (tmo < min_t) {
				timeout = &min_ts;
			} else if (tmo > max_t) {
				timeout = &max_ts;
			} else {
				ts = VTIM_timespec(tmo);
				timeout = &ts;
			}
		} else {
			timeout = &max_ts;
		}
	}
	return(0);
}

/*--------------------------------------------------------------------*/

static int
vws_pass(void *priv, struct waited *sp)
{
	int r;
	struct vws *vws;

	CAST_OBJ_NOTNULL(vws, priv, VWS_MAGIC);
	r = port_send(vws->dport, 0, TRUST_ME(sp));
	if (r == -1 && errno == EAGAIN)
		return (-1);
	AZ(r);
	return (0);
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_init_f)
vws_init(struct waiter *w)
{
	struct vws *vws;

	CHECK_OBJ_NOTNULL(w, WAITER_MAGIC);
	vws = w->priv;
	INIT_OBJ(vws, VWS_MAGIC);
	vws->waiter = w;
	vws->dport = port_create();
	assert(vws->dport >= 0);

	AZ(pthread_create(&vws->thread, NULL, vws_thread, vws));
}

/*--------------------------------------------------------------------*/

static void __match_proto__(waiter_fini_f)
vws_fini(struct waiter *w)
{
	struct vws *vws;
	void *vp;

	CAST_OBJ_NOTNULL(vws, w->priv, VWS_MAGIC);
	AZ(close(vws->dport));
	AZ(pthread_join(vws->thread, &vp));
}

/*--------------------------------------------------------------------*/

const struct waiter_impl waiter_ports = {
	.name =		"ports",
	.init =		vws_init,
	.fini =		vws_fini,
	.pass =		vws_pass,
	.size =		sizeof(struct vws),
};

#endif /* defined(HAVE_PORT_CREATE) */
