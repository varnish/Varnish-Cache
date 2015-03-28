/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2014 Varnish Software AS
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

#include "cache.h"
#include "cache_filter.h"

int
VDP_bytes(struct req *req, enum vdp_action act, const void *ptr, ssize_t len)
{
	int retval;
	struct vdp_entry *vdp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	assert(act == VDP_NULL || act == VDP_FLUSH);
	vdp = req->vdp_nxt;
	CHECK_OBJ_NOTNULL(vdp, VDP_ENTRY_MAGIC);
	req->vdp_nxt = VTAILQ_NEXT(vdp, list);

	assert(act > VDP_NULL || len > 0);
	/* Call the present layer, while pointing to the next layer down */
	retval = vdp->func(req, act, &vdp->priv, ptr, len);
	req->vdp_nxt = vdp;
	return (retval);
}

void
VDP_push(struct req *req, vdp_bytes *func, void *priv, int bottom)
{
	struct vdp_entry *vdp;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(func);

	vdp = WS_Alloc(req->ws, sizeof *vdp);
	AN(vdp);
	INIT_OBJ(vdp, VDP_ENTRY_MAGIC);
	vdp->func = func;
	vdp->priv = priv;
	if (bottom)
		VTAILQ_INSERT_TAIL(&req->vdp, vdp, list);
	else
		VTAILQ_INSERT_HEAD(&req->vdp, vdp, list);
	req->vdp_nxt = VTAILQ_FIRST(&req->vdp);

	AZ(vdp->func(req, VDP_INIT, &vdp->priv, NULL, 0));
}

static void
vdp_pop(struct req *req, vdp_bytes *func)
{
	struct vdp_entry *vdp;
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	vdp = VTAILQ_FIRST(&req->vdp);
	CHECK_OBJ_NOTNULL(vdp, VDP_ENTRY_MAGIC);
	assert(vdp->func == func);
	VTAILQ_REMOVE(&req->vdp, vdp, list);
	AZ(vdp->func(req, VDP_FINI, &vdp->priv, NULL, 0));
	AZ(vdp->priv);
	req->vdp_nxt = VTAILQ_FIRST(&req->vdp);
}

void
VDP_close(struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	while (!VTAILQ_EMPTY(&req->vdp))
		vdp_pop(req, VTAILQ_FIRST(&req->vdp)->func);
}

/*--------------------------------------------------------------------*/

enum objiter_status
VDP_DeliverObj(struct req *req)
{
	enum objiter_status ois;
	ssize_t len;
	void *oi;
	void *ptr;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (req->res_mode & RES_ESI) {
		ESI_Deliver(req);
		return (OIS_DONE);
	}

	oi = ObjIterBegin(req->wrk, req->objcore);
	XXXAN(oi);
	AZ(req->synth_body);

	do {
		ois = ObjIter(req->objcore, oi, &ptr, &len);
		switch(ois) {
		case OIS_DONE:
			AZ(len);
			break;
		case OIS_ERROR:
			break;
		case OIS_DATA:
		case OIS_STREAM:
			if (VDP_bytes(req,
			     ois == OIS_DATA ? VDP_NULL : VDP_FLUSH,  ptr, len))
				ois = OIS_ERROR;
			break;
		default:
			WRONG("Wrong OIS value");
		}
	} while (ois == OIS_DATA || ois == OIS_STREAM);
	ObjIterEnd(req->objcore, &oi);
	return (ois);
}

