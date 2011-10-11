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
 * Implementation of a binary heap API
 *
 * See also:
 *	http://portal.acm.org/citation.cfm?doid=1785414.1785434
 *	(or: http://queue.acm.org/detail.cfm?id=1814327)
 *
 * Test driver can be built and started using the following commands:
 * $ cc -DTEST_DRIVER -I../.. -I../../include -lrt -lm binary_heap.c
 * $ ./a.out
 */

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <math.h>		// for testcase
#include <stdint.h>
#include <stdio.h>		// for testcase
#include <stdlib.h>
#include <time.h>		// for testcase
#include <unistd.h>

#include "binary_heap.h"
#include "miniobj.h"		// for testcase
#include "vas.h"

/* Parameters --------------------------------------------------------*/

/*
 * The number of elements in a row has to be a compromise between
 * wasted space and number of memory allocations.
 * With 64k objects per row, there will be at least 5...10 seconds
 * between row additions on a very busy server.
 * At the same time, the worst case amount of wasted memory is kept
 * at a reasonable 1 MB -- two rows on 64bit system.
 * Finally, but without practical significance: 16 bits should be
 * easier for the compiler to optimize.
 */
#define ROW_SHIFT		16


#undef PARANOIA

/* Private definitions -----------------------------------------------*/

#define ROOT_IDX		1

#define ROW_WIDTH		(1 << ROW_SHIFT)

/*lint -emacro(572, ROW) shift 0 >> by 16 */
/*lint -emacro(835, ROW) 0 left of >> */
/*lint -emacro(778, ROW) const >> evaluates to zero */
#define ROW(b, n)		((b)->array[(n) >> ROW_SHIFT])

/*lint -emacro(835, A) 0 left of & */
#define A(b, n)			ROW(b, n)[(n) & (ROW_WIDTH - 1)]


#ifdef TEST_DRIVER

/*
 * The memory model is used by test driver in order to count an approximate
 * number of page faults induced by binheap mutations.
 * The model keeps recently accessed pages in lru list
 * of resident_pages_count size.
 * It uses dumb array for lru list implementation, since it is simple and
 * it works quite fast for small sizes.
 */
struct mem {
	unsigned	magic;
#define MEM_MAGIC	0xf07c9610U
	uintptr_t	*lru;
	uintptr_t	page_mask;
	uint64_t	pagefaults_count;
	unsigned	resident_pages_count;
};

static struct mem *
create_mem(void)
{
	struct mem *m;
	uintptr_t page_size;

	page_size = (uintptr_t) getpagesize();
	xxxassert(page_size > 0);
	XXXAZ(page_size & (page_size - 1));

	m = malloc(sizeof(*m));
	XXXAN(m);
	m->magic = MEM_MAGIC;
	m->lru = NULL;
	m->page_mask = ~(page_size - 1);
	m->pagefaults_count = 0;
	m->resident_pages_count = 0;
	return (m);
}

static void
init_mem(struct mem *m, unsigned resident_pages_count)
{
	CHECK_OBJ_NOTNULL(m, MEM_MAGIC);
	free(m->lru);
	m->lru = NULL;
	if (resident_pages_count > 0) {
		m->lru = calloc(resident_pages_count, sizeof(*m->lru));
		XXXAN(m->lru);
	}
	m->pagefaults_count = 0;
	m->resident_pages_count = resident_pages_count;
}

static void
access_mem(struct mem *m, void *p)
{
	uintptr_t addr, *lru;
	unsigned u, v;

	CHECK_OBJ_NOTNULL(m, MEM_MAGIC);
	if (m->resident_pages_count == 0)
		return;	/* mem model is disabled */
	if (p == NULL)
		return;	/* access to NULL is forbidden */

	addr = ((uintptr_t) p) & m->page_mask;
	lru = m->lru;
	for (u = 0; u < m->resident_pages_count; u++) {
		if (lru[u] == addr) {
			for (v = u; v >= 1; v--)
				lru[v] = lru[v - 1];
			lru[0] = addr;
			return;
		}
	}
	m->pagefaults_count++;
	for (v = m->resident_pages_count - 1; v >= 1; v--)
		lru[v] = lru[v - 1];
	lru[0] = addr;
}

#define TEST_DRIVER_DECLARE_MEM		struct mem *m;	/* semicolon */
#define TEST_DRIVER_CREATE_MEM(bh)	(bh)->m = create_mem()
#define TEST_DRIVER_ACCESS_MEM(bh, p)	access_mem((bh)->m, (p))
#else
#define TEST_DRIVER_DECLARE_MEM		/* nothing */
#define TEST_DRIVER_CREATE_MEM(bh)	((void)0)
#define TEST_DRIVER_ACCESS_MEM(bh, p)	((void)0)
#endif

#define TEST_DRIVER_ACCESS_IDX(bh, u)	do { \
	TEST_DRIVER_ACCESS_MEM(bh, &A(bh, u)); \
	TEST_DRIVER_ACCESS_MEM(bh, A(bh, u)); \
} while (0)

