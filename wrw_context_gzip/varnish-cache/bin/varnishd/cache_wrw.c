/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
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
 * We maintain a number of worker thread pools, to spread lock contention.
 *
 * Pools can be added on the fly, as a means to mitigate lock contention,
 * but can only be removed again by a restart. (XXX: we could fix that)
 *
 * Two threads herd the pools, one eliminates idle threads and aggregates
 * statistics for all the pools, the other thread creates new threads
 * on demand, subject to various numerical constraints.
 *
 * The algorithm for when to create threads needs to be reactive enough
 * to handle startup spikes, but sufficiently attenuated to not cause
 * thread pileups.  This remains subject for improvement.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id: cache_pool.c 4069 2009-05-11 08:57:00Z phk $")

#include <sys/types.h>
#include <sys/uio.h>

#ifdef SENDFILE_WORKS
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/socket.h>
#elif defined(__linux__)
#include <sys/sendfile.h>
#elif defined(__sun)
#include <sys/sendfile.h>
#else
#error Unknown sendfile() implementation
#endif
#endif /* SENDFILE_WORKS */

#include "shmlog.h"
#include "cache.h"

/*--------------------------------------------------------------------
 * Write data to fd
 * We try to use writev() if possible in order to minimize number of
 * syscalls made and packets sent.  It also just might allow the worker
 * thread to complete the request without holding stuff locked.
 */

struct wrw_context *
WRW_New(struct sess *sp, int *fd)
{
	struct wrw_context *wrw;
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	wrw = (struct wrw_context *) WS_Alloc(sp->ws, (unsigned int) sizeof(struct wrw_context));
	wrw->magic = WRW_MAGIC;
	wrw->fd = fd;
	wrw->err = 0;
	wrw->liov = 0;
	wrw->niov = 0;
	
	return wrw;
}

void
WRW_Reserve(struct wrw_context *wrw, int *fd)
{

	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);
	AZ(wrw->fd);
	wrw->err = 0;
	wrw->liov = 0;
	wrw->niov = 0;
	wrw->fd = fd;
}

static void
WRW_Release(struct wrw_context *wrw)
{

	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);
	wrw->err = 0;
	wrw->liov = 0;
	wrw->niov = 0;
	wrw->fd = NULL;
}

unsigned
WRW_Flush(struct wrw_context *wrw, struct worker *w)
{
	ssize_t i;

	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);
	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(wrw->fd);
	if (*wrw->fd >= 0 && wrw->niov > 0 && wrw->err == 0) {
		i = writev(*wrw->fd, wrw->iov, wrw->niov);
		if (i != wrw->liov) {
			wrw->err++;
			WSL(w, SLT_Debug, *wrw->fd,
			    "Write error, len = %d/%d, errno = %s",
			    i, wrw->liov, strerror(errno));
		}
	}
	wrw->liov = 0;
	wrw->niov = 0;
	return (wrw->err);
}

unsigned
WRW_FlushRelease(struct wrw_context *wrw, struct worker *w)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);
	AN(wrw->fd);
	u = WRW_Flush(wrw, w);
	WRW_Release(wrw);
	return (u);
}

unsigned
WRW_WriteH(struct wrw_context *wrw, struct worker *w, const txt *hh, const char *suf)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);
	AN(wrw->fd);
	AN(wrw);
	AN(hh);
	AN(hh->b);
	AN(hh->e);
	u = WRW_Write(wrw, w, hh->b, hh->e - hh->b);
	if (suf != NULL)
		u += WRW_Write(wrw, w, suf, -1);
	return (u);
}

unsigned
WRW_Write(struct wrw_context *wrw, struct worker *w, const void *ptr, int len)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrw, WRW_MAGIC);

	AN(wrw->fd);
	if (len == 0 || *wrw->fd < 0)
		return (0);
	if (len == -1)
		len = strlen(ptr);
	if (wrw->niov == MAX_IOVS)
		(void)WRW_Flush(wrw, w);
	wrw->iov[wrw->niov].iov_base = TRUST_ME(ptr);
	wrw->iov[wrw->niov].iov_len = len;
	wrw->liov += len;
	wrw->niov++;
	return (len);
}

#ifdef SENDFILE_WORKS
void
WRW_Sendfile(struct worker *w, int fd, off_t off, unsigned len)
{

	CHECK_OBJ_NOTNULL(w, WORKER_MAGIC);
	AN(w->wfd);
	assert(fd >= 0);
	assert(len > 0);

#if defined(__FreeBSD__) || defined(__DragonFly__)
	do {
		struct sf_hdtr sfh;
		memset(&sfh, 0, sizeof sfh);
		if (w->niov > 0) {
			sfh.headers = w->iov;
			sfh.hdr_cnt = w->niov;
		}
		if (sendfile(fd, *w->wfd, off, len, &sfh, NULL, 0) != 0)
			w->werr++;
		w->liov = 0;
		w->niov = 0;
	} while (0);
#elif defined(__linux__)
	do {
		if (WRK_Flush(w) == 0 &&
		    sendfile(*w->wfd, fd, &off, len) != len)
			w->werr++;
	} while (0);
#elif defined(__sun) && defined(HAVE_SENDFILEV)
	do {
		sendfilevec_t svvec[HTTP_HDR_MAX * 2 + 1];
		size_t xferred = 0, expected = 0;
		int i;
		for (i = 0; i < w->niov; i++) {
			svvec[i].sfv_fd = SFV_FD_SELF;
			svvec[i].sfv_flag = 0;
			svvec[i].sfv_off = (off_t) w->iov[i].iov_base;
			svvec[i].sfv_len = w->iov[i].iov_len;
			expected += svvec[i].sfv_len;
		}
		svvec[i].sfv_fd = fd;
		svvec[i].sfv_flag = 0;
		svvec[i].sfv_off = off;
		svvec[i].sfv_len = len;
		expected += svvec[i].sfv_len;
		if (sendfilev(*w->wfd, svvec, i, &xferred) == -1 ||
		    xferred != expected)
			w->werr++;
		w->liov = 0;
		w->niov = 0;
	} while (0);
#elif defined(__sun) && defined(HAVE_SENDFILE)
	do {
		if (WRK_Flush(w) == 0 &&
		    sendfile(*w->wfd, fd, &off, len) != len)
			w->werr++;
	} while (0);
#else
#error Unknown sendfile() implementation
#endif
}
#endif /* SENDFILE_WORKS */

