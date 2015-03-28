/*-
 * Copyright (c) 2010 Varnish Software AS
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

#include <pthread.h>
#include <unistd.h>

#include "vas.h"
#include "vmb.h"

#ifdef VMB_NEEDS_PTHREAD_WORKAROUND_THIS_IS_BAD_FOR_PERFORMANCE

static pthread_mutex_t	mb_mtx;
static pthread_once_t	mb_mtx_once = PTHREAD_ONCE_INIT;

static void
vmb_init(void)
{

	AZ(pthread_mutex_init(&mb_mtx, NULL));
}


void
vmb_pthread(void)
{

	AZ(pthread_once(&mb_mtx_once, vmb_init));

	AZ(pthread_mutex_lock(&mb_mtx));
	AZ(pthread_mutex_unlock(&mb_mtx));
}

#endif /* VMB_NEEDS_PTHREAD_WORKAROUND_THIS_IS_BAD_FOR_PERFORMANCE */