struct binheap {
	unsigned		magic;
#define BINHEAP_MAGIC		0xf581581aU	/* from /dev/random */
	void			*priv;
	binheap_cmp_t		*cmp;
	binheap_update_t	*update;
	void			***array;
	unsigned		rows;
	unsigned		length;
	unsigned		next;
	unsigned		page_size;
	unsigned		page_mask;
	unsigned		page_shift;
	TEST_DRIVER_DECLARE_MEM			/* no semicolon */
};

#define VM_AWARE

#ifdef VM_AWARE

static  unsigned
parent(const struct binheap *bh, unsigned u)
{
	unsigned po;
	unsigned v;

	assert(u != UINT_MAX);
	po = u & bh->page_mask;

	if (u < bh->page_size || po > 3) {
		v = (u & ~bh->page_mask) | (po >> 1);
	} else if (po < 2) {
		v = (u - bh->page_size) >> bh->page_shift;
		v += v & ~(bh->page_mask >> 1);
		v |= bh->page_size / 2;
	} else {
		v = u - 2;
	}
	return (v);
}

static void
child(const struct binheap *bh, unsigned u, unsigned *a, unsigned *b)
{
	uintmax_t uu;

	if (u > bh->page_mask && (u & (bh->page_mask - 1)) == 0) {
		/* First two elements are magical except on the first page */
		*a = *b = u + 2;
	} else if (u & (bh->page_size >> 1)) {
		/* The bottom row is even more magical */
		*a = (u & ~bh->page_mask) >> 1;
		*a |= u & (bh->page_mask >> 1);
		*a += 1;
		uu = (uintmax_t)*a << bh->page_shift;
		*a = uu;
		if (*a == uu) {
			*b = *a + 1;
		} else {
			/*
			 * An unsigned is not big enough: clamp instead
			 * of truncating.  We do not support adding
			 * more than UINT_MAX elements anyway, so this
			 * is without consequence.
			 */
			*a = UINT_MAX;
			*b = UINT_MAX;
		}
	} else {
		/* The rest is as usual, only inside the page */
		*a = u + (u & bh->page_mask);
		*b = *a + 1;
	}
#ifdef PARANOIA
	assert(*a > 0);
	assert(*b > 0);
	if (*a != UINT_MAX) {
		assert(parent(bh, *a) == u);
		assert(parent(bh, *b) == u);
	}
#endif
}


#else

static unsigned
parent(const struct binheap *bh, unsigned u)
{

	(void)bh;
	return (u / 2);
}

static void
child(const struct binheap *bh, unsigned u, unsigned *a, unsigned *b)
{

	(void)bh;
	*a = u * 2;
	*b = *a + 1;
}

#endif

/* Implementation ----------------------------------------------------*/

static void
binheap_addrow(struct binheap *bh)
{
	unsigned u;

	/* First make sure we have space for another row */
	if (&ROW(bh, bh->length) >= bh->array + bh->rows) {
		u = bh->rows * 2;
		bh->array = realloc(bh->array, sizeof(*bh->array) * u);
		assert(bh->array != NULL);

		/* NULL out new pointers */
		while (bh->rows < u)
			bh->array[bh->rows++] = NULL;
	}
	assert(ROW(bh, bh->length) == NULL);
	ROW(bh, bh->length) = malloc(sizeof(**bh->array) * ROW_WIDTH);
	assert(ROW(bh, bh->length));
	bh->length += ROW_WIDTH;
}

