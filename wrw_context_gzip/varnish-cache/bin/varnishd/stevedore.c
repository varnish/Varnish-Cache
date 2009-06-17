/*-
 * Copyright (c) 2007-2009 Linpro AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgav <des@des.no>
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

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "stevedore.h"

static VTAILQ_HEAD(, stevedore)	stevedores =
    VTAILQ_HEAD_INITIALIZER(stevedores);

static const struct stevedore * volatile stv_next;

struct storage *
STV_alloc(struct sess *sp, size_t size)
{
	struct storage *st;
	struct stevedore *stv = NULL;
	unsigned fail = 0;

	/*
	 * Always try the stevedore which allocated the object in order to
	 * not needlessly split an object across multiple stevedores.
	 */
	if (sp->obj != NULL) {
		CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
		if (sp->obj->objstore != NULL) {
			stv = sp->obj->objstore->stevedore;
			CHECK_OBJ_NOTNULL(stv, STEVEDORE_MAGIC);
		}
	}

	for (;;) {
		if (stv == NULL) {
			/* pick a stevedore and bump the head along */
			stv = VTAILQ_NEXT(stv_next, list);
			if (stv == NULL)
				stv = VTAILQ_FIRST(&stevedores);
			AN(stv);
			AN(stv->name);
			stv_next = stv;
			fail = 0;
		}

		/* try to allocate from it */
		AN(stv->alloc);
		st = stv->alloc(stv, size);
		if (st != NULL)
			break;

		/* no luck; try to free some space and keep trying */
		if (EXP_NukeOne(sp, &stv->lru) == -1)
			break;

		/* Enough is enough: try another if we have one */
		if (++fail == 50)
			stv = NULL;
	}
	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	return (st);
}

void
STV_trim(struct storage *st, size_t size)
{

	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	AN(st->stevedore);
	if (st->stevedore->trim)
		st->stevedore->trim(st, size);
}

void
STV_free(struct storage *st)
{

	CHECK_OBJ_NOTNULL(st, STORAGE_MAGIC);
	AN(st->stevedore);
	AN(st->stevedore->free);
	st->stevedore->free(st);
}

void
STV_add(const struct stevedore *stv2, int ac, char * const *av)
{
	struct stevedore *stv;

	CHECK_OBJ_NOTNULL(stv2, STEVEDORE_MAGIC);
	ALLOC_OBJ(stv, STEVEDORE_MAGIC);
	AN(stv);

	*stv = *stv2;
	AN(stv->name);
	AN(stv->alloc);
	VTAILQ_INIT(&stv->lru);

	if (stv->init != NULL)
		stv->init(stv, ac, av);
	else if (ac != 0)
		ARGV_ERR("(-s%s) too many arguments\n", stv->name);

	VTAILQ_INSERT_TAIL(&stevedores, stv, list);

	if (!stv_next)
		stv_next = VTAILQ_FIRST(&stevedores);
}

void
STV_open(void)
{
	struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stevedores, list) {
		if (stv->open != NULL)
			stv->open(stv);
	}
}

void
STV_close(void)
{
	struct stevedore *stv;

	VTAILQ_FOREACH(stv, &stevedores, list) {
		if (stv->close != NULL)
			stv->close(stv);
	}
}

struct objcore_head *
STV_lru(struct storage *st)
{
	if (st == NULL)
		return (NULL);
	CHECK_OBJ(st, STORAGE_MAGIC);

	return (&st->stevedore->lru);
}

const struct choice STV_choice[] = {
	{ "file",	&smf_stevedore },
	{ "malloc",	&sma_stevedore },
	{ "persistent",	&smp_stevedore },
#ifdef HAVE_LIBUMEM
	{ "umem",	&smu_stevedore },
#endif
	{ NULL,		NULL }
};
