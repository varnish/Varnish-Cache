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
 */

/*
 * This macro can be used in .h files to isolate bits that the manager
 * should not (need to) see, such as pthread mutexes etc.
 */
#define VARNISH_CACHE_CHILD	1

#include "common.h"

#include "vapi/vsc_int.h"
#include "vapi/vsl_int.h"

#include <sys/socket.h>

#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#if defined(HAVE_EPOLL_CTL)
#include <sys/epoll.h>
#endif


#include "heritage.h"

enum body_status {
#define BODYSTATUS(U,l)	BS_##U,
#include "tbl/body_status.h"
#undef BODYSTATUS
};

static inline const char *
body_status(enum body_status e)
{
	switch(e) {
#define BODYSTATUS(U,l)	case BS_##U: return (#l);
#include "tbl/body_status.h"
#undef BODYSTATUS
	default:
		return ("?");
	}
}

/*
 * NB: HDR_STATUS is only used in cache_http.c, everybody else uses the
 * http->status integer field.
 */

enum {
	/* Fields from the first line of HTTP proto */
	HTTP_HDR_REQ,
	HTTP_HDR_URL,
	HTTP_HDR_PROTO,
	HTTP_HDR_STATUS,
	HTTP_HDR_RESPONSE,
	/* HTTP header lines */
	HTTP_HDR_FIRST,
};

struct SHA256Context;
struct VSC_C_lck;
struct ban;
struct busyobj;
struct cli;
struct cli_proto;
struct director;
struct iovec;
struct objcore;
struct object;
struct objhead;
struct pool;
struct sess;
struct sesspool;
struct vbc;
struct vef_priv;
struct vrt_backend;
struct vsb;
struct waitinglist;

#define DIGEST_LEN		32

/* Name of transient storage */
#define TRANSIENT_STORAGE	"Transient"

/*--------------------------------------------------------------------
 * Pointer aligment magic
 */

#define PALGN		(sizeof(void *) - 1)
#define PAOK(p)		(((uintptr_t)(p) & PALGN) == 0)
#define PRNDDN(p)	((uintptr_t)(p) & ~PALGN)
#define PRNDUP(p)	(((uintptr_t)(p) + PALGN) & ~PALGN)

/*--------------------------------------------------------------------*/

typedef struct {
	char			*b;
	char			*e;
} txt;

/*--------------------------------------------------------------------*/

enum step {
#define STEP(l, u)	STP_##u,
#include "tbl/steps.h"
#undef STEP
};

/*--------------------------------------------------------------------
 * Workspace structure for quick memory allocation.
 */

struct ws {
	unsigned		magic;
#define WS_MAGIC		0x35fac554
	unsigned		overflow;	/* workspace overflowed */
	const char		*id;		/* identity */
	char			*s;		/* (S)tart of buffer */
	char			*f;		/* (F)ree pointer */
	char			*r;		/* (R)eserved length */
	char			*e;		/* (E)nd of buffer */
};

/*--------------------------------------------------------------------
 * HTTP Request/Response/Header handling structure.
 */

enum httpwhence {
	HTTP_Rx	 = 1,
	HTTP_Tx  = 2,
	HTTP_Obj = 3
};

/* NB: remember to update http_Copy() if you add fields */
struct http {
	unsigned		magic;
#define HTTP_MAGIC		0x6428b5c9

	enum httpwhence		logtag;

	struct ws		*ws;
	txt			*hd;
	unsigned char		*hdf;
#define HDF_FILTER		(1 << 0)	/* Filtered by Connection */
	uint16_t		shd;		/* Size of hd space */
	uint16_t		nhd;		/* Next free hd */
	uint16_t		status;
	uint8_t			protover;
	uint8_t			conds;		/* If-* headers present */
};

/*--------------------------------------------------------------------
 * HTTP Protocol connection structure
 */

struct http_conn {
	unsigned		magic;
#define HTTP_CONN_MAGIC		0x3e19edd1