struct binheap *
binheap_new(void *priv, binheap_cmp_t *cmp_f, binheap_update_t *update_f)
{
	struct binheap *bh;
	unsigned u;

	bh = calloc(sizeof *bh, 1);
	if (bh == NULL)
		return (bh);
	bh->priv = priv;

	bh->page_size = (unsigned)getpagesize() / sizeof (void *);
	bh->page_mask = bh->page_size - 1;
	assert(!(bh->page_size & bh->page_mask));	/* power of two */
	for (u = 1; (1U << u) != bh->page_size; u++)
		;
	bh->page_shift = u;
	assert(bh->page_size <= (sizeof(**bh->array) * ROW_WIDTH));

	bh->cmp = cmp_f;
	bh->update = update_f;
	bh->next = ROOT_IDX;
	bh->rows = 16;		/* A tiny-ish number */
	bh->array = calloc(sizeof *bh->array, bh->rows);
	assert(bh->array != NULL);
	TEST_DRIVER_CREATE_MEM(bh);
	binheap_addrow(bh);
	A(bh, ROOT_IDX) = NULL;
	bh->magic = BINHEAP_MAGIC;
	return (bh);
}

static void
binheap_update(const struct binheap *bh, unsigned u)
{
	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(u < bh->next);
	assert(A(bh, u) != NULL);
	if (bh->update != NULL) {
		TEST_DRIVER_ACCESS_IDX(bh, u);
		bh->update(bh->priv, A(bh, u), u);
	}
}

static void
binhead_swap(const struct binheap *bh, unsigned u, unsigned v)
{
	void *p;

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(u < bh->next);
	assert(A(bh, u) != NULL);
	assert(v < bh->next);
	assert(A(bh, v) != NULL);
	p = A(bh, u);
	A(bh, u) = A(bh, v);
	A(bh, v) = p;
	binheap_update(bh, u);
	binheap_update(bh, v);
}

static unsigned
binheap_trickleup(const struct binheap *bh, unsigned u)
{
	unsigned v;

	assert(bh != NULL); assert(bh->magic == BINHEAP_MAGIC);
	assert(u < bh->next);
	assert(A(bh, u) != NULL);

	while (u > ROOT_IDX) {
		assert(u < bh->next);
		assert(A(bh, u) != NULL);
		v = parent(bh, u);
		assert(v < u);
		assert(v < bh->next);
		assert(A(bh, v) != NULL);
		TEST_DRIVER_ACCESS_IDX(bh, u);
		TEST_DRIVER_ACCESS_IDX(bh, v);
		if (!bh->cmp(bh->priv, A(bh, u), A(bh, v)))
			break;
		binhead_swap(bh, u, v);
		u = v;
	}
	return (u);
}

static unsigned
binheap_trickledown(const struct binheap *bh, unsigned u)
{
	unsigned v1, v2;

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(u < bh->next);
	assert(A(bh, u) != NULL);

	while (1) {
		assert(u < bh->next);
		assert(A(bh, u) != NULL);
		child(bh, u, &v1, &v2);
		assert(v1 > 0);
		assert(v2 > 0);
		assert(v1 <= v2);

		if (v1 >= bh->next)
			return (u);

		assert(A(bh, v1) != NULL);
		if (v1 != v2 && v2 < bh->next) {
			assert(A(bh, v2) != NULL);
			TEST_DRIVER_ACCESS_IDX(bh, v1);
			TEST_DRIVER_ACCESS_IDX(bh, v2);
			if (bh->cmp(bh->priv, A(bh, v2), A(bh, v1)))
				v1 = v2;
		}
		assert(v1 < bh->next);
		assert(A(bh, v1) != NULL);
		TEST_DRIVER_ACCESS_IDX(bh, u);
		TEST_DRIVER_ACCESS_IDX(bh, v1);
		if (bh->cmp(bh->priv, A(bh, u), A(bh, v1)))
			return (u);
		binhead_swap(bh, u, v1);
		u = v1;
	}
}

void
binheap_insert(struct binheap *bh, void *p)
{
	unsigned u;

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(bh->length >= bh->next);
	if (bh->length == bh->next)
		binheap_addrow(bh);
	assert(bh->length > bh->next);
	u = bh->next++;
	A(bh, u) = p;
	binheap_update(bh, u);
	(void)binheap_trickleup(bh, u);
	assert(u < bh->next);
	assert(A(bh, u) != NULL);
}


