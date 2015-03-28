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
 * We maintain a number of worker thread pools, to spread lock contention.
 *
 * Pools can be added on the fly, as a means to mitigate lock contention,
 * but can only be removed again by a restart. (XXX: we could fix that)
 *
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>

#include "cache.h"

#include "vtim.h"

VTAILQ_HEAD(taskhead, pool_task);

/* Number of work requests queued in excess of worker threads available */

struct pool {
	unsigned			magic;
#define POOL_MAGIC			0x606658fa
	VTAILQ_ENTRY(pool)		list;

	pthread_cond_t			herder_cond;
	pthread_t			herder_thr;

	struct lock			mtx;
	struct taskhead			idle_queue;
	struct taskhead			front_queue;
	struct taskhead			back_queue;
	unsigned			nthr;
	unsigned			dry;
	unsigned			lqueue;
	uintmax_t			ndropped;
	uintmax_t			nqueued;
	struct sesspool			*sesspool;
	struct dstat			*a_stat;
	struct dstat			*b_stat;
};

static struct lock		pool_mtx;
static pthread_t		thr_pool_herder;

static struct lock		wstat_mtx;

/*--------------------------------------------------------------------
 * Summing of stats into global stats counters
 */

static void
pool_sumstat(const struct dstat *src)
{

	Lck_AssertHeld(&wstat_mtx);
#define L0(n)
#define L1(n) (VSC_C_main->n += src->n)
#define VSC_F(n,t,l,s,f,v,d,e)	L##l(n);
#include "tbl/vsc_f_main.h"
#undef VSC_F
#undef L0
#undef L1
}

void
Pool_Sumstat(struct worker *wrk)
{

	Lck_Lock(&wstat_mtx);
	pool_sumstat(wrk->stats);
	Lck_Unlock(&wstat_mtx);
	memset(wrk->stats, 0, sizeof *wrk->stats);
}

int
Pool_TrySumstat(struct worker *wrk)
{
	if (Lck_Trylock(&wstat_mtx))
		return (0);
	pool_sumstat(wrk->stats);
	Lck_Unlock(&wstat_mtx);
	memset(wrk->stats, 0, sizeof *wrk->stats);
	return (1);
}

/*--------------------------------------------------------------------
 * Summing of stats into pool counters
 */

static void
pool_addstat(struct dstat *dst, struct dstat *src)
{

	dst->summs++;
#define L0(n)
#define L1(n) (dst->n += src->n)
#define VSC_F(n,t,l,s,f,v,d,e)	L##l(n);
#include "tbl/vsc_f_main.h"
#undef VSC_F
#undef L0
#undef L1
	memset(src, 0, sizeof *src);
}

/*--------------------------------------------------------------------
 * Helper function to update stats for purges under lock
 */

void
Pool_PurgeStat(unsigned nobj)
{
	Lck_Lock(&wstat_mtx);
	VSC_C_main->n_purges++;
	VSC_C_main->n_obj_purged += nobj;
	Lck_Unlock(&wstat_mtx);
}


/*--------------------------------------------------------------------
 */

static struct worker *
pool_getidleworker(struct pool *pp)
{
	struct pool_task *pt;
	struct worker *wrk;

	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	Lck_AssertHeld(&pp->mtx);
	pt = VTAILQ_FIRST(&pp->idle_queue);
	if (pt == NULL) {
		if (pp->nthr < cache_param->wthread_max) {
			pp->dry++;
			AZ(pthread_cond_signal(&pp->herder_cond));
		}
		return (NULL);
	}
	AZ(pt->func);
	CAST_OBJ_NOTNULL(wrk, pt->priv, WORKER_MAGIC);
	return (wrk);
}

/*--------------------------------------------------------------------
 * Special scheduling:  If no thread can be found, the current thread
 * will be prepared for rescheduling instead.
 * The selected threads workspace is reserved and the argument put there.
 * Return one if another thread was scheduled, otherwise zero.
 */