	int			fd;
	unsigned		vsl_id;
	unsigned		maxbytes;
	unsigned		maxhdr;
	struct ws		*ws;
	txt			rxbuf;
	txt			pipeline;
	const char		*error;
};

/*--------------------------------------------------------------------*/

struct acct {
	double			first;
#define ACCT(foo)	uint64_t	foo;
#include "tbl/acct_fields.h"
#undef ACCT
};

/*--------------------------------------------------------------------*/

#define L0(t, n)
#define L1(t, n)		t n;
#define VSC_F(n, t, l, f, e,d)	L##l(t, n)
#define VSC_DO_MAIN
struct dstat {
#include "tbl/vsc_fields.h"
};
#undef VSC_F
#undef VSC_DO_MAIN
#undef L0
#undef L1

/* Fetch processors --------------------------------------------------*/

typedef void vfp_begin_f(struct sess *, size_t );
typedef int vfp_bytes_f(struct sess *, struct http_conn *, ssize_t);
typedef int vfp_end_f(struct sess *sp);

struct vfp {
	vfp_begin_f	*begin;
	vfp_bytes_f	*bytes;
	vfp_end_f	*end;
};

extern struct vfp vfp_gunzip;
extern struct vfp vfp_gzip;
extern struct vfp vfp_testgzip;
extern struct vfp vfp_esi;

/*--------------------------------------------------------------------*/

struct exp {
	double			ttl;
	double			grace;
	double			keep;
	double			age;
	double			entered;
};

/*--------------------------------------------------------------------*/

struct wrw {
	int			*wfd;
	unsigned		werr;	/* valid after WRW_Flush() */
	struct iovec		*iov;
	unsigned		siov;
	unsigned		niov;
	ssize_t			liov;
	ssize_t			cliov;
	unsigned		ciov;	/* Chunked header marker */
};

/*--------------------------------------------------------------------*/

struct stream_ctx {
	unsigned		magic;
#define STREAM_CTX_MAGIC	0x8213728b

	struct vgz		*vgz;
	void			*obuf;
	ssize_t			obuf_len;
	ssize_t			obuf_ptr;

	/* Next byte we will take from storage */
	ssize_t			stream_next;

	/* First byte of storage if we free it as we go (pass) */
	ssize_t			stream_front;
};

/*--------------------------------------------------------------------*/

struct wrk_accept {
	unsigned		magic;
#define WRK_ACCEPT_MAGIC	0x8c4b4d59

	/* Accept stuff */
	struct sockaddr_storage	acceptaddr;
	socklen_t		acceptaddrlen;
	int			acceptsock;
	struct listen_sock	*acceptlsock;
};

/*--------------------------------------------------------------------*/

struct worker {
	unsigned		magic;
#define WORKER_MAGIC		0x6391adcf
	struct pool		*pool;
	struct objhead		*nobjhead;
	struct objcore		*nobjcore;
	struct waitinglist	*nwaitinglist;
	struct busyobj		*nbusyobj;
	void			*nhashpriv;
	struct dstat		stats;

	/* Pool stuff */
	double			lastused;

	struct wrw		wrw;

	pthread_cond_t		cond;

	VTAILQ_ENTRY(worker)	list;
	struct sess		*sp;

	struct VCL_conf		*vcl;

	uint32_t		*wlb, *wlp, *wle;
	unsigned		wlr;

	/* Lookup stuff */
	struct SHA256Context	*sha256ctx;

	struct http_conn	htc[1];
	struct ws		ws[1];
	struct http		*bereq;
	struct http		*beresp;
	struct http		*resp;

	struct exp		exp;

	/* This is only here so VRT can find it */
	const char		*storage_hint;