#ifdef PARANOIA
static void
chk(const struct binheap *bh)
{
	unsigned u, v;

	for (u = 2; u < bh->next; u++) {
		v = parent(bh, u);
		assert(!bh->cmp(bh->priv, A(bh, u), A(bh, v)));
	}
}
#endif

void *
binheap_root(const struct binheap *bh)
{

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
#ifdef PARANOIA
	chk(bh);
#endif
	TEST_DRIVER_ACCESS_IDX(bh, ROOT_IDX);
	return (A(bh, ROOT_IDX));
}

/*
 * It may seem counter-intuitive that we delete by replacement with
 * the tail object. "That's almost certain to not belong there, in
 * particular when we delete the root ?" is the typical reaction.
 *
 * If we tried to trickle up into the empty position, we would,
 * eventually, end up with a hole in the bottom row, at which point
 * we would move the tail object there.
 * But there is no guarantee that the tail object would not need to
 * trickle up from that position, in fact, it might be the new root
 * of this half of the subtree.
 * The total number of operations is guaranteed to be at least
 * N{height} downward selections, because we have to get the hole
 * all the way down, but in addition to that, we may get up to
 * N{height}-1 upward trickles.
 *
 * When we fill the hole with the tail object, the worst case is
 * that it trickles all the way up to of this half-tree, or down
 * to become the tail object again.
 *
 * In other words worst case is N{height} up or downward trickles.
 * But there is a decent chance that it does not make it all the way.
 */

void
binheap_delete(struct binheap *bh, unsigned idx)
{

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(bh->next > ROOT_IDX);
	assert(idx < bh->next);
	assert(idx > 0);
	assert(A(bh, idx) != NULL);
	TEST_DRIVER_ACCESS_IDX(bh, idx);
	bh->update(bh->priv, A(bh, idx), BINHEAP_NOIDX);
	if (idx == --bh->next) {
		A(bh, bh->next) = NULL;
		return;
	}
	A(bh, idx) = A(bh, bh->next);
	A(bh, bh->next) = NULL;
	binheap_update(bh, idx);
	idx = binheap_trickleup(bh, idx);
	assert(idx < bh->next);
	assert(idx > 0);
	assert(A(bh, idx) != NULL);
	idx = binheap_trickledown(bh, idx);
	assert(idx < bh->next);
	assert(idx > 0);
	assert(A(bh, idx) != NULL);

	/*
	 * We keep a hysteresis of one full row before we start to
	 * return space to the OS to avoid silly behaviour around
	 * row boundaries.
	 */
	if (bh->next + 2 * ROW_WIDTH <= bh->length) {
		free(ROW(bh, bh->length - 1));
		ROW(bh, bh->length - 1) = NULL;
		bh->length -= ROW_WIDTH;
	}
}

/*
 * Move an item up/down after changing its key value
 */

void
binheap_reorder(const struct binheap *bh, unsigned idx)
{

	assert(bh != NULL);
	assert(bh->magic == BINHEAP_MAGIC);
	assert(bh->next > ROOT_IDX);
	assert(idx < bh->next);
	assert(idx > 0);
	assert(A(bh, idx) != NULL);
	idx = binheap_trickleup(bh, idx);
	assert(idx < bh->next);
	assert(idx > 0);
	assert(A(bh, idx) != NULL);
	idx = binheap_trickledown(bh, idx);
	assert(idx < bh->next);
	assert(idx > 0);
	assert(A(bh, idx) != NULL);
}

#ifdef TEST_DRIVER
/* Test driver -------------------------------------------------------*/

static double
get_time(void)
{
	struct timespec ts;
	int rv;

	rv = clock_gettime(CLOCK_MONOTONIC, &ts);
	XXXAZ(rv);
	return (ts.tv_sec + 1e-9 * ts.tv_nsec);
}

static void
vasfail(const char *func, const char *file, int line,
    const char *cond, int err, int xxx)
{
	fprintf(stderr, "PANIC: %s %s %d %s %d %d\n",
		func, file, line, cond, err, xxx);
	abort();
}

