/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * LRU and object timer handling.
 *
 */

#include "config.h"

#include <stdlib.h>

#include "cache.h"

#include "binary_heap.h"
#include "hash/hash_slinger.h"
#include "vtim.h"

struct exp_callback {
	unsigned			magic;
#define EXP_CALLBACK_MAGIC		0xab956eb1
	exp_callback_f			*func;
	void				*priv;
	VTAILQ_ENTRY(exp_callback)	list;
};

struct exp_priv {
	unsigned			magic;
#define EXP_PRIV_MAGIC			0x9db22482
	struct lock			mtx;

	struct worker			*wrk;
	struct vsl_log			vsl;

	VTAILQ_HEAD(,objcore)		inbox;
	struct binheap			*heap;
	pthread_cond_t			condvar;

	VTAILQ_HEAD(,exp_callback)	ecb_list;
	pthread_rwlock_t		cb_rwl;
};

static struct exp_priv *exphdl;

static void
exp_event(struct worker *wrk, struct objcore *oc, enum exp_event_e e)
{
	struct exp_callback *cb;

	/*
	 * Strictly speaking this is not atomic, but neither is VMOD
	 * loading in general, so this is a fair optimization
	 */
	if (VTAILQ_EMPTY(&exphdl->ecb_list))
		return;

	AZ(pthread_rwlock_rdlock(&exphdl->cb_rwl));
	VTAILQ_FOREACH(cb, &exphdl->ecb_list, list) {
		CHECK_OBJ_NOTNULL(cb, EXP_CALLBACK_MAGIC);
		cb->func(wrk, oc, e, cb->priv);
	}
	AZ(pthread_rwlock_unlock(&exphdl->cb_rwl));
}

/*--------------------------------------------------------------------
 * struct exp manipulations
 */

void
EXP_Clr(struct exp *e)
{

	e->ttl = -1;
	e->grace = 0;
	e->keep = 0;
	e->t_origin = 0;
}

/*--------------------------------------------------------------------
 * Calculate an objects effective ttl time, taking req.ttl into account
 * if it is available.
 */

double
EXP_Ttl(const struct req *req, const struct exp *e)
{
	double r;

	r = e->ttl;
	if (req != NULL && req->d_ttl > 0. && req->d_ttl < r)
		r = req->d_ttl;
	return (e->t_origin + r);
}

/*--------------------------------------------------------------------
 * Calculate when this object is no longer useful
 */

double
EXP_When(const struct exp *e)
{
	double when;

	if (e->t_origin == 0)
		return (0.);
	when = e->t_origin + e->ttl + e->grace + e->keep;
	AZ(isnan(when));
	return (when);
}

/*--------------------------------------------------------------------
 * Post an objcore to the exp_thread's inbox.
 */

static void
exp_mail_it(struct objcore *oc)
{
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AN(oc->exp_flags & OC_EF_OFFLRU);
	Lck_Lock(&exphdl->mtx);
	if (oc->exp_flags & OC_EF_DYING)
		VTAILQ_INSERT_HEAD(&exphdl->inbox, oc, lru_list);
	else
		VTAILQ_INSERT_TAIL(&exphdl->inbox, oc, lru_list);
	VSC_C_main->exp_mailed++;
	AZ(pthread_cond_signal(&exphdl->condvar));
	Lck_Unlock(&exphdl->mtx);
}

/*--------------------------------------------------------------------
 * Inject an object with a reference into the lru/binheap.
 *
 * This can either come from a stevedore (persistent) during startup
 * or from EXP_Insert() below.
 */

void
EXP_Inject(struct worker *wrk, struct objcore *oc, struct lru *lru)
{

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	AZ(oc->exp_flags & (OC_EF_OFFLRU | OC_EF_INSERT | OC_EF_MOVE));
	AZ(oc->exp_flags & OC_EF_DYING);
	AZ(oc->flags & OC_F_BUSY);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	Lck_Lock(&lru->mtx);
	lru->n_objcore++;
	oc->exp_flags |= OC_EF_OFFLRU | OC_EF_INSERT | OC_EF_EXP;
	oc->timer_when = EXP_When(&oc->exp);
	Lck_Unlock(&lru->mtx);

	exp_event(wrk, oc, EXP_INJECT);

	exp_mail_it(oc);
}

/*--------------------------------------------------------------------
 * Insert new object.
 *
 * We grab a reference to the object, which will keep it around until
 * we decide its time to let it go.
 */

