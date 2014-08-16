/*-
 * Copyright (c) 2013 Varnish Software AS
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

#include <stdlib.h>

#include "cache.h"
#include "vend.h"
#include "storage/storage.h"
#include "hash/hash_slinger.h"

struct objiter {
	unsigned			magic;
#define OBJITER_MAGIC			0x745fb151
	struct busyobj			*bo;
	struct object			*obj;
	struct storage			*st;
	struct worker			*wrk;
	ssize_t				len;
};

struct objiter *
ObjIterBegin(struct worker *wrk, struct object *obj)
{
	struct objiter *oi;

	CHECK_OBJ_NOTNULL(obj, OBJECT_MAGIC);
	ALLOC_OBJ(oi, OBJITER_MAGIC);
	if (oi == NULL)
		return (oi);
	oi->obj = obj;
	oi->wrk = wrk;
	oi->bo = HSH_RefBusy(obj->objcore);
	return (oi);
}

enum objiter_status
ObjIter(struct objiter *oi, void **p, ssize_t *l)
{
	ssize_t ol;
	ssize_t nl;

	CHECK_OBJ_NOTNULL(oi, OBJITER_MAGIC);
	CHECK_OBJ_NOTNULL(oi->obj, OBJECT_MAGIC);
	AN(p);
	AN(l);
	*p = NULL;
	*l = 0;

	if (oi->bo == NULL) {
		if (oi->st == NULL)
			oi->st = VTAILQ_FIRST(&oi->obj->body->list);
		else
			oi->st = VTAILQ_NEXT(oi->st, list);
		while(oi->st != NULL && oi->st->len == 0)
			oi->st = VTAILQ_NEXT(oi->st, list);
		if (oi->st != NULL) {
			*p = oi->st->ptr;
			*l = oi->st->len;
			assert(*l > 0);
			return (OIS_DATA);
		}
		return (OIS_DONE);
	} else {
		ol = oi->len;
		while (1) {
			nl = VBO_waitlen(oi->bo, ol);
			if (nl != ol)
				break;
			if (oi->bo->state == BOS_FINISHED)
				return (OIS_DONE);
			if (oi->bo->state == BOS_FAILED)
				return (OIS_ERROR);
		}
		Lck_Lock(&oi->bo->mtx);
		AZ(VTAILQ_EMPTY(&oi->obj->body->list));
		VTAILQ_FOREACH(oi->st, &oi->obj->body->list, list) {
			if (oi->st->len > ol) {
				*p = oi->st->ptr + ol;
				*l = oi->st->len - ol;
				oi->len += *l;
				break;
			}
			ol -= oi->st->len;
			assert(ol >= 0);
			nl -= oi->st->len;
			assert(nl > 0);
		}
		CHECK_OBJ_NOTNULL(oi->obj, OBJECT_MAGIC);
		CHECK_OBJ_NOTNULL(oi->st, STORAGE_MAGIC);
		oi->st = VTAILQ_NEXT(oi->st, list);
		if (oi->st != NULL && oi->st->len == 0)
			oi->st = NULL;
		Lck_Unlock(&oi->bo->mtx);
		assert(*l > 0 || oi->bo->state == BOS_FINISHED);
		return (oi->st != NULL ? OIS_DATA : OIS_STREAM);
	}
}

void
ObjIterEnd(struct objiter **oi)
{

	AN(oi);
	CHECK_OBJ_NOTNULL((*oi), OBJITER_MAGIC);
	CHECK_OBJ_NOTNULL((*oi)->obj, OBJECT_MAGIC);
	if ((*oi)->bo != NULL) {
		if ((*oi)->obj->objcore->flags & OC_F_PASS)
			(*oi)->bo->abandon = 1;
		VBO_DerefBusyObj((*oi)->wrk, &(*oi)->bo);
	}
	FREE_OBJ((*oi));
	*oi = NULL;
}

static const struct objcore_methods *
obj_getmethods(const struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(oc->stevedore, STEVEDORE_MAGIC);
	AN(oc->stevedore->methods);
	return (oc->stevedore->methods);
}

void
ObjTrimStore(struct objcore *oc, struct dstat *ds)
{
	const struct stevedore *stv;
	struct storage *st;
	struct object *o;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(ds);
	stv = oc->stevedore;
	CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
	o = ObjGetObj(oc, ds);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	st = VTAILQ_LAST(&o->body->list, storagehead);
	if (st == NULL)
		return;
	if (st->len == 0) {
		VTAILQ_REMOVE(&o->body->list, st, list);
		STV_free(st);
	} else if (st->len < st->space) {
		STV_trim(st, st->len, 1);
	}
}

struct object *
ObjGetObj(struct objcore *oc, struct dstat *ds)
{
	const struct objcore_methods *m = obj_getmethods(oc);

	AN(ds);
	AN(m->getobj);
	return (m->getobj(ds, oc));
}

void
ObjUpdateMeta(struct objcore *oc, struct dstat *ds)
{
	const struct objcore_methods *m = obj_getmethods(oc);

	if (m->updatemeta != NULL)
		m->updatemeta(oc, ds);
}

void
ObjFreeObj(struct objcore *oc, struct dstat *ds)
{
	const struct objcore_methods *m = obj_getmethods(oc);

	AN(ds);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(m->freeobj);
	m->freeobj(ds, oc);
}

struct lru *
ObjGetLRU(const struct objcore *oc)
{
	const struct objcore_methods *m = obj_getmethods(oc);

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(m->getlru);
	return (m->getlru(oc));
}

void *
ObjGetattr(struct objcore *oc, struct dstat *ds, enum obj_attr attr,
   ssize_t *len)
{
	struct object *o;
	ssize_t dummy;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(ds);
	if (len == NULL)
		len = &dummy;
	o = ObjGetObj(oc, ds);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	switch (attr) {
	case OA_ESIDATA:
		if (o->esidata == NULL)
			return (NULL);
		*len = o->esidata->len;
		return (o->esidata->ptr);
	case OA_FLAGS:
		*len = sizeof o->oa_flags;
		return (o->oa_flags);
	case OA_GZIPBITS:
		*len = sizeof o->oa_gzipbits;
		return (o->oa_gzipbits);
	case OA_HEADERS:
		*len = 0;			// XXX: hack
		return (o->oa_http);
	case OA_LASTMODIFIED:
		*len = sizeof o->oa_lastmodified;
		return (o->oa_lastmodified);
	case OA_VARY:
		*len = 4;			// XXX: hack
		return (o->vary);
	case OA_VXID:
		*len = sizeof o->oa_vxid;
		return (o->oa_vxid);
	default:
		break;
	}
	WRONG("Unsupported OBJ_ATTR");
}

void *
ObjSetattr(const struct vfp_ctx *vc, enum obj_attr attr,
    ssize_t len)
{
	struct object *o;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vc->bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(vc->bo->fetch_objcore, OBJCORE_MAGIC);
	o = ObjGetObj(vc->bo->fetch_objcore, vc->bo->stats);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	switch (attr) {
	case OA_ESIDATA:
		o->esidata = STV_alloc(vc, len);
		if (o->esidata == NULL)
			return (NULL);
		o->esidata->len = len;
		return (o->esidata->ptr);
	case OA_FLAGS:
		assert(len == sizeof o->oa_flags);
		return (o->oa_flags);
	case OA_GZIPBITS:
		assert(len == sizeof o->oa_gzipbits);
		return (o->oa_gzipbits);
	case OA_LASTMODIFIED:
		assert(len == sizeof o->oa_lastmodified);
		return (o->oa_lastmodified);
	case OA_VXID:
		assert(len == sizeof o->oa_vxid);
		return (o->oa_vxid);
	default:
		break;
	}
	WRONG("Unsupported OBJ_ATTR");
}

int
ObjCopyAttr(const struct vfp_ctx *vc, struct objcore *ocs, enum obj_attr attr)
{
	void *vps, *vpd;
	ssize_t l;

	CHECK_OBJ_NOTNULL(vc, VFP_CTX_MAGIC);

	vps = ObjGetattr(ocs, vc->bo->stats, attr, &l);
	// XXX: later we want to have zero-length OA's too
	if (vps == NULL || l <= 0)
		return (-1);
	vpd = ObjSetattr(vc, attr, l);
	if (vpd == NULL)
		return (-1);
	memcpy(vpd, vps, l);
	return (0);
}

unsigned
ObjGetXID(struct objcore *oc, struct dstat *ds)
{
	uint32_t u;

	AZ(ObjGetU32(oc, ds, OA_VXID, &u));
	return (u);
}

uint64_t
ObjGetLen(struct objcore *oc, struct dstat *ds)
{
	struct object *o;

	o = ObjGetObj(oc, ds);
	CHECK_OBJ_NOTNULL(o, OBJECT_MAGIC);
	return (o->body->len);
}

/*--------------------------------------------------------------------
 * There is no well-defined byteorder for IEEE-754 double and the
 * correct solution (frexp(3) and manual encoding) is more work
 * than our (weak) goal of being endian-agnostic requires at this point.
 * We give it a shot by memcpy'ing doubles over a uint64_t and then
 * BE encode that.
 */