int
Pool_Task_Arg(struct worker *wrk, task_func_t *func,
    const void *arg, size_t arg_len)
{
	struct pool *pp;
	struct worker *wrk2;
	int retval;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(arg);
	AN(arg_len);
	pp = wrk->pool;
	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);

	Lck_Lock(&pp->mtx);
	wrk2 = pool_getidleworker(pp);
	if (wrk2 != NULL) {
		VTAILQ_REMOVE(&pp->idle_queue, &wrk2->task, list);
		retval = 1;
	} else {
		wrk2 = wrk;
		retval = 0;
	}
	Lck_Unlock(&pp->mtx);
	AZ(wrk2->task.func);

	assert(arg_len == WS_Reserve(wrk2->aws, arg_len));
	memcpy(wrk2->aws->f, arg, arg_len);
	wrk2->task.func = func;
	wrk2->task.priv = wrk2->aws->f;
	if (retval)
		AZ(pthread_cond_signal(&wrk2->cond));
	return (retval);
}

/*--------------------------------------------------------------------
 * Enter a new task to be done
 */

int
Pool_Task(struct pool *pp, struct pool_task *task, enum pool_how how)
{
	struct worker *wrk;
	int retval = 0;

	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	AN(task);
	AN(task->func);

	Lck_Lock(&pp->mtx);

	/*
	 * The common case first:  Take an idle thread, do it.
	 */

	wrk = pool_getidleworker(pp);
	if (wrk != NULL) {
		VTAILQ_REMOVE(&pp->idle_queue, &wrk->task, list);
		AZ(wrk->task.func);
		wrk->task.func = task->func;
		wrk->task.priv = task->priv;
		Lck_Unlock(&pp->mtx);
		AZ(pthread_cond_signal(&wrk->cond));
		return (0);
	}

	switch (how) {
	case POOL_NO_QUEUE:
		retval = -1;
		break;
	case POOL_QUEUE_FRONT:
		/* If we have too much in the queue already, refuse. */
		if (pp->lqueue > cache_param->wthread_queue_limit) {
			pp->ndropped++;
			retval = -1;
		} else {
			VTAILQ_INSERT_TAIL(&pp->front_queue, task, list);
			pp->nqueued++;
			pp->lqueue++;
		}
		break;
	case POOL_QUEUE_BACK:
		VTAILQ_INSERT_TAIL(&pp->back_queue, task, list);
		break;
	default:
		WRONG("Unknown enum pool_how");
	}
	Lck_Unlock(&pp->mtx);
	return (retval);
}

/*--------------------------------------------------------------------
 * Empty function used as a pointer value for the thread exit condition.
 */

static void __match_proto__(task_func_t)
pool_kiss_of_death(struct worker *wrk, void *priv)
{
	(void)wrk;
	(void)priv;
}

/*--------------------------------------------------------------------
 * Special function to summ stats
 */

static void __match_proto__(task_func_t)
pool_stat_summ(struct worker *wrk, void *priv)
{
	struct dstat *src;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(wrk->pool, POOL_MAGIC);
	AN(priv);
	src = priv;
	Lck_Lock(&wstat_mtx);
	pool_sumstat(src);
	Lck_Unlock(&wstat_mtx);
	memset(src, 0, sizeof *src);
	wrk->pool->b_stat = src;
}

/*--------------------------------------------------------------------
 * This is the work function for worker threads in the pool.
 */

