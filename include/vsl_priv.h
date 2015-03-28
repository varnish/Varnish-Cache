/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 * Define the layout of the shared memory log segment.
 *
 * NB: THIS IS NOT A PUBLIC API TO VARNISH!
 */

#ifndef VSL_PRIV_H_INCLUDED
#define VSL_PRIV_H_INCLUDED

#include "vapi/vsl_int.h"
#include "vapi/vsm_int.h"

#define VSL_CLASS		"Log"
#define VSL_SEGMENTS		8

/*
 * Shared memory log format
 *
 * The segments array has index values providing safe entry points into
 * the log, where each element N gives the index of the first log record
 * in the Nth fraction of the log. An index value of -1 indicated that no
 * log records in this fraction exists.
 *
 * The segment member shows the current segment where Varnish is currently
 * appending log data.
 *
 * The seq member contains a non-zero seq number randomly initialized,
 * which increases whenever writing the log starts from the front.
 *
 * The format of the actual log is in vapi/vsl_int.h
 *
 */

struct VSL_head {
#define VSL_HEAD_MARKER		"VSLHEAD0"	/* Incr. as version# */
	char			marker[VSM_MARKER_LEN];
	volatile ssize_t	segments[VSL_SEGMENTS];
	volatile unsigned	segment;	/* Current varnishd segment */
	volatile unsigned	seq;		/* Non-zero seq number */
	uint32_t		log[];
};

#endif /* VSL_PRIV_H_INCLUDED */