	/* Fetch stuff */
	enum body_status	body_status;
	struct vfp		*vfp;
	struct vgz		*vgz_rx;
	struct vef_priv		*vef_priv;
	unsigned		do_stream;
	unsigned		do_esi;
	unsigned		do_gzip;
	unsigned		is_gzip;
	unsigned		do_gunzip;
	unsigned		is_gunzip;
	unsigned		do_close;
	char			*h_content_length;

	/* Stream state */
	struct stream_ctx	*sctx;

	/* ESI stuff */
	struct vep_state	*vep;
	int			gzip_resp;
	ssize_t			l_crc;
	uint32_t		crc;

	/* Timeouts */
	double			connect_timeout;
	double			first_byte_timeout;
	double			between_bytes_timeout;

	/* Delivery mode */
	unsigned		res_mode;
#define RES_LEN			(1<<1)
#define RES_EOF			(1<<2)
#define RES_CHUNKED		(1<<3)
#define RES_ESI			(1<<4)
#define RES_ESI_CHILD		(1<<5)
#define RES_GUNZIP		(1<<6)

	/* Temporary accounting */
	struct acct		acct_tmp;
};

/* LRU ---------------------------------------------------------------*/

struct lru {
	unsigned		magic;
#define LRU_MAGIC		0x3fec7bb0
	VTAILQ_HEAD(,objcore)	lru_head;
	struct lock		mtx;
};

/* Storage -----------------------------------------------------------*/

struct storage {
	unsigned		magic;
#define STORAGE_MAGIC		0x1a4e51c0

#ifdef SENDFILE_WORKS
	int			fd;
	off_t			where;
#endif

	VTAILQ_ENTRY(storage)	list;
	struct stevedore	*stevedore;
	void			*priv;

	unsigned char		*ptr;
	unsigned		len;
	unsigned		space;
};

/* Object core structure ---------------------------------------------
 * Objects have sideways references in the binary heap and the LRU list
 * and we want to avoid paging in a lot of objects just to move them up
 * or down the binheap or to move a unrelated object on the LRU list.
 * To avoid this we use a proxy object, objcore, to hold the relevant
 * housekeeping fields parts of an object.
 */

typedef struct object *getobj_f(struct worker *wrk, struct objcore *oc);
typedef void updatemeta_f(struct objcore *oc);
typedef void freeobj_f(struct objcore *oc);
typedef struct lru *getlru_f(const struct objcore *oc);

struct objcore_methods {
	getobj_f	*getobj;
	updatemeta_f	*updatemeta;
	freeobj_f	*freeobj;
	getlru_f	*getlru;
};

struct objcore {
	unsigned		magic;
#define OBJCORE_MAGIC		0x4d301302
	unsigned		refcnt;
	struct objcore_methods	*methods;
	void			*priv;
	unsigned		priv2;
	struct objhead		*objhead;
	struct busyobj		*busyobj;
	struct binheap_entry	*exp_entry;
	unsigned		flags;
#define OC_F_BUSY		(1<<1)
#define OC_F_PASS		(1<<2)
#define OC_F_LRUDONTMOVE	(1<<4)
#define OC_F_PRIV		(1<<5)		/* Stevedore private flag */
	VTAILQ_ENTRY(objcore)	list;
	VTAILQ_ENTRY(objcore)	lru_list;
	VTAILQ_ENTRY(objcore)	ban_list;
	struct ban		*ban;
};

static inline struct object *
oc_getobj(struct worker *wrk, struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AZ(oc->flags & OC_F_BUSY);
	AN(oc->methods);
	AN(oc->methods->getobj);
	return (oc->methods->getobj(wrk, oc));
}

static inline void
oc_updatemeta(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(oc->methods);
	if (oc->methods->updatemeta != NULL)
		oc->methods->updatemeta(oc);
}

static inline void
oc_freeobj(struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(oc->methods);
	AN(oc->methods->freeobj);
	oc->methods->freeobj(oc);
}

static inline struct lru *
oc_getlru(const struct objcore *oc)
{

	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);
	AN(oc->methods);
	AN(oc->methods->getlru);
	return (oc->methods->getlru(oc));
}