void
Pool_Work_Thread(struct pool *pp, struct worker *wrk)
{
	struct pool_task *tp;
	struct pool_task tpx, tps;
	int i;

	CHECK_OBJ_NOTNULL(pp, POOL_MAGIC);
	wrk->pool = pp;
	while (1) {
		Lck_Lock(&pp->mtx);

		CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

		WS_Reset(wrk->aws, NULL);
		AZ(wrk->vsl);

		tp = VTAILQ_FIRST(&pp->front_queue);
		if (tp != NULL) {
			pp->lqueue--;
			VTAILQ_REMOVE(&pp->front_queue, tp, list);
		} else {
			tp = VTAILQ_FIRST(&pp->back_queue);
			if (tp != NULL)
				VTAILQ_REMOVE(&pp->back_queue, tp, list);
		}

		if ((tp == NULL && wrk->stats->summs > 0) ||
		    (wrk->stats->summs >= cache_param->wthread_stats_rate))
			pool_addstat(pp->a_stat, wrk->stats);

		if (tp != NULL) {
			wrk->stats->summs++;
		} else if (pp->b_stat != NULL && pp->a_stat->summs) {
			/* Nothing to do, push pool stats into global pool */
			tps.func = pool_stat_summ;
			tps.priv = pp->a_stat;
			pp->a_stat = pp->b_stat;
			pp->b_stat = NULL;
			tp = &tps;
		} else {
			/* Nothing to do: To sleep, perchance to dream ... */
			if (isnan(wrk->lastused))
				wrk->lastused = VTIM_real();
			wrk->task.func = NULL;
			wrk->task.priv = wrk;
			VTAILQ_INSERT_HEAD(&pp->idle_queue, &wrk->task, list);
			do {
				i = Lck_CondWait(&wrk->cond, &pp->mtx,
				    wrk->vcl == NULL ?  0 : wrk->lastused+60.);
				if (i == ETIMEDOUT)
					VCL_Rel(&wrk->vcl);
			} while (wrk->task.func == NULL);
			tpx = wrk->task;
			tp = &tpx;
			wrk->stats->summs++;
		}
		Lck_Unlock(&pp->mtx);

		if (tp->func == pool_kiss_of_death)
			break;

		do {
			memset(&wrk->task, 0, sizeof wrk->task);
			assert(wrk->pool == pp);
			tp->func(wrk, tp->priv);
			tpx = wrk->task;
			tp = &tpx;
		} while (tp->func != NULL);

		/* cleanup for next task */
		wrk->seen_methods = 0;
	}
	wrk->pool = NULL;
}

/*--------------------------------------------------------------------
 * Create another worker thread.
 */

struct pool_info {
	unsigned		magic;
#define POOL_INFO_MAGIC		0x4e4442d3
	size_t			stacksize;
	struct pool		*qp;
};

static void *
pool_thread(void *priv)
{
	struct pool_info *pi;

	CAST_OBJ_NOTNULL(pi, priv, POOL_INFO_MAGIC);
	WRK_Thread(pi->qp, pi->stacksize, cache_param->workspace_thread);
	FREE_OBJ(pi);
	return (NULL);
}

static void
pool_breed(struct pool *qp)
{
	pthread_t tp;
	pthread_attr_t tp_attr;
	struct pool_info *pi;

	AZ(pthread_attr_init(&tp_attr));
	AZ(pthread_attr_setdetachstate(&tp_attr, PTHREAD_CREATE_DETACHED));

	/* Set the stacksize for worker threads we create */
	if (cache_param->wthread_stacksize != UINT_MAX)
		AZ(pthread_attr_setstacksize(&tp_attr,
		    cache_param->wthread_stacksize));

	ALLOC_OBJ(pi, POOL_INFO_MAGIC);
	AN(pi);
	AZ(pthread_attr_getstacksize(&tp_attr, &pi->stacksize));
	pi->qp = qp;

	if (pthread_create(&tp, &tp_attr, pool_thread, pi)) {
		VSL(SLT_Debug, 0, "Create worker thread failed %d %s",
		    errno, strerror(errno));
		Lck_Lock(&pool_mtx);
		VSC_C_main->threads_failed++;
		Lck_Unlock(&pool_mtx);
		VTIM_sleep(cache_param->wthread_fail_delay);
	} else {
		qp->dry = 0;
		qp->nthr++;
		Lck_Lock(&pool_mtx);
		VSC_C_main->threads++;
		VSC_C_main->threads_created++;
		Lck_Unlock(&pool_mtx);
		VTIM_sleep(cache_param->wthread_add_delay);
	}

	AZ(pthread_attr_destroy(&tp_attr));
}

/*--------------------------------------------------------------------
 * Herd a single pool
 *
 * This thread wakes up whenever a pool queues.
 *
 * The trick here is to not be too aggressive about creating threads.
 * We do this by only examining one pool at a time, and by sleeping
 * a short while whenever we create a thread and a little while longer
 * whenever we fail to, hopefully missing a lot of cond_signals in
 * the meantime.
 *
 * XXX: probably need a lot more work.
 *
 */