vas_f *VAS_Fail = vasfail;

#define ITERATIONS_PER_TEST_COUNT	10000000
#define PARENT_CHILD_TESTS_COUNT	1000000
#define MAX_ITEMS_COUNT			1000000
#define MIN_ITEMS_COUNT			1000
#define TEST_STEPS_COUNT		5
#define MAX_RESIDENT_PAGES_COUNT	4096

/*
 * Pad foo so its' size is equivalent to the objcore size.
 * Currently size of objcore is 120 bytes on x64 and 64 bytes
 * on x32. This means that the padding should be 100 for x64
 * and 44 for x32.
 */
#define PADDING 100

struct foo {
	unsigned	magic;
#define FOO_MAGIC	0x23239823
	unsigned	idx;
	double		key;
	unsigned	n;
	char		padding[PADDING];
};

static struct foo *ff[MAX_ITEMS_COUNT];

static int
cmp(void *priv, void *a, void *b)
{
	struct foo *fa, *fb;

	CAST_OBJ_NOTNULL(fa, a, FOO_MAGIC);
	CAST_OBJ_NOTNULL(fb, b, FOO_MAGIC);
	return (fa->key < fb->key);
}

void
update(void *priv, void *a, unsigned u)
{
	struct foo *fa;

	CAST_OBJ_NOTNULL(fa, a, FOO_MAGIC);
	fa->idx = u;
}

static void
check_consistency(const struct binheap *bh, unsigned items_count)
{
	struct foo *fp1, *fp2;
	unsigned u, v;

	CHECK_OBJ_NOTNULL(bh, BINHEAP_MAGIC);
	assert(bh->next >= ROOT_IDX);
	assert(bh->next <= bh->length);
	assert(bh->length >= ROW_WIDTH);
	assert(bh->rows >= 1);
	assert(bh->rows <= UINT_MAX / ROW_WIDTH);
	assert(bh->rows * ROW_WIDTH >= bh->length);
	assert(bh->page_shift > 0);
	assert(bh->page_shift <= ROW_SHIFT);
	assert(bh->page_size == (1 << bh->page_shift));
	assert(bh->page_mask == bh->page_size - 1);
	AN(bh->rows);
	for (u = ROOT_IDX + 1; u < bh->next; u++) {
		v = parent(bh, u);
		assert(v < u);
		assert(v >= ROOT_IDX);
		fp1 = A(bh, u);
		fp2 = A(bh, v);
		AN(fp1);
		AN(fp2);
		assert(fp2->key <= fp1->key);
		assert(fp1->idx == u);
		assert(fp2->idx == v);
		assert(fp1->n < items_count);
		assert(fp2->n < items_count);
		assert(ff[fp1->n] == fp1);
		assert(ff[fp2->n] == fp2);
	}
}

#define MQPS(t, q)		((t) ? (q) / (t) / 1e6 : 0)
#define PF(bh)			\
	((double) (bh)->m->pagefaults_count - (bh)->m->resident_pages_count)
#define PF_PER_ITERATION(bh, iterations_count)	\
	(PF(bh) > 0 ? PF(bh) / iterations_count : 0)

#ifdef PARANOIA
#define paranoia_check(bh)	check_consistency(bh)
#else
#define paranoia_check(bh)	((void)0)
#endif

static void
check_parent_child(struct binheap *bh, unsigned n_max)
{
	unsigned n, u, v;

	for (n = ROOT_IDX; n < n_max; n++) {
		child(bh, n, &u, &v);
		assert(u >= n);
		assert(u > ROOT_IDX);
		if (u == v) {
			if (u == UINT_MAX)
				continue;	/* child index is too big */
			v = parent(bh, u);
			assert(v == n);
		}
		else {
			assert(u + 1 == v);
			v = parent(bh, u);
			assert(v == n);
			v = parent(bh, u + 1);
			assert(v == n);
		}

		if (n == ROOT_IDX)
			continue;
		u = parent(bh, n);
		assert(u <= n);
		assert(u != BINHEAP_NOIDX);
		child(bh, u, &v, &u);
		assert(v == n || v == n - 1);
	}
}

static void
foo_check(const struct foo *fp, unsigned items_count)
{
	CHECK_OBJ_NOTNULL(fp, FOO_MAGIC);
	assert(fp->n < items_count);
	assert(fp == ff[fp->n]);
}

