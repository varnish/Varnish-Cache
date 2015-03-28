/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache/cache.h"

#include "hash/hash_slinger.h"

#include "cache/cache_director.h"
#include "vcli_priv.h"
#include "vtcp.h"
#include "vtim.h"

/*--------------------------------------------------------------------
 * Pass the request body to the backend
 */

static int __match_proto__(req_body_iter_f)
vbf_iter_req_body(struct req *req, void *priv, void *ptr, size_t l)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CAST_OBJ_NOTNULL(bo, priv, BUSYOBJ_MAGIC);

	if (l > 0) {
		bo->acct.bereq_bodybytes += V1L_Write(bo->wrk, ptr, l);
		if (V1L_Flush(bo->wrk))
			return (-1);
	}
	return (0);
}

/*--------------------------------------------------------------------
 * Send request, and receive the HTTP protocol response, but not the
 * response body.
 *
 * Return value:
 *	-1 failure, not retryable
 *	 0 success
 *	 1 failure which can be retried.
 */

int
V1F_fetch_hdr(struct worker *wrk, struct busyobj *bo, const char *def_host)
{
	struct http *hp;
	enum htc_status_e hs;
	int retry = 1;
	int j, first;
	ssize_t i;
	struct http_conn *htc;
	int do_chunked = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_ORNULL(bo->req, REQ_MAGIC);

	htc = bo->htc;
	hp = bo->bereq;

	/*
	 * Now that we know our backend, we can set a default Host:
	 * header if one is necessary.  This cannot be done in the VCL
	 * because the backend may be chosen by a director.
	 */
	if (!http_GetHdr(bo->bereq, H_Host, NULL) && def_host != NULL)
		http_PrintfHeader(hp, "Host: %s", def_host);

	if (bo->req != NULL &&
	    bo->req->req_body_status == REQ_BODY_WITHOUT_LEN) {
		http_PrintfHeader(hp, "Transfer-Encoding: chunked");
		do_chunked = 1;
	}

	(void)VTCP_blocking(htc->fd);	/* XXX: we should timeout instead */
	V1L_Reserve(wrk, wrk->aws, &htc->fd, bo->vsl, bo->t_prev);
	bo->acct.bereq_hdrbytes = HTTP1_Write(wrk, hp, HTTP1_Req);

	/* Deal with any message-body the request might (still) have */
	i = 0;

	if (bo->req != NULL) {
		if (do_chunked)
			V1L_Chunked(wrk);
		i = VRB_Iterate(bo->req, vbf_iter_req_body, bo);

		if (bo->req->req_body_status == REQ_BODY_TAKEN) {
			retry = -1;
		} else if (bo->req->req_body_status == REQ_BODY_FAIL) {
			VSLb(bo->vsl, SLT_FetchError,
			    "req.body read error: %d (%s)",
			    errno, strerror(errno));
			bo->req->doclose = SC_RX_BODY;
			retry = -1;
		}
		if (do_chunked)
			V1L_EndChunk(wrk);
	}

	j = V1L_FlushRelease(wrk);
	if (j != 0 || i < 0) {
		VSLb(bo->vsl, SLT_FetchError, "backend write error: %d (%s)",
		    errno, strerror(errno));
		VSLb_ts_busyobj(bo, "Bereq", W_TIM_real(wrk));
		bo->doclose = SC_TX_ERROR;
		return (retry);
	}
	VSLb_ts_busyobj(bo, "Bereq", W_TIM_real(wrk));

	VSC_C_main->backend_req++;

	/* Receive response */

	SES_RxInit(htc, bo->ws, cache_param->http_resp_size,
	    cache_param->http_resp_hdr_len);
	CHECK_OBJ_NOTNULL(htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(bo->htc, HTTP_CONN_MAGIC);

	VTCP_set_read_timeout(htc->fd, htc->first_byte_timeout);

	first = 1;
	do {
		hs = SES_Rx(htc, 0);
		if (hs == HTC_S_MORE)
			hs = HTTP1_Complete(htc);
		if (hs == HTC_S_OVERFLOW) {
			WS_ReleaseP(htc->ws, htc->rxbuf_b);
			bo->acct.beresp_hdrbytes +=
			    htc->rxbuf_e - htc->rxbuf_b;
			VSLb(bo->vsl, SLT_FetchError,
			    "http %sread error: overflow",
			    first ? "first " : "");
			bo->doclose = SC_RX_OVERFLOW;
			return (-1);
		}
		if (hs == HTC_S_EOF) {
			WS_ReleaseP(htc->ws, htc->rxbuf_b);
			bo->acct.beresp_hdrbytes +=
			    htc->rxbuf_e - htc->rxbuf_b;
			VSLb(bo->vsl, SLT_FetchError, "http %sread error: EOF",
			    first ? "first " : "");
			bo->doclose = SC_RX_TIMEOUT;
			return (retry);
		}
		if (first) {
			retry = -1;
			first = 0;
			VTCP_set_read_timeout(htc->fd,
			    htc->between_bytes_timeout);
		}
	} while (hs != HTC_S_COMPLETE);
	bo->acct.beresp_hdrbytes += htc->rxbuf_e - htc->rxbuf_b;

	hp = bo->beresp;

	if (HTTP1_DissectResponse(hp, htc)) {
		VSLb(bo->vsl, SLT_FetchError, "http format error");
		bo->doclose = SC_RX_JUNK;
		return (-1);
	}

	bo->doclose = http_DoConnection(hp);

	return (0);
}
