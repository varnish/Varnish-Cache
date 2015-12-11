/*-
 * Copyright (c) 2012-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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
#include <stdlib.h>
#include <stdio.h>

#include "cache/cache.h"

#include "vcl.h"
#include "vrt.h"
#include "vsb.h"
#include "vtim.h"
#include "vcc_if.h"

struct priv_vcl {
	unsigned		magic;
#define PRIV_VCL_MAGIC		0x8E62FA9D
	char			*foo;
	uintptr_t		exp_cb;
};

VCL_DURATION vcl_release_delay = 0.0;

VCL_VOID __match_proto__(td_debug_panic)
vmod_panic(VRT_CTX, const char *str, ...)
{
	va_list ap;
	const char *b;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	va_start(ap, str);
	b = VRT_String(ctx->ws, "PANIC: ", str, ap);
	va_end(ap);
	VAS_Fail("VCL", "", 0, b, VAS_VCL);
}

VCL_STRING __match_proto__(td_debug_author)
vmod_author(VRT_CTX, VCL_ENUM id)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (!strcmp(id, "phk"))
		return ("Poul-Henning");
	if (!strcmp(id, "des"))
		return ("Dag-Erling");
	if (!strcmp(id, "kristian"))
		return ("Kristian");
	if (!strcmp(id, "mithrandir"))
		return ("Tollef");
	WRONG("Illegal VMOD enum");
}

VCL_VOID __match_proto__(td_debug_test_priv_call)
vmod_test_priv_call(VRT_CTX, struct vmod_priv *priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup("BAR");
		priv->free = free;
	} else {
		assert(!strcmp(priv->priv, "BAR"));
	}
}

VCL_STRING __match_proto__(td_debug_test_priv_task)
vmod_test_priv_task(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup(s);
		priv->free = free;
	}
	return (priv->priv);
}

VCL_STRING __match_proto__(td_debug_test_priv_top)
vmod_test_priv_top(VRT_CTX, struct vmod_priv *priv, VCL_STRING s)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	if (priv->priv == NULL) {
		priv->priv = strdup(s);
		priv->free = free;
	}
	return (priv->priv);
}

VCL_VOID __match_proto__(td_debug_test_priv_vcl)
vmod_test_priv_vcl(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(priv);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AN(priv_vcl->foo);
	assert(!strcmp(priv_vcl->foo, "FOO"));
}

VCL_BLOB
vmod_str2blob(VRT_CTX, VCL_STRING s)
{
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	p = (void*)WS_Alloc(ctx->ws, sizeof *p);
	AN(p);
	memset(p, 0, sizeof *p);
	p->len = strlen(s);
	p->priv = WS_Copy(ctx->ws, s, -1);
	return (p);
}

VCL_STRING
vmod_blob2hex(VRT_CTX, VCL_BLOB b)
{
	char *s, *p;
	uint8_t *q;
	int i;

	s = WS_Alloc(ctx->ws, b->len * 2 + 2);
	AN(s);
	p = s;
	q = b->priv;
	for (i = 0; i < b->len; i++) {
		sprintf(p, "%02x", *q);
		p += 2;
		q += 1;
	}
	VRT_priv_fini(b);
	return (s);
}

VCL_BACKEND
vmod_no_backend(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	return (NULL);
}

VCL_VOID __match_proto__(td_debug_rot52)
vmod_rot52(VRT_CTX, VCL_HTTP hp)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);

	http_PrintfHeader(hp, "Encrypted: ROT52");
}

VCL_STRING
vmod_argtest(VRT_CTX, VCL_STRING one, VCL_REAL two, VCL_STRING three,
    VCL_STRING comma)
{
	char buf[100];

	bprintf(buf, "%s %g %s %s", one, two, three, comma);
	return WS_Copy(ctx->ws, buf, -1);
}

VCL_INT
vmod_vre_limit(VRT_CTX)
{
	(void)ctx;
	return (cache_param->vre_limits.match);
}

static void __match_proto__(exp_callback_f)
exp_cb(struct worker *wrk, struct objcore *oc, enum exp_event_e ev, void *priv)
{
	const struct priv_vcl *priv_vcl;
	const char *what;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(priv_vcl, priv, PRIV_VCL_MAGIC);
	switch (ev) {
	case EXP_INSERT: what = "insert"; break;
	case EXP_INJECT: what = "inject"; break;
	case EXP_REMOVE: what = "remove"; break;
	default: WRONG("Wrong exp_event");
	}
	VSL(SLT_Debug, 0, "exp_cb: event %s 0x%jx", what,
	    (intmax_t)(uintptr_t)oc);
}

VCL_VOID __match_proto__()
vmod_register_exp_callback(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ_NOTNULL(priv_vcl, priv->priv, PRIV_VCL_MAGIC);
	AZ(priv_vcl->exp_cb);
	priv_vcl->exp_cb = EXP_Register_Callback(exp_cb, priv_vcl);
	VSL(SLT_Debug, 0, "exp_cb: registered");
}

VCL_VOID __match_proto__()
vmod_init_fail(VRT_CTX)
{

	AN(ctx->msg);
	VSB_printf(ctx->msg, "Planned failure in vcl_init{}");
	VRT_handling(ctx, VCL_RET_FAIL);
}

static void __match_proto__(vmod_priv_free_f)
priv_vcl_free(void *priv)
{
	struct priv_vcl *priv_vcl;

	CAST_OBJ_NOTNULL(priv_vcl, priv, PRIV_VCL_MAGIC);
	AN(priv_vcl->foo);
	free(priv_vcl->foo);
	if (priv_vcl->exp_cb != 0) {
		EXP_Deregister_Callback(&priv_vcl->exp_cb);
		VSL(SLT_Debug, 0, "exp_cb: deregistered");
	}
	FREE_OBJ(priv_vcl);
	AZ(priv_vcl);
}

static int
event_load(VRT_CTX, struct vmod_priv *priv)
{
	struct priv_vcl *priv_vcl;

	AN(ctx->msg);
	if (cache_param->nuke_limit == 42) {
		VSB_printf(ctx->msg, "nuke_limit is not the answer.");
		return (-1);
	}

	ALLOC_OBJ(priv_vcl, PRIV_VCL_MAGIC);
	AN(priv_vcl);
	priv_vcl->foo = strdup("FOO");
	AN(priv_vcl->foo);
	priv->priv = priv_vcl;
	priv->free = priv_vcl_free;
	return (0);
}

static int
event_warm(VRT_CTX)
{

	VSL(SLT_Debug, 0, "%s: VCL_EVENT_WARM", VCL_Name(ctx->vcl));

	if (cache_param->max_esi_depth == 42) {
		VSB_printf(ctx->msg, "max_esi_depth is not the answer.");
		return (-1);
	}

	VRT_ref_vcl(ctx);
	return (0);
}

static void*
cooldown_thread(void *priv)
{
	struct vrt_ctx ctx;

	AN(priv);
	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	ctx.vcl = (struct vcl*)priv;

	VTIM_sleep(vcl_release_delay);
	VRT_rel_vcl(&ctx);
	return (NULL);
}

static int
event_cold(VRT_CTX)
{
	pthread_t thread;

	VSL(SLT_Debug, 0, "%s: VCL_EVENT_COLD", VCL_Name(ctx->vcl));

	if (vcl_release_delay == 0.0) {
		VRT_rel_vcl(ctx);
		return (0);
	}

	AZ(pthread_create(&thread, NULL, cooldown_thread, ctx->vcl));
	AZ(pthread_detach(thread));
	return (0);
}

int __match_proto__(vmod_event_f)
event_function(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{

	switch (e) {
		case VCL_EVENT_LOAD: return event_load(ctx, priv);
		case VCL_EVENT_COLD: return event_cold(ctx);
		case VCL_EVENT_WARM: return event_warm(ctx);
		default: return (0);
	}
}

VCL_VOID __match_proto__(td_debug_sleep)
vmod_sleep(VRT_CTX, VCL_DURATION t)
{

	CHECK_OBJ_ORNULL(ctx, VRT_CTX_MAGIC);
	VTIM_sleep(t);
}

static struct ws *wsfind(VRT_CTX, VCL_ENUM which) {
	if (!strcmp(which, "client")) {
		return ctx->ws;
	} else if (!strcmp(which, "backend")) {
		return ctx->bo->ws;
	} else if (!strcmp(which, "session")) {
		return ctx->req->sp->ws;
	} else if (!strcmp(which, "thread")) {
		return ctx->req->wrk->aws;
	} else
		WRONG("No such workspace.");
}

void
vmod_workspace_allocate(VRT_CTX, VCL_ENUM which, VCL_INT size)
{
	struct ws *ws;
	char *s;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);

	WS_Assert(ws);
	AZ(ws->r);

	s = WS_Alloc(ws, size);
	if (!s)
		return;
	memset(s, '\0', size);
}

VCL_INT
vmod_workspace_free(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);

	WS_Assert(ws);
	AZ(ws->r);

	return pdiff(ws->f, ws->e);
}

VCL_BOOL
vmod_workspace_overflowed(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);
	WS_Assert(ws);

	return (WS_Overflowed(ws));
}

void
vmod_workspace_overflow(VRT_CTX, VCL_ENUM which)
{
	struct ws *ws;
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	ws = wsfind(ctx, which);
	WS_Assert(ws);

	WS_MarkOverflow(ws);
}

void
vmod_vcl_release_delay(VRT_CTX, VCL_DURATION delay)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	assert(delay > 0.0);
	vcl_release_delay = delay;
}