static void
foo_check_existence(struct binheap *bh, const struct foo *fp,
	unsigned items_count)
{
	foo_check(fp, items_count);
	assert(fp->idx != BINHEAP_NOIDX);
	assert(fp->idx >= ROOT_IDX);
	assert(fp->idx < bh->next);
	assert(fp == A(bh, fp->idx));
}

static void
foo_insert(struct binheap *bh, unsigned n, unsigned items_count)
{
	struct foo *fp;
	double key;

	paranoia_check(bh);
	assert(n < items_count);
	AZ(ff[n]);
	fp = ff[n] = malloc(sizeof(*fp));
	XXXAN(fp);
	key = random();
	fp->magic = FOO_MAGIC;
	fp->key = key;
	fp->n = n;
	binheap_insert(bh, fp);
	foo_check_existence(bh, fp, items_count);
	assert(fp->key == key);
	assert(fp->n == n);
	paranoia_check(bh);
}

static void
foo_delete(struct binheap *bh, struct foo *fp, unsigned items_count)
{
	double key;
	unsigned n;

	paranoia_check(bh);
	foo_check_existence(bh, fp, items_count);
	key = fp->key;
	n = fp->n;
	binheap_delete(bh, fp->idx);
	foo_check(fp, items_count);
	assert(fp->idx == BINHEAP_NOIDX);
	assert(fp->key == key);
	assert(fp->n == n);
	free(fp);
	ff[n] = NULL;
	paranoia_check(bh);
}

static void
foo_reorder(struct binheap *bh, struct foo *fp, unsigned items_count)
{
	double key;
	unsigned n;

	paranoia_check(bh);
	foo_check_existence(bh, fp, items_count);
	key = random();
	n = fp->n;
	fp->key = key;
	binheap_reorder(bh, fp->idx);
	foo_check_existence(bh, fp, items_count);
	assert(fp->key == key);
	assert(fp->n == n);
	paranoia_check(bh);
}

