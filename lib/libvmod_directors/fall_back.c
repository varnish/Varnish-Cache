/*-
 * Copyright (c) 2013-2015 Varnish Software AS
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

#include <stdlib.h>

#include "cache/cache.h"
#include "cache/cache_director.h"

#include "vrt.h"
#include "vcc_if.h"

#include "vdir.h"

struct vmod_directors_fallback {
	unsigned				magic;
#define VMOD_DIRECTORS_FALLBACK_MAGIC		0xad4e26ba
	struct vdir				*vd;
};

static unsigned __match_proto__(vdi_healthy)
vmod_fallback_healthy(const struct director *dir, const struct busyobj *bo,
    double *changed)
{
	struct vmod_directors_fallback *rr;

	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DIRECTORS_FALLBACK_MAGIC);
	return (vdir_any_healthy(rr->vd, bo, changed));
}

static const struct director * __match_proto__(vdi_resolve_f)
vmod_fallback_resolve(const struct director *dir, struct worker *wrk,
    struct busyobj *bo)
{
	struct vmod_directors_fallback *rr;
	unsigned u;
	VCL_BACKEND be = NULL;

	CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CAST_OBJ_NOTNULL(rr, dir->priv, VMOD_DIRECTORS_FALLBACK_MAGIC);
	vdir_rdlock(rr->vd);
	for (u = 0; u < rr->vd->n_backend; u++) {
		be = rr->vd->backend[u];
		CHECK_OBJ_NOTNULL(be, DIRECTOR_MAGIC);
		if (be->healthy(be, bo, NULL))
			break;
	}
	vdir_unlock(rr->vd);
	if (u == rr->vd->n_backend)
		be = NULL;
	return (be);
}

VCL_VOID __match_proto__()
vmod_fallback__init(VRT_CTX,
    struct vmod_directors_fallback **rrp, const char *vcl_name)
{
	struct vmod_directors_fallback *rr;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(rrp);
	AZ(*rrp);
	ALLOC_OBJ(rr, VMOD_DIRECTORS_FALLBACK_MAGIC);
	AN(rr);
	*rrp = rr;
	vdir_new(&rr->vd, "fallback", vcl_name, vmod_fallback_healthy,
	    vmod_fallback_resolve, rr);
}

VCL_VOID __match_proto__()
vmod_fallback__fini(struct vmod_directors_fallback **rrp)
{
	struct vmod_directors_fallback *rr;

	rr = *rrp;
	*rrp = NULL;
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_FALLBACK_MAGIC);
	vdir_delete(&rr->vd);
	FREE_OBJ(rr);
}

VCL_VOID __match_proto__()
vmod_fallback_add_backend(VRT_CTX,
    struct vmod_directors_fallback *rr, VCL_BACKEND be)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_FALLBACK_MAGIC);
	(void)vdir_add_backend(rr->vd, be, 0.0);
}

VCL_VOID __match_proto__()
vmod_fallback_remove_backend(VRT_CTX,
    struct vmod_directors_fallback *fb, VCL_BACKEND be)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(fb, VMOD_DIRECTORS_FALLBACK_MAGIC);
	(void)vdir_remove_backend(fb->vd, be);
}

VCL_BACKEND __match_proto__()
vmod_fallback_backend(VRT_CTX,
    struct vmod_directors_fallback *rr)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(rr, VMOD_DIRECTORS_FALLBACK_MAGIC);
	return (rr->vd->dir);
}