void
EXP_Insert(struct worker *wrk, struct objcore *oc)
{
	struct lru *lru;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	HSH_Ref(oc);

	AZ(oc->exp_flags & (OC_EF_OFFLRU | OC_EF_INSERT | OC_EF_MOVE));
	AZ(oc->exp_flags & OC_EF_DYING);
	AN(oc->flags & OC_F_BUSY);

	lru = ObjGetLRU(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	Lck_Lock(&lru->mtx);
	lru->n_objcore++;
	oc->exp_flags |= OC_EF_OFFLRU | OC_EF_INSERT | OC_EF_EXP;
	oc->exp_flags |= OC_EF_MOVE;
	Lck_Unlock(&lru->mtx);

	exp_event(wrk, oc, EXP_INSERT);

	exp_mail_it(oc);
}

/*--------------------------------------------------------------------
 * Object was used, move to tail of LRU list.
 *
 * To avoid the exphdl->mtx becoming a hotspot, we only attempt to move
 * objects if they have not been moved recently and if the lock is available.
 * This optimization obviously leaves the LRU list imperfectly sorted.
 */

void
EXP_Touch(struct objcore *oc, double now)
{
	struct lru *lru;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	if (oc->busyobj != NULL)
		return;

	if (now - oc->last_lru < cache_param->lru_interval)
		return;

	lru = ObjGetLRU(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	/*
	 * For -spersistent (and possibly other stevedores, we don't move
	 * objects on the lru list, since LRU doesn't really help much.
	 */
	if (lru->flags & LRU_F_DONTMOVE)
		return;

	if (Lck_Trylock(&lru->mtx))
		return;

	AN(oc->exp_flags & OC_EF_EXP);

	if (!(oc->exp_flags & OC_EF_OFFLRU)) {
		/* Can only touch it while it's actually on the LRU list */
		VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
		VTAILQ_INSERT_TAIL(&lru->lru_head, oc, lru_list);
		VSC_C_main->n_lru_moved++;
	}
	oc->last_lru = now;
	Lck_Unlock(&lru->mtx);
}

/*--------------------------------------------------------------------
 * We have changed one or more of the object timers, tell the exp_thread
 *
 */

void
EXP_Rearm(struct objcore *oc, double now, double ttl, double grace, double keep)
{
	struct lru *lru;
	double when;

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	assert(oc->refcnt > 0);

	AN(oc->exp_flags & OC_EF_EXP);

	if (!isnan(ttl))
		oc->exp.ttl = now + ttl - oc->exp.t_origin;
	if (!isnan(grace))
		oc->exp.grace = grace;
	if (!isnan(keep))
		oc->exp.keep = keep;

	when = EXP_When(&oc->exp);

	VSL(SLT_ExpKill, 0, "EXP_Rearm p=%p E=%.9f e=%.9f f=0x%x", oc,
	    oc->timer_when, when, oc->flags);

	if (when > oc->exp.t_origin && when > oc->timer_when)
		return;

	lru = ObjGetLRU(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	Lck_Lock(&lru->mtx);

	if (!isnan(now) && when <= now)
		oc->exp_flags |= OC_EF_DYING;
	else
		oc->exp_flags |= OC_EF_MOVE;

	if (oc->exp_flags & OC_EF_OFFLRU) {
		oc = NULL;
	} else {
		oc->exp_flags |= OC_EF_OFFLRU;
		VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
	}
	Lck_Unlock(&lru->mtx);

	if (oc != NULL)
		exp_mail_it(oc);
}

/*--------------------------------------------------------------------
 * Attempt to make space by nuking the oldest object on the LRU list
 * which isn't in use.
 * Returns: 1: did, 0: didn't, -1: can't
 */

int
EXP_NukeOne(struct worker *wrk, struct lru *lru)
{
	struct objcore *oc, *oc2;
	struct objhead *oh;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);
	/* Find the first currently unused object on the LRU.  */
	Lck_Lock(&lru->mtx);
	VTAILQ_FOREACH_SAFE(oc, &lru->lru_head, lru_list, oc2) {
		CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

		VSLb(wrk->vsl, SLT_ExpKill, "LRU_Cand p=%p f=0x%x r=%d",
		    oc, oc->flags, oc->refcnt);

		AZ(oc->exp_flags & OC_EF_OFFLRU);
		AZ(oc->exp_flags & OC_EF_DYING);

		/*
		 * It wont release any space if we cannot release the last
		 * reference, besides, if somebody else has a reference,
		 * it's a bad idea to nuke this object anyway.
		 */
		if (oc->refcnt > 1)
			continue;
		oh = oc->objhead;
		CHECK_OBJ_NOTNULL(oh, OBJHEAD_MAGIC);
		if (Lck_Trylock(&oh->mtx))
			continue;
		if (oc->refcnt == 1) {
			oc->exp_flags |= OC_EF_DYING | OC_EF_OFFLRU;
			oc->refcnt++;
			VSC_C_main->n_lru_nuked++; // XXX per lru ?
			VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
		} else {
			oc = NULL;
		}
		Lck_Unlock(&oh->mtx);
		if (oc != NULL)
			break;
	}
	Lck_Unlock(&lru->mtx);

	if (oc == NULL) {
		VSLb(wrk->vsl, SLT_ExpKill, "LRU_Fail");
		return (-1);
	}

	/* XXX: We could grab and return one storage segment to our caller */
	ObjSlim(wrk, oc);

	exp_mail_it(oc);

	VSLb(wrk->vsl, SLT_ExpKill, "LRU x=%u", ObjGetXID(wrk, oc));
	(void)HSH_DerefObjCore(wrk, &oc);
	return (1);
}

/*--------------------------------------------------------------------*/

uintptr_t
EXP_Register_Callback(exp_callback_f *func, void *priv)
{
	struct exp_callback *ecb;

	AN(func);

	ALLOC_OBJ(ecb, EXP_CALLBACK_MAGIC);
	AN(ecb);
	ecb->func = func;
	ecb->priv = priv;
	AZ(pthread_rwlock_wrlock(&exphdl->cb_rwl));
	VTAILQ_INSERT_TAIL(&exphdl->ecb_list, ecb, list);
	AZ(pthread_rwlock_unlock(&exphdl->cb_rwl));
	return ((uintptr_t)ecb);
}

void
EXP_Deregister_Callback(uintptr_t *handle)
{
	struct exp_callback *ecb;

	AN(handle);
	AN(*handle);
	AZ(pthread_rwlock_wrlock(&exphdl->cb_rwl));
	VTAILQ_FOREACH(ecb, &exphdl->ecb_list, list) {
		CHECK_OBJ_NOTNULL(ecb, EXP_CALLBACK_MAGIC);
		if ((uintptr_t)ecb == *handle)
			break;
	}
	AN(ecb);
	VTAILQ_REMOVE(&exphdl->ecb_list, ecb, list);
	AZ(pthread_rwlock_unlock(&exphdl->cb_rwl));
	FREE_OBJ(ecb);
	*handle = 0;
}

/*--------------------------------------------------------------------
 * Handle stuff in the inbox
 */

static void
exp_inbox(struct exp_priv *ep, struct objcore *oc, double now)
{
	unsigned flags;
	struct lru *lru;

	CHECK_OBJ_NOTNULL(ep, EXP_PRIV_MAGIC);
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	VSLb(&ep->vsl, SLT_ExpKill, "EXP_Inbox p=%p e=%.9f f=0x%x", oc,
	    oc->timer_when, oc->flags);

	// AZ(oc->flags & OC_F_BUSY);

	lru = ObjGetLRU(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);

	/* Evacuate our action-flags, and put it back on the LRU list */
	Lck_Lock(&lru->mtx);
	flags = oc->exp_flags;
	AN(flags & OC_EF_OFFLRU);
	oc->exp_flags &= ~(OC_EF_INSERT | OC_EF_MOVE);
	oc->last_lru = now;
	if (!(flags & OC_EF_DYING)) {
		VTAILQ_INSERT_TAIL(&lru->lru_head, oc, lru_list);
		oc->exp_flags &= ~OC_EF_OFFLRU;
	}
	Lck_Unlock(&lru->mtx);

	if (flags & OC_EF_DYING) {
		VSLb(&ep->vsl, SLT_ExpKill, "EXP_Kill p=%p e=%.9f f=0x%x", oc,
		    oc->timer_when, oc->flags);
		if (!(flags & OC_EF_INSERT)) {
			assert(oc->timer_idx != BINHEAP_NOIDX);
			binheap_delete(ep->heap, oc->timer_idx);
		}
		assert(oc->timer_idx == BINHEAP_NOIDX);
		exp_event(ep->wrk, oc, EXP_REMOVE);
		(void)HSH_DerefObjCore(ep->wrk, &oc);
		return;
	}

	if (flags & OC_EF_MOVE) {
		oc->timer_when = EXP_When(&oc->exp);
		ObjUpdateMeta(ep->wrk, oc);
	}

	VSLb(&ep->vsl, SLT_ExpKill, "EXP_When p=%p e=%.9f f=0x%x", oc,
	    oc->timer_when, flags);

	/*
	 * XXX: There are some pathological cases here, were we
	 * XXX: insert or move an expired object, only to find out
	 * XXX: the next moment and rip them out again.
	 */

	if (flags & OC_EF_INSERT) {
		assert(oc->timer_idx == BINHEAP_NOIDX);
		binheap_insert(exphdl->heap, oc);
		assert(oc->timer_idx != BINHEAP_NOIDX);
	} else if (flags & OC_EF_MOVE) {
		assert(oc->timer_idx != BINHEAP_NOIDX);
		binheap_reorder(exphdl->heap, oc->timer_idx);
		assert(oc->timer_idx != BINHEAP_NOIDX);
	} else {
		WRONG("Objcore state wrong in inbox");
	}
}

/*--------------------------------------------------------------------
 * Expire stuff from the binheap
 */

static double
exp_expire(struct exp_priv *ep, double now)
{
	struct lru *lru;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(ep, EXP_PRIV_MAGIC);

	oc = binheap_root(ep->heap);
	if (oc == NULL)
		return (now + 355./113.);

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	/* Ready ? */
	if (oc->timer_when > now)
		return (oc->timer_when);

	VSC_C_main->n_expired++;

	lru = ObjGetLRU(oc);
	CHECK_OBJ_NOTNULL(lru, LRU_MAGIC);
	Lck_Lock(&lru->mtx);
	// AZ(oc->flags & OC_F_BUSY);
	oc->exp_flags |= OC_EF_DYING;
	if (oc->exp_flags & OC_EF_OFFLRU)
		oc = NULL;
	else {
		oc->exp_flags |= OC_EF_OFFLRU;
		VTAILQ_REMOVE(&lru->lru_head, oc, lru_list);
	}
	Lck_Unlock(&lru->mtx);

	if (oc == NULL)
		return (now + 1e-3);		// XXX ?

	/* Remove from binheap */
	assert(oc->timer_idx != BINHEAP_NOIDX);
	binheap_delete(ep->heap, oc->timer_idx);
	assert(oc->timer_idx == BINHEAP_NOIDX);

	CHECK_OBJ_NOTNULL(oc->objhead, OBJHEAD_MAGIC);
	VSLb(&ep->vsl, SLT_ExpKill, "EXP_Expired x=%u t=%.0f",
	    ObjGetXID(ep->wrk, oc), EXP_Ttl(NULL, &oc->exp) - now);
	exp_event(ep->wrk, oc, EXP_REMOVE);
	(void)HSH_DerefObjCore(ep->wrk, &oc);
	return (0);
}

/*--------------------------------------------------------------------
 * This thread monitors the root of the binary heap and whenever an
 * object expires, accounting also for graceability, it is killed.
 */

static int
object_cmp(void *priv, void *a, void *b)
{
	struct objcore *aa, *bb;

	(void)priv;
	CAST_OBJ_NOTNULL(aa, a, OBJCORE_MAGIC);
	CAST_OBJ_NOTNULL(bb, b, OBJCORE_MAGIC);
	return (aa->timer_when < bb->timer_when);
}

static void
object_update(void *priv, void *p, unsigned u)
{
	struct objcore *oc;

	(void)priv;
	CAST_OBJ_NOTNULL(oc, p, OBJCORE_MAGIC);
	oc->timer_idx = u;
}

static void * __match_proto__(bgthread_t)
exp_thread(struct worker *wrk, void *priv)
{
	struct objcore *oc;
	double t = 0, tnext = 0;
	struct exp_priv *ep;

	CAST_OBJ_NOTNULL(ep, priv, EXP_PRIV_MAGIC);
	ep->wrk = wrk;
	VSL_Setup(&ep->vsl, NULL, 0);
	ep->heap = binheap_new(NULL, object_cmp, object_update);
	AN(ep->heap);
	while (1) {

		Lck_Lock(&ep->mtx);
		oc = VTAILQ_FIRST(&ep->inbox);
		if (oc != NULL) {
			VTAILQ_REMOVE(&ep->inbox, oc, lru_list);
			VSC_C_main->exp_received++;
			tnext = 0;
		} else if (tnext > t) {
			VSL_Flush(&ep->vsl, 0);
			Pool_Sumstat(wrk);
			(void)Lck_CondWait(&ep->condvar, &ep->mtx, tnext);
		}
		Lck_Unlock(&ep->mtx);

		t = VTIM_real();

		if (oc != NULL)
			exp_inbox(ep, oc, t);
		else
			tnext = exp_expire(ep, t);
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

void
EXP_Init(void)
{
	struct exp_priv *ep;
	pthread_t pt;

	ALLOC_OBJ(ep, EXP_PRIV_MAGIC);
	AN(ep);

	Lck_New(&ep->mtx, lck_exp);
	AZ(pthread_cond_init(&ep->condvar, NULL));
	VTAILQ_INIT(&ep->inbox);
	AZ(pthread_rwlock_init(&ep->cb_rwl, NULL));
	VTAILQ_INIT(&ep->ecb_list);
	exphdl = ep;
	WRK_BgThread(&pt, "cache-timeout", exp_thread, ep);
}
