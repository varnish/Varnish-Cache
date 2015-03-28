/*-
 * Copyright (c) 2008-2011 Varnish Software AS
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
 * Persistent storage method
 *
 * XXX: Before we start the client or maybe after it stops, we should give the
 * XXX: stevedores a chance to examine their storage for consistency.
 *
 * XXX: Do we ever free the LRU-lists ?
 */

/*
 *
 * Overall layout:
 *
 *	struct smp_ident;		Identification and geometry
 *	sha256[...]			checksum of same
 *
 *	struct smp_sign;
 *	banspace_1;			First ban-space
 *	sha256[...]			checksum of same
 *
 *	struct smp_sign;
 *	banspace_2;			Second ban-space
 *	sha256[...]			checksum of same
 *
 *	struct smp_sign;
 *	struct smp_segment_1[N];	First Segment table
 *	sha256[...]			checksum of same
 *
 *	struct smp_sign;
 *	struct smp_segment_2[N];	Second Segment table
 *	sha256[...]			checksum of same
 *
 *	N segments {
 *		struct smp_sign;
 *		struct smp_object[M]	Objects in segment
 *		sha256[...]		checksum of same
 *		objspace
 *	}
 *
 */

/*
 * The identblock is located in the first sector of the storage space.
 * This is written once and not subsequently modified in normal operation.
 * It is immediately followed by a SHA256sum of the structure, as stored.
 */

struct smp_ident {
	char			ident[32];	/* Human readable ident
						 * so people and programs
						 * can tell what the file
						 * or device contains.
						 */

	uint32_t		byte_order;	/* 0x12345678 */

	uint32_t		size;		/* sizeof(struct smp_ident) */

	uint32_t		major_version;

	uint32_t		unique;

	uint32_t		align;		/* alignment in silo */

	uint32_t		granularity;	/* smallest ... in bytes */

	uint64_t		mediasize;	/* ... in bytes */

	uint64_t		stuff[6];	/* pointers to stuff */
#define	SMP_BAN1_STUFF		0
#define	SMP_BAN2_STUFF		1
#define	SMP_SEG1_STUFF		2
#define	SMP_SEG2_STUFF		3
#define	SMP_SPC_STUFF		4
#define	SMP_END_STUFF		5
};

/*
 * The size of smp_ident should be fixed and constant across all platforms.
 * We enforce that with the following #define and an assert in smp_init()
 */
#define SMP_IDENT_SIZE		112

#define SMP_IDENT_STRING	"Varnish Persistent Storage Silo"

/*
 * This is used to sign various bits on the disk.
 */

struct smp_sign {
	char			ident[8];
	uint32_t		unique;
	uint64_t		mapped;
	/* The length field is the length of the signed data only
	 * (does not include struct smp_sign) */
	uint64_t		length;		/* NB: Must be last */
};

#define SMP_SIGN_SPACE		(sizeof(struct smp_sign) + SHA256_LEN)

/*
 * A segment pointer.
 */

struct smp_segptr {
	uint64_t		offset;		/* rel to silo */
	uint64_t		length;		/* rel to offset */
	uint64_t		objlist;	/* rel to silo */
	uint32_t		lobjlist;	/* len of objlist */
};

/*
 * An object descriptor
 *
 * A positive ttl is obj.ttl with obj.grace being NAN
 * A negative ttl is - (obj.ttl + obj.grace)
 */

struct smp_object {
	uint8_t			hash[32];	/* really: DIGEST_LEN */
	struct exp		exp;
	uint32_t		__filler__;	/* -> align/8 on 32bit */
	double			ban;
	uint64_t		ptr;		/* rel to silo */
};

#define ASSERT_SILO_THREAD(sc) \
    do {assert(pthread_equal(pthread_self(), (sc)->thread));} while (0)

/*
 * Context for a signature.
 *
 * A signature is a sequence of bytes in the silo, signed by a SHA256 hash
 * which follows the bytes.
 *
 * The context structure allows us to append to a signature without
 * recalculating the entire SHA256 hash.
 */

struct smp_signctx {
	struct smp_sign		*ss;
	struct SHA256Context	ctx;
	uint32_t		unique;
	const char		*id;
};

/*
 * A space wrapped by a signature
 *
 * A signspace is a chunk of the silo that is wrapped by a
 * signature. It has attributes for size, so range checking can be
 * performed.
 *
 */

struct smp_signspace {
	struct smp_signctx	ctx;
	uint8_t			*start;
	uint64_t		size;
};

struct smp_sc;

/* XXX: name confusion with on-media version ? */
struct smp_seg {
	unsigned		magic;
#define SMP_SEG_MAGIC		0x45c61895

	struct smp_sc		*sc;
	struct lru		*lru;

	VTAILQ_ENTRY(smp_seg)	list;		/* on smp_sc.smp_segments */

	struct smp_segptr	p;

	unsigned		flags;
#define SMP_SEG_MUSTLOAD	(1 << 0)
#define SMP_SEG_LOADED		(1 << 1)

	uint32_t		nobj;		/* Number of objects */
	uint32_t		nalloc;		/* Allocations */
	uint32_t		nfixed;		/* How many fixed objects */

	/* Only for open segment */
	struct smp_object	*objs;		/* objdesc array */
	struct smp_signctx	ctx[1];
};

VTAILQ_HEAD(smp_seghead, smp_seg);

struct smp_sc {
	unsigned		magic;
#define SMP_SC_MAGIC		0x7b73af0a
	struct stevedore	*parent;