static void
test(struct binheap *bh, unsigned items_count, unsigned resident_pages_count)
{
	double start, end, key;
	struct foo *fp;
	unsigned u, n, iterations_count;
	unsigned delete_count, insert_count, reorder_count;

	CHECK_OBJ_NOTNULL(bh, BINHEAP_MAGIC);
	assert(items_count >= MIN_ITEMS_COUNT);
	assert(items_count <= MAX_ITEMS_COUNT);
	iterations_count = ITERATIONS_PER_TEST_COUNT;
	assert(iterations_count >= items_count);

	fprintf(stderr, "\n+ %u items, %u iterations, %u resident pages\n",
		items_count, iterations_count, resident_pages_count);
	AZ(binheap_root(bh));
	check_consistency(bh, 0);

	/* First insert our items */
	key = 0;
	start = get_time();
	init_mem(bh->m, resident_pages_count);
	for (n = 0; n < items_count; n++) {
		foo_insert(bh, n, items_count);
		key = ff[n]->key;
		fp = binheap_root(bh);
		foo_check(fp, items_count);
		assert(fp->idx == ROOT_IDX);
		assert(fp->key <= key);
	}
	check_consistency(bh, items_count);
	end = get_time();
	fprintf(stderr, "%u inserts: %.3lf Mqps, "
		"%.3lf pagefaults per iteration\n",
		items_count, MQPS(end - start, items_count),
		PF_PER_ITERATION(bh, items_count));

	/* For M cycles, pick the root, insert new */
	start = get_time();
	init_mem(bh->m, resident_pages_count);
	for (u = 0; u < iterations_count; u++) {
		fp = binheap_root(bh);
		foo_check(fp, items_count);
		assert(fp->idx == ROOT_IDX);
		assert(fp->key <= key);
		n = fp->n;
		foo_delete(bh, fp, items_count);
		foo_insert(bh, n, items_count);
		key = ff[n]->key;
	}
	check_consistency(bh, items_count);
	end = get_time();
	fprintf(stderr, "%u root replacements: %.3lf Mqps, "
		"%.3lf pagefaults per iteration\n", iterations_count,
		MQPS(end - start, iterations_count),
		PF_PER_ITERATION(bh, iterations_count));

	/* Randomly reorder */
	start = get_time();
	init_mem(bh->m, resident_pages_count);
	for (u = 0; u < iterations_count; u++) {
		n = random() % items_count;
		fp = ff[n];
		foo_reorder(bh, fp, items_count);
	}
	check_consistency(bh, items_count);
	end = get_time();
	fprintf(stderr, "%u random reorders: %.3lf Mqps, "
		"%.3lf pagefaults per iteration\n", iterations_count,
		MQPS(end - start, iterations_count),
		PF_PER_ITERATION(bh, iterations_count));

	/* Randomly insert, delete and reorder */
	delete_count = 0;
	insert_count = 0;
	reorder_count = 0;
	start = get_time();
	init_mem(bh->m, resident_pages_count);
	for (u = 0; u < iterations_count; u++) {
		n = random() % items_count;
		fp = ff[n];
		if (fp != NULL) {
			if (((unsigned) fp->key) & 1) {
				foo_delete(bh, fp, items_count);
				++delete_count;
			} else {
				foo_reorder(bh, fp, items_count);
				++reorder_count;
			}
		} else {
			foo_insert(bh, n, items_count);
			++insert_count;
		}
	}
	assert(delete_count >= insert_count);
	check_consistency(bh, items_count);
	end = get_time();
	fprintf(stderr,
		"%u deletes, %u inserts, %u reorders: %.3lf Mqps, "
		"%.3lf pagefaults per iteration\n",
		delete_count, insert_count, reorder_count,
		MQPS(end - start, iterations_count),
		PF_PER_ITERATION(bh, iterations_count));

	/* Then remove everything */
	key = 0;
	u = 0;
	start = get_time();
	init_mem(bh->m, resident_pages_count);
	while (1) {
		fp = binheap_root(bh);
		if (fp == NULL)
			break;
		foo_check(fp, items_count);
		assert(fp->idx == ROOT_IDX);
		assert(fp->key >= key);
		key = fp->key;
		foo_delete(bh, fp, items_count);
		++u;
	}
	assert(u == items_count - (delete_count - insert_count));
	AZ(binheap_root(bh));
	check_consistency(bh, 0);
	end = get_time();
	fprintf(stderr, "%u deletes: %.3lf Mqps, "
		"%.3lf pagefaults per iteration\n",
		u, MQPS(end - start, u), PF_PER_ITERATION(bh, u));
}

static void
run_tests(struct binheap *bh, unsigned resident_pages_count)
{
	double k;
	unsigned u, items_count;

	CHECK_OBJ_NOTNULL(bh, BINHEAP_MAGIC);
	assert(MIN_ITEMS_COUNT > 0);
	assert(MAX_ITEMS_COUNT > MIN_ITEMS_COUNT);
	k = log(((double) MAX_ITEMS_COUNT) / MIN_ITEMS_COUNT);
	assert(TEST_STEPS_COUNT > 1);
	k /= (TEST_STEPS_COUNT - 1);
	test(bh, MIN_ITEMS_COUNT, resident_pages_count);
	for (u = 1; u < TEST_STEPS_COUNT - 1; u++) {
		items_count = (unsigned) (MIN_ITEMS_COUNT * exp(k * u));
		test(bh, items_count, resident_pages_count);
	}
	test(bh, MAX_ITEMS_COUNT, resident_pages_count);
}

int
main(int argc, char **argv)
{
	struct binheap *bh;
	unsigned u;

	srandom(123);	/* generate predictive results */

	bh = binheap_new(NULL, cmp, update);
	AZ(binheap_root(bh));
	check_consistency(bh, 0);
	check_parent_child(bh, PARENT_CHILD_TESTS_COUNT);
	fprintf(stderr, "%u parent-child tests OK\n", PARENT_CHILD_TESTS_COUNT);

	fprintf(stderr, "\n* Tests with pagefault counter enabled\n");
	for (u = 1; u <= UINT_MAX / 2 && u <= MAX_RESIDENT_PAGES_COUNT; u *= 2)
		run_tests(bh, u);

	fprintf(stderr, "\n* Tests with pagefault counter disabled "
			"(aka 'perftests')\n");
	run_tests(bh, 0);
	return (0);
}
#endif