int
ObjSetDouble(const struct vfp_ctx *vc, enum obj_attr a, double t)
{
	void *vp;
	uint64_t u;

	assert(sizeof t == sizeof u);
	memcpy(&u, &t, sizeof u);
	vp = ObjSetattr(vc, a, sizeof u);
	if (vp == NULL)
		return (-1);
	vbe64enc(vp, u);
	return (0);
}

int
ObjGetDouble(struct objcore *oc, struct dstat *ds, enum obj_attr a, double *d)
{
	void *vp;
	uint64_t u;
	ssize_t l;

	assert(sizeof *d == sizeof u);
	vp = ObjGetattr(oc, ds, a, &l);
	if (vp == NULL)
		return (-1);
	if (d != NULL) {
		assert(l == sizeof u);
		u = vbe64dec(vp);
		memcpy(d, &u, sizeof *d);
	}
	return (0);
}

/*--------------------------------------------------------------------
 */

int
ObjSetU64(const struct vfp_ctx *vc, enum obj_attr a, uint64_t t)
{
	void *vp;

	vp = ObjSetattr(vc, a, sizeof t);
	if (vp == NULL)
		return (-1);
	vbe64enc(vp, t);
	return (0);
}

int
ObjGetU64(struct objcore *oc, struct dstat *ds, enum obj_attr a, uint64_t *d)
{
	void *vp;
	ssize_t l;

	vp = ObjGetattr(oc, ds, a, &l);
	if (vp == NULL || l != sizeof *d)
		return (-1);
	if (d != NULL)
		*d = vbe64dec(vp);
	return (0);
}

int
ObjSetU32(const struct vfp_ctx *vc, enum obj_attr a, uint32_t t)
{
	void *vp;

	vp = ObjSetattr(vc, a, sizeof t);
	if (vp == NULL)
		return (-1);
	vbe32enc(vp, t);
	return (0);
}

int
ObjGetU32(struct objcore *oc, struct dstat *ds, enum obj_attr a, uint32_t *d)
{
	void *vp;
	ssize_t l;

	vp = ObjGetattr(oc, ds, a, &l);
	if (vp == NULL || l != sizeof *d)
		return (-1);
	if (d != NULL)
		*d = vbe32dec(vp);
	return (0);
}

/*--------------------------------------------------------------------
 */

int
ObjCheckFlag(struct objcore *oc, struct dstat *ds, enum obj_flags of)
{
	uint8_t *fp;

	fp = ObjGetattr(oc, ds, OA_FLAGS, NULL);
	AN(fp);
	return ((*fp) & of);
}

void
ObjSetFlag(const struct vfp_ctx *vc, enum obj_flags of, int val)
{
	uint8_t *fp;

	fp = ObjSetattr(vc, OA_FLAGS, 1);
	AN(fp);
	if (val)
		(*fp) |= of;
	else
		(*fp) &= ~of;
}