static void*
pool_herder(void *priv)
{
	struct pool *pp;
	struct pool_task *pt;
	double t_idle;
	struct worker *wrk;

	CAST_OBJ_NOTNULL(pp, priv, POOL_MAGIC);

	while (1) {
		/* Make more threads if needed and allowed */
		if (pp->nthr < cache_param->wthread_min ||
		    (pp->dry && pp->nthr < cache_param->wthread_max)) {
			pool_breed(pp);
			continue;
		}
		assert(pp->nthr >= cache_param->wthread_min);

		if (pp->nthr > cache_param->wthread_min) {

			t_idle = VTIM_real() - cache_param->wthread_timeout;

			Lck_Lock(&pp->mtx);
			/* XXX: unsafe counters */
			VSC_C_main->sess_queued += pp->nqueued;
			VSC_C_main->sess_dropped += pp->ndropped;
			pp->nqueued = pp->ndropped = 0;

			wrk = NULL;
			pt = VTAILQ_LAST(&pp->idle_queue, taskhead);
			if (pt != NULL) {
				AZ(pt->func);
				CAST_OBJ_NOTNULL(wrk, pt->priv, WORKER_MAGIC);

				if (wrk->lastused < t_idle ||
				    pp->nthr > cache_param->wthread_max) {
					/* Give it a kiss on the cheek... */
					VTAILQ_REMOVE(&pp->idle_queue,
					    &wrk->task, list);
					wrk->task.func = pool_kiss_of_death;
					AZ(pthread_cond_signal(&wrk->cond));
				} else
					wrk = NULL;
			}
			Lck_Unlock(&pp->mtx);

			if (wrk != NULL) {
				pp->nthr--;
				Lck_Lock(&pool_mtx);
				VSC_C_main->threads--;
				VSC_C_main->threads_destroyed++;
				Lck_Unlock(&pool_mtx);
				VTIM_sleep(cache_param->wthread_destroy_delay);
				continue;
			}
		}

		Lck_Lock(&pp->mtx);
		if (!pp->dry) {
			(void)Lck_CondWait(&pp->herder_cond, &pp->mtx,
				VTIM_real() + 5);
		} else {
			/* XXX: unsafe counters */
			VSC_C_main->threads_limited++;
			pp->dry = 0;
		}
		Lck_Unlock(&pp->mtx);
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------
 * Add a thread pool
 */

static struct pool *
pool_mkpool(unsigned pool_no)
{
	struct pool *pp;

	ALLOC_OBJ(pp, POOL_MAGIC);
	if (pp == NULL)
		return (NULL);
	pp->a_stat = calloc(1, sizeof *pp->a_stat);
	AN(pp->a_stat);
	pp->b_stat = calloc(1, sizeof *pp->b_stat);
	AN(pp->b_stat);
	Lck_New(&pp->mtx, lck_wq);

	VTAILQ_INIT(&pp->idle_queue);
	VTAILQ_INIT(&pp->front_queue);
	VTAILQ_INIT(&pp->back_queue);
	AZ(pthread_cond_init(&pp->herder_cond, NULL));
	AZ(pthread_create(&pp->herder_thr, NULL, pool_herder, pp));

	while (VTAILQ_EMPTY(&pp->idle_queue))
		(void)usleep(10000);

	pp->sesspool = SES_NewPool(pp, pool_no);
	AN(pp->sesspool);

	return (pp);
}

/*--------------------------------------------------------------------
 * This thread adjusts the number of pools to match the parameter.
 *
 */

static void *
pool_poolherder(void *priv)
{
	unsigned nwq;
	VTAILQ_HEAD(,pool)	pools = VTAILQ_HEAD_INITIALIZER(pools);
	struct pool *pp;
	uint64_t u;

	THR_SetName("pool_herder");
	(void)priv;

	nwq = 0;
	while (1) {
		if (nwq < cache_param->wthread_pools) {
			pp = pool_mkpool(nwq);
			if (pp != NULL) {
				VTAILQ_INSERT_TAIL(&pools, pp, list);
				VSC_C_main->pools++;
				nwq++;
				continue;
			}
		}
		/* XXX: remove pools */
		if (0)
			SES_DeletePool(NULL);
		(void)sleep(1);
		u = 0;
		VTAILQ_FOREACH(pp, &pools, list)
			u += pp->lqueue;
		VSC_C_main->thread_queue_len = u;
	}
	NEEDLESS_RETURN(NULL);
}

/*--------------------------------------------------------------------*/

void
Pool_Init(void)
{

	Lck_New(&wstat_mtx, lck_wstat);
	Lck_New(&pool_mtx, lck_wq);
	AZ(pthread_create(&thr_pool_herder, NULL, pool_poolherder, NULL));
}
