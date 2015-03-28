/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vtc.h"

#include "vtim.h"

static pthread_mutex_t	vtclog_mtx;
static char		*vtclog_buf;
static unsigned		vtclog_left;

struct vtclog {
	unsigned	magic;
#define VTCLOG_MAGIC	0x82731202
	const char	*id;
	struct vsb	*vsb;
	pthread_mutex_t	mtx;
	int		act;
};

static pthread_key_t log_key;
static double t0;

/**********************************************************************/

void
vtc_loginit(char *buf, unsigned buflen)
{

	t0 = VTIM_mono();
	vtclog_buf = buf;
	vtclog_left = buflen;
	AZ(pthread_mutex_init(&vtclog_mtx, NULL));
	AZ(pthread_key_create(&log_key, NULL));
}

/**********************************************************************/


struct vtclog *
vtc_logopen(const char *id)
{
	struct vtclog *vl;

	ALLOC_OBJ(vl, VTCLOG_MAGIC);
	AN(vl);
	vl->id = id;
	vl->vsb = VSB_new_auto();
	AZ(pthread_mutex_init(&vl->mtx, NULL));
	AZ(pthread_setspecific(log_key, vl));
	return (vl);
}

void
vtc_logclose(struct vtclog *vl)
{

	CHECK_OBJ_NOTNULL(vl, VTCLOG_MAGIC);
	if (pthread_getspecific(log_key) == vl)
		AZ(pthread_setspecific(log_key, NULL));
	VSB_delete(vl->vsb);
	AZ(pthread_mutex_destroy(&vl->mtx));
	FREE_OBJ(vl);
}

static const char * const lead[] = {
	"----",
	"*   ",
	"**  ",
	"*** ",
	"****"
};

#define NLEAD (sizeof(lead)/sizeof(lead[0]))

static void
vtc_log_emit(const struct vtclog *vl, int lvl)
{
	int l;

	if (lvl < 0)
		lvl = 0;
	if (vtc_stop && lvl == 0)
		return;
	l = VSB_len(vl->vsb);
	AZ(pthread_mutex_lock(&vtclog_mtx));
	assert(vtclog_left > l);
	memcpy(vtclog_buf,VSB_data(vl->vsb), l);
	vtclog_buf += l;
	*vtclog_buf = '\0';
	vtclog_left -= l;
	AZ(pthread_mutex_unlock(&vtclog_mtx));
}

//lint -e{818}
void
vtc_log(struct vtclog *vl, int lvl, const char *fmt, ...)
{
	double tx;

	CHECK_OBJ_NOTNULL(vl, VTCLOG_MAGIC);
	tx = VTIM_mono() - t0;
	AZ(pthread_mutex_lock(&vl->mtx));
	vl->act = 1;
	assert(lvl < (int)NLEAD);
	VSB_clear(vl->vsb);
	VSB_printf(vl->vsb, "%s %-4s %4.1f ",
	    lead[lvl < 0 ? 1: lvl], vl->id, tx);
	va_list ap;
	va_start(ap, fmt);
	(void)VSB_vprintf(vl->vsb, fmt, ap);
	va_end(ap);
	VSB_putc(vl->vsb, '\n');
	AZ(VSB_finish(vl->vsb));

	vtc_log_emit(vl, lvl);

	VSB_clear(vl->vsb);
	vl->act = 0;
	AZ(pthread_mutex_unlock(&vl->mtx));
	if (lvl > 0)
		return;
	if (lvl == 0)
		vtc_error = 1;
	if (pthread_self() != vtc_thread)
		pthread_exit(NULL);
}

/**********************************************************************
 * Dump a string
 */