/* Busy Object structure ---------------------------------------------*/

struct busyobj {
	unsigned		magic;
#define BUSYOBJ_MAGIC		0x23b95567
	uint8_t			*vary;
};

/* Object structure --------------------------------------------------*/

VTAILQ_HEAD(storagehead, storage);

struct object {
	unsigned		magic;
#define OBJECT_MAGIC		0x32851d42
	unsigned		xid;
	struct storage		*objstore;
	struct objcore		*objcore;

	struct ws		ws_o[1];

	uint8_t			*vary;
	unsigned		hits;
	uint16_t		response;

	/* XXX: make bitmap */
	uint8_t			gziped;
	/* Bit positions in the gzip stream */
	ssize_t			gzip_start;
	ssize_t			gzip_last;
	ssize_t			gzip_stop;

	ssize_t			len;

	struct exp		exp;

	double			last_modified;
	double			last_lru;

	struct http		*http;

	struct storagehead	store;

	struct storage		*esidata;

	double			last_use;

};

/* -------------------------------------------------------------------*/

struct sess {
	unsigned		magic;
#define SESS_MAGIC		0x2c2f9c5a
	int			fd;
	unsigned		vsl_id;
	unsigned		xid;

	int			restarts;
	int			esi_level;
	int			disable_esi;

	uint8_t			hash_ignore_busy;
	uint8_t			hash_always_miss;

	struct worker		*wrk;

	socklen_t		sockaddrlen;
	socklen_t		mysockaddrlen;
	struct sockaddr_storage	sockaddr;
	struct sockaddr_storage	mysockaddr;
	struct listen_sock	*mylsock;

	/* formatted ascii client address */
	char			*addr;
	char			*port;
	char			*client_identity;

	/* HTTP request */
	const char		*doclose;
	struct http		*http;
	struct http		*http0;

	struct ws		ws[1];
	char			*ws_ses;	/* WS above session data */
	char			*ws_req;	/* WS above request data */

	unsigned char		digest[DIGEST_LEN];

	/* Built Vary string */
	uint8_t			*vary_b;
	uint8_t			*vary_l;
	uint8_t			*vary_e;

	struct http_conn	htc[1];

	/* Timestamps, all on TIM_real() timescale */
	double			t_open;
	double			t_req;
	double			t_resp;
	double			t_end;

	/* Acceptable grace period */
	struct exp		exp;

	enum step		step;
	unsigned		cur_method;
	unsigned		handling;
	unsigned char		sendbody;
	unsigned char		wantbody;
	uint16_t		err_code;
	const char		*err_reason;

	VTAILQ_ENTRY(sess)	list;

	struct director		*director;
	struct vbc		*vbc;
	struct object		*obj;
	struct objcore		*objcore;
	struct VCL_conf		*vcl;

	/* The busy objhead we sleep on */
	struct objhead		*hash_objhead;

	/* Various internal stuff */
	struct sessmem		*mem;

	VTAILQ_ENTRY(sess)	poollist;
	uint64_t		req_bodybytes;
	struct acct		acct_ses;

#if defined(HAVE_EPOLL_CTL)
	struct epoll_event ev;
#endif
};

/* Prototypes etc ----------------------------------------------------*/

/* cache_acceptor.c */
void VCA_Prep(struct sess *sp);
void VCA_Init(void);
void VCA_Shutdown(void);
int VCA_Accept(struct listen_sock *ls, struct wrk_accept *wa);
void VCA_SetupSess(struct worker *w);
void VCA_FailSess(struct worker *w);

/* cache_backend.c */
void VBE_UseHealth(const struct director *vdi);

struct vbc *VDI_GetFd(const struct director *, struct sess *sp);
int VDI_Healthy(const struct director *, const struct sess *sp);
void VDI_CloseFd(struct sess *sp);
void VDI_RecycleFd(struct sess *sp);
void VDI_AddHostHeader(const struct sess *sp);
void VBE_Poll(void);