	pthread_t		bgthread;
	unsigned		flags;
#define SMP_SC_LOADED		(1 << 0)
#define SMP_SC_STOP		(1 << 1)

	const struct stevedore	*stevedore;
	int			fd;
	const char		*filename;
	off_t			mediasize;
	uintptr_t		align;
	uint32_t		granularity;
	uint32_t		unique;

	uint8_t			*base;

	struct smp_ident	*ident;

	struct smp_seghead	segments;
	struct smp_seg		*cur_seg;
	uint64_t		next_bot;	/* next alloc address bottom */
	uint64_t		next_top;	/* next alloc address top */

	uint64_t		free_offset;

	pthread_t		thread;

	VTAILQ_ENTRY(smp_sc)	list;

	struct smp_signctx	idn;
	struct smp_signspace	ban1;
	struct smp_signspace	ban2;
	struct smp_signspace	seg1;
	struct smp_signspace	seg2;

	struct ban		*tailban;

	struct lock		mtx;

	/* Cleaner metrics */

	unsigned		min_nseg;
	unsigned		aim_nseg;
	unsigned		max_nseg;

	uint64_t		min_segl;
	uint64_t		aim_segl;
	uint64_t		max_segl;

	uint64_t		free_reserve;
};

/*--------------------------------------------------------------------*/

/* Pointer round up/down & assert */
#define PRNDN(sc, x)	((void*)RDN2((uintptr_t)(x), sc->align))
#define PRNUP(sc, x)	((void*)RUP2((uintptr_t)(x), sc->align))
#define PASSERTALIGN(sc, x)	assert(PRNDN(sc, x) == (x))

/* Integer round up/down & assert */
#define IRNDN(sc, x)	RDN2(x, sc->align)
#define IRNUP(sc, x)	RUP2(x, sc->align)
#define IASSERTALIGN(sc, x)	assert(IRNDN(sc, x) == (x))

/*--------------------------------------------------------------------*/

#define ASSERT_PTR_IN_SILO(sc, ptr) \
	assert((const void*)(ptr) >= (const void*)((sc)->base) && \
	    (const void*)(ptr) < (const void *)((sc)->base + (sc)->mediasize))

/*--------------------------------------------------------------------*/

#define SIGN_DATA(ctx)	((void *)((ctx)->ss + 1))
#define SIGN_END(ctx)	((void *)((int8_t *)SIGN_DATA(ctx) + (ctx)->ss->length))

#define SIGNSPACE_DATA(spc)	(SIGN_DATA(&(spc)->ctx))
#define SIGNSPACE_FRONT(spc)	(SIGN_END(&(spc)->ctx))
#define SIGNSPACE_LEN(spc)	((spc)->ctx.ss->length)
#define SIGNSPACE_FREE(spc)	((spc)->size - SIGNSPACE_LEN(spc))

/* storage_persistent_mgt.c */

void smp_mgt_init(struct stevedore *parent, int ac, char * const *av);

/* storage_persistent_silo.c */

void smp_load_seg(struct worker *, const struct smp_sc *sc, struct smp_seg *sg);
void smp_new_seg(struct smp_sc *sc);
void smp_close_seg(struct smp_sc *sc, struct smp_seg *sg);
void smp_init_oc(struct objcore *oc, struct smp_seg *sg, unsigned objidx);
void smp_save_segs(struct smp_sc *sc);
extern const struct storeobj_methods smp_oc_methods;

/* storage_persistent_subr.c */

void smp_def_sign(const struct smp_sc *sc, struct smp_signctx *ctx,
    uint64_t off, const char *id);
int smp_chk_sign(struct smp_signctx *ctx);
void smp_append_sign(struct smp_signctx *ctx, const void *ptr, uint32_t len);
void smp_reset_sign(struct smp_signctx *ctx);
void smp_sync_sign(const struct smp_signctx *ctx);

void smp_def_signspace(const struct smp_sc *sc, struct smp_signspace *spc,
		       uint64_t off, uint64_t size, const char *id);
int smp_chk_signspace(struct smp_signspace *spc);
void smp_append_signspace(struct smp_signspace *spc, uint32_t len);
void smp_reset_signspace(struct smp_signspace *spc);
void smp_copy_signspace(struct smp_signspace *dst,
			const struct smp_signspace *src);
void smp_trunc_signspace(struct smp_signspace *spc, uint32_t len);
void smp_msync(void *addr, size_t length);

void smp_newsilo(struct smp_sc *sc);
int smp_valid_silo(struct smp_sc *sc);

/*--------------------------------------------------------------------
 * Caculate payload of some stuff
 */

static inline uint64_t
smp_stuff_len(const struct smp_sc *sc, unsigned stuff)
{
	uint64_t l;

	assert(stuff < SMP_END_STUFF);
	l = sc->ident->stuff[stuff + 1] - sc->ident->stuff[stuff];
	l -= SMP_SIGN_SPACE;
	return (l);
}

static inline uint64_t
smp_segend(const struct smp_seg *sg)
{

	return (sg->p.offset + sg->p.length);
}

static inline uint64_t
smp_spaceleft(const struct smp_sc *sc, const struct smp_seg *sg)
{

	IASSERTALIGN(sc, sc->next_bot);
	assert(sc->next_bot <= sc->next_top - IRNUP(sc, SMP_SIGN_SPACE));
	assert(sc->next_bot >= sg->p.offset);
	assert(sc->next_top < sg->p.offset + sg->p.length);
	return ((sc->next_top - sc->next_bot) - IRNUP(sc, SMP_SIGN_SPACE));
}