//lint -e{818}
void
vtc_dump(struct vtclog *vl, int lvl, const char *pfx, const char *str, int len)
{
	int nl = 1, olen;
	unsigned l;
	double tx;

	CHECK_OBJ_NOTNULL(vl, VTCLOG_MAGIC);
	tx = VTIM_mono() - t0;
	assert(lvl >= 0);
	assert(lvl < NLEAD);
	AZ(pthread_mutex_lock(&vl->mtx));
	vl->act = 1;
	VSB_clear(vl->vsb);
	if (pfx == NULL)
		pfx = "";
	if (str == NULL)
		VSB_printf(vl->vsb, "%s %-4s %4.1f %s(null)\n",
		    lead[lvl], vl->id, tx, pfx);
	else {
		olen = len;
		if (len < 0)
			len = strlen(str);
		for (l = 0; l < len; l++, str++) {
			if (l > 1024 && olen != -2) {
				VSB_printf(vl->vsb, "...");
				break;
			}
			if (nl) {
				VSB_printf(vl->vsb, "%s %-4s %4.1f %s| ",
				    lead[lvl], vl->id, tx, pfx);
				nl = 0;
			}
			if (*str == '\r')
				VSB_printf(vl->vsb, "\\r");
			else if (*str == '\t')
				VSB_printf(vl->vsb, "\\t");
			else if (*str == '\n') {
				VSB_printf(vl->vsb, "\\n\n");
				nl = 1;
			} else if (*str < 0x20 || *str > 0x7e)
				VSB_printf(vl->vsb, "\\x%02x", (*str) & 0xff);
			else
				VSB_printf(vl->vsb, "%c", *str);
		}
	}
	if (!nl)
		VSB_printf(vl->vsb, "\n");
	AZ(VSB_finish(vl->vsb));

	vtc_log_emit(vl, lvl);

	VSB_clear(vl->vsb);
	vl->act = 0;
	AZ(pthread_mutex_unlock(&vl->mtx));
	if (lvl == 0) {
		vtc_error = 1;
		if (pthread_self() != vtc_thread)
			pthread_exit(NULL);
	}
}

/**********************************************************************
 * Hexdump
 */

//lint -e{818}
void
vtc_hexdump(struct vtclog *vl, int lvl, const char *pfx,
    const unsigned char *str, int len)
{
	int nl = 1;
	unsigned l;
	double tx;

	CHECK_OBJ_NOTNULL(vl, VTCLOG_MAGIC);
	tx = VTIM_mono() - t0;
	assert(len >= 0);
	assert(lvl >= 0);
	assert(lvl < NLEAD);
	AZ(pthread_mutex_lock(&vl->mtx));
	vl->act = 1;
	VSB_clear(vl->vsb);
	if (pfx == NULL)
		pfx = "";
	if (str == NULL)
		VSB_printf(vl->vsb, "%s %-4s %4.1f %s| (null)",
		    lead[lvl], vl->id, tx, pfx);
	else {
		for (l = 0; l < len; l++, str++) {
			if (l > 512) {
				VSB_printf(vl->vsb, "...");
				break;
			}
			if (nl) {
				VSB_printf(vl->vsb, "%s %-4s %4.1f %s| ",
				    lead[lvl], vl->id, tx, pfx);
				nl = 0;
			}
			VSB_printf(vl->vsb, " %02x", *str);
			if ((l & 0xf) == 0xf) {
				VSB_printf(vl->vsb, "\n");
				nl = 1;
			}
		}
	}
	if (!nl)
		VSB_printf(vl->vsb, "\n");
	AZ(VSB_finish(vl->vsb));

	vtc_log_emit(vl, lvl);

	VSB_clear(vl->vsb);
	vl->act = 0;
	AZ(pthread_mutex_unlock(&vl->mtx));
	if (lvl == 0) {
		vtc_error = 1;
		if (pthread_self() != vtc_thread)
			pthread_exit(NULL);
	}
}

/**********************************************************************/

static void __attribute__((__noreturn__))
vtc_log_VAS_Fail(const char *func, const char *file, int line,
    const char *cond, enum vas_e why)
{
	struct vtclog *vl;

	(void)why;
	vl = pthread_getspecific(log_key);
	if (vl == NULL || vl->act) {
		fprintf(stderr,
		    "Assert error in %s(), %s line %d:\n"
		    "  Condition(%s) not true.\n",
		    func, file, line, cond);
	} else {
		vtc_log(vl, 0, "Assert error in %s(), %s line %d:"
		    "  Condition(%s) not true.\n", func, file, line, cond);
	}
	abort();
}

vas_f *VAS_Fail __attribute__((__noreturn__)) = vtc_log_VAS_Fail;