/* cache_backend_cfg.c */
void VBE_Init(void);
struct backend *VBE_AddBackend(struct cli *cli, const struct vrt_backend *vb);

/* cache_backend_poll.c */
void VBP_Init(void);

/* cache_ban.c */
struct ban *BAN_New(void);
int BAN_AddTest(struct cli *, struct ban *, const char *, const char *,
    const char *);
void BAN_Free(struct ban *b);
void BAN_Insert(struct ban *b);
void BAN_Init(void);
void BAN_NewObjCore(struct objcore *oc);
void BAN_DestroyObj(struct objcore *oc);
int BAN_CheckObject(struct object *o, const struct sess *sp);
void BAN_Reload(const uint8_t *ban, unsigned len);
struct ban *BAN_TailRef(void);
void BAN_Compile(void);
struct ban *BAN_RefBan(struct objcore *oc, double t0, const struct ban *tail);
void BAN_TailDeref(struct ban **ban);
double BAN_Time(const struct ban *ban);

/* cache_center.c [CNT] */
void CNT_Session(struct sess *sp);
void CNT_Init(void);

/* cache_cli.c [CLI] */
void CLI_Init(void);
void CLI_Run(void);
void CLI_AddFuncs(struct cli_proto *p);
extern pthread_t cli_thread;
#define ASSERT_CLI() do {assert(pthread_self() == cli_thread);} while (0)

/* cache_expiry.c */
void EXP_Clr(struct exp *e);
double EXP_Get_ttl(const struct exp *e);
double EXP_Get_grace(const struct exp *e);
double EXP_Get_keep(const struct exp *e);
void EXP_Set_ttl(struct exp *e, double v);
void EXP_Set_grace(struct exp *e, double v);
void EXP_Set_keep(struct exp *e, double v);

double EXP_Ttl(const struct sess *, const struct object*);
double EXP_Grace(const struct sess *, const struct object*);
void EXP_Insert(struct object *o);
void EXP_Inject(struct objcore *oc, struct lru *lru, double when);
void EXP_Init(void);
void EXP_Rearm(const struct object *o);
int EXP_Touch(struct objcore *oc);
int EXP_NukeOne(const struct sess *sp, struct lru *lru);

/* cache_fetch.c */
struct storage *FetchStorage(const struct sess *sp, ssize_t sz);
int FetchHdr(struct sess *sp);
int FetchBody(struct sess *sp);
int FetchReqBody(struct sess *sp);
void Fetch_Init(void);

/* cache_gzip.c */
struct vgz;

enum vgz_flag { VGZ_NORMAL, VGZ_ALIGN, VGZ_RESET, VGZ_FINISH };
struct vgz *VGZ_NewUngzip(struct sess *sp, const char *id);
struct vgz *VGZ_NewGzip(struct sess *sp, const char *id);
void VGZ_Ibuf(struct vgz *, const void *, ssize_t len);
int VGZ_IbufEmpty(const struct vgz *vg);
void VGZ_Obuf(struct vgz *, void *, ssize_t len);
int VGZ_ObufFull(const struct vgz *vg);
int VGZ_ObufStorage(const struct sess *sp, struct vgz *vg);
int VGZ_Gzip(struct vgz *, const void **, size_t *len, enum vgz_flag);
int VGZ_Gunzip(struct vgz *, const void **, size_t *len);
void VGZ_Destroy(struct vgz **);
void VGZ_UpdateObj(const struct vgz*, struct object *);
int VGZ_WrwGunzip(const struct sess *, struct vgz *, const void *ibuf,
    ssize_t ibufl, char *obuf, ssize_t obufl, ssize_t *obufp);

/* Return values */
#define VGZ_ERROR	-1
#define VGZ_OK		0
#define VGZ_END		1
#define VGZ_STUCK	2

/* cache_http.c */
unsigned HTTP_estimate(unsigned nhttp);
void HTTP_Copy(struct http *to, const struct http * const fm);
struct http *HTTP_create(void *p, uint16_t nhttp);
const char *http_StatusMessage(unsigned);
unsigned http_EstimateWS(const struct http *fm, unsigned how, uint16_t *nhd);
void HTTP_Init(void);
void http_ClrHeader(struct http *to);
unsigned http_Write(struct worker *w, unsigned vsl_id, const struct http *hp,
    int resp);
void http_CopyResp(struct http *to, const struct http *fm);
void http_SetResp(struct http *to, const char *proto, uint16_t status,
    const char *response);
void http_FilterFields(struct worker *w, unsigned vsl_id, struct http *to,
    const struct http *fm, unsigned how);
void http_FilterHeader(const struct sess *sp, unsigned how);
void http_PutProtocol(struct worker *w, unsigned vsl_id, const struct http *to,
    const char *protocol);
void http_PutStatus(struct http *to, uint16_t status);
void http_PutResponse(struct worker *w, unsigned vsl_id, const struct http *to,
    const char *response);
void http_PrintfHeader(struct worker *w, unsigned vsl_id, struct http *to,
    const char *fmt, ...);
void http_SetHeader(struct worker *w, unsigned vsl_id, struct http *to,
    const char *hdr);
void http_SetH(const struct http *to, unsigned n, const char *fm);
void http_ForceGet(const struct http *to);
void http_Setup(struct http *ht, struct ws *ws);
int http_GetHdr(const struct http *hp, const char *hdr, char **ptr);
int http_GetHdrData(const struct http *hp, const char *hdr,
    const char *field, char **ptr);
int http_GetHdrField(const struct http *hp, const char *hdr,
    const char *field, char **ptr);
double http_GetHdrQ(const struct http *hp, const char *hdr, const char *field);
uint16_t http_GetStatus(const struct http *hp);
const char *http_GetReq(const struct http *hp);
int http_HdrIs(const struct http *hp, const char *hdr, const char *val);
uint16_t http_DissectRequest(struct sess *sp);
uint16_t http_DissectResponse(struct worker *w, const struct http_conn *htc,
    struct http *sp);
const char *http_DoConnection(const struct http *hp);
void http_CopyHome(struct worker *w, unsigned vsl_id, const struct http *hp);
void http_Unset(struct http *hp, const char *hdr);
void http_CollectHdr(struct http *hp, const char *hdr);

/* cache_httpconn.c */
void HTC_Init(struct http_conn *htc, struct ws *ws, int fd, unsigned vsl_id,
    unsigned maxbytes, unsigned maxhdr);
int HTC_Reinit(struct http_conn *htc);
int HTC_Rx(struct http_conn *htc);
ssize_t HTC_Read(struct http_conn *htc, void *d, size_t len);
int HTC_Complete(struct http_conn *htc);

#define HTTPH(a, b, c, d, e, f, g) extern char b[];
#include "tbl/http_headers.h"
#undef HTTPH

/* cache_main.c */
void THR_SetName(const char *name);
const char* THR_GetName(void);
void THR_SetSession(const struct sess *sp);
const struct sess * THR_GetSession(void);

/* cache_lck.c */

/* Internal functions, call only through macros below */
void Lck__Lock(struct lock *lck, const char *p, const char *f, int l);
void Lck__Unlock(struct lock *lck, const char *p, const char *f, int l);
int Lck__Trylock(struct lock *lck, const char *p, const char *f, int l);
void Lck__New(struct lock *lck, struct VSC_C_lck *, const char *);
void Lck__Assert(const struct lock *lck, int held);

/* public interface: */
void LCK_Init(void);
void Lck_Delete(struct lock *lck);
int Lck_CondWait(pthread_cond_t *cond, struct lock *lck, struct timespec *ts);

#define Lck_New(a, b) Lck__New(a, b, #b)
#define Lck_Lock(a) Lck__Lock(a, __func__, __FILE__, __LINE__)
#define Lck_Unlock(a) Lck__Unlock(a, __func__, __FILE__, __LINE__)
#define Lck_Trylock(a) Lck__Trylock(a, __func__, __FILE__, __LINE__)
#define Lck_AssertHeld(a) Lck__Assert(a, 1)

#define LOCK(nam) extern struct VSC_C_lck *lck_##nam;
#include "tbl/locks.h"
#undef LOCK

/* cache_panic.c */
void PAN_Init(void);

/* cache_pipe.c */
void PipeSession(struct sess *sp);

/* cache_pool.c */
void Pool_Init(void);
void Pool_Work_Thread(void *priv, struct worker *w);
void Pool_Wait(struct sess *sp);
int Pool_Schedule(struct pool *pp, struct sess *sp);

#define WRW_IsReleased(w)	((w)->wrw.wfd == NULL)
int WRW_Error(const struct worker *w);
void WRW_Chunked(struct worker *w);
void WRW_EndChunk(struct worker *w);
void WRW_Reserve(struct worker *w, int *fd);
unsigned WRW_Flush(struct worker *w);
unsigned WRW_FlushRelease(struct worker *w);
unsigned WRW_Write(struct worker *w, const void *ptr, int len);
unsigned WRW_WriteH(struct worker *w, const txt *hh, const char *suf);
#ifdef SENDFILE_WORKS
void WRW_Sendfile(struct worker *w, int fd, off_t off, unsigned len);
#endif  /* SENDFILE_WORKS */

/* cache_session.c [SES] */
struct sess *SES_New(struct worker *wrk, struct sesspool *pp);
struct sess *SES_Alloc(void);
void SES_Close(struct sess *sp, const char *reason);
void SES_Delete(struct sess *sp, const char *reason);
void SES_Charge(struct sess *sp);
struct sesspool *SES_NewPool(struct pool *pp);
void SES_DeletePool(struct sesspool *sp, struct worker *wrk);
int SES_Schedule(struct sess *sp);


/* cache_shmlog.c */
void VSL_Init(void);
void *VSM_Alloc(unsigned size, const char *class, const char *type,
    const char *ident);
void VSM_Free(const void *ptr);
#ifdef VSL_ENDMARKER
void VSL(enum VSL_tag_e tag, int id, const char *fmt, ...);
void WSLR(struct worker *w, enum VSL_tag_e tag, int id, txt t);
void WSL(struct worker *w, enum VSL_tag_e tag, int id, const char *fmt, ...);
void WSL_Flush(struct worker *w, int overflow);

#define DSL(flag, tag, id, ...)					\
	do {							\
		if (params->diag_bitmap & (flag))		\
			VSL((tag), (id), __VA_ARGS__);		\
	} while (0)

#define WSP(sess, tag, ...)					\
	WSL((sess)->wrk, tag, (sess)->vsl_id, __VA_ARGS__)

#define WSPR(sess, tag, txt)					\
	WSLR((sess)->wrk, tag, (sess)->vsl_id, txt)

#define INCOMPL() do {							\
	VSL(SLT_Debug, 0, "INCOMPLETE AT: %s(%d)", __func__, __LINE__); \
	fprintf(stderr,							\
	    "INCOMPLETE AT: %s(%d)\n",					\
	    (const char *)__func__, __LINE__);				\
	abort();							\
	} while (0)
#endif

/* cache_response.c */
void RES_BuildHttp(const struct sess *sp);
void RES_WriteObj(struct sess *sp);
void RES_StreamStart(struct sess *sp);
void RES_StreamEnd(struct sess *sp);
void RES_StreamPoll(const struct sess *sp);

/* cache_vary.c */
struct vsb *VRY_Create(const struct sess *sp, const struct http *hp);
int VRY_Match(struct sess *sp, const uint8_t *vary);
void VRY_Validate(const uint8_t *vary);

/* cache_vcl.c */
void VCL_Init(void);
void VCL_Refresh(struct VCL_conf **vcc);
void VCL_Rel(struct VCL_conf **vcc);
void VCL_Poll(void);

#define VCL_MET_MAC(l,u,b) void VCL_##l##_method(struct sess *);
#include "tbl/vcl_returns.h"
#undef VCL_MET_MAC

/* cache_vrt.c */

char *VRT_String(struct ws *ws, const char *h, const char *p, va_list ap);
char *VRT_StringList(char *d, unsigned dl, const char *p, va_list ap);

void ESI_Deliver(struct sess *);
void ESI_DeliverChild(const struct sess *);

/* cache_vrt_vmod.c */
void VMOD_Init(void);

/* cache_wrk.c */

void WRK_Init(void);
int WRK_TrySumStat(struct worker *w);
void WRK_SumStat(struct worker *w);
void *WRK_thread(void *priv);
typedef void *bgthread_t(struct sess *, void *priv);
void WRK_BgThread(pthread_t *thr, const char *name, bgthread_t *func,
    void *priv);

/* cache_ws.c */

void WS_Init(struct ws *ws, const char *id, void *space, unsigned len);
unsigned WS_Reserve(struct ws *ws, unsigned bytes);
void WS_Release(struct ws *ws, unsigned bytes);
void WS_ReleaseP(struct ws *ws, char *ptr);
void WS_Assert(const struct ws *ws);
void WS_Reset(struct ws *ws, char *p);
char *WS_Alloc(struct ws *ws, unsigned bytes);
char *WS_Dup(struct ws *ws, const char *);
char *WS_Snapshot(struct ws *ws);
unsigned WS_Free(const struct ws *ws);

/* rfc2616.c */
void RFC2616_Ttl(const struct sess *sp);
enum body_status RFC2616_Body(const struct sess *sp);
unsigned RFC2616_Req_Gzip(const struct sess *sp);
int RFC2616_Do_Cond(const struct sess *sp);

/* stevedore.c */
struct object *STV_NewObject(struct sess *sp, const char *hint, unsigned len,
    struct exp *, uint16_t nhttp);
struct storage *STV_alloc(const struct sess *sp, size_t size);
void STV_trim(struct storage *st, size_t size);
void STV_free(struct storage *st);
void STV_open(void);
void STV_close(void);
void STV_Freestore(struct object *o);

/* storage_synth.c */
struct vsb *SMS_Makesynth(struct object *obj);
void SMS_Finish(struct object *obj);
void SMS_Init(void);

/* storage_persistent.c */
void SMP_Init(void);
void SMP_Ready(void);
void SMP_NewBan(const uint8_t *ban, unsigned len);

/*
 * A normal pointer difference is signed, but we never want a negative value
 * so this little tool will make sure we don't get that.
 */

static inline unsigned
pdiff(const void *b, const void *e)
{

	assert(b <= e);
	return
	    ((unsigned)((const unsigned char *)e - (const unsigned char *)b));
}

static inline void
Tcheck(const txt t)
{

	AN(t.b);
	AN(t.e);
	assert(t.b <= t.e);
}

/*
 * unsigned length of a txt
 */

static inline unsigned
Tlen(const txt t)
{

	Tcheck(t);
	return ((unsigned)(t.e - t.b));
}

static inline void
Tadd(txt *t, const char *p, int l)
{
	Tcheck(*t);

	if (l <= 0) {
	} if (t->b + l < t->e) {
		memcpy(t->b, p, l);
		t->b += l;
	} else {
		t->b = t->e;
	}
}

static inline void
AssertObjBusy(const struct object *o)
{
	AN(o->objcore);
	AN (o->objcore->flags & OC_F_BUSY);
}

static inline void
AssertObjCorePassOrBusy(const struct objcore *oc)
{
	if (oc != NULL)
		AN (oc->flags & OC_F_BUSY);
}
