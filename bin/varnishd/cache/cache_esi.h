/*-
 * Copyright (c) 2011 Varnish Software AS
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

#define	VEC_GZ	(0x21)
#define	VEC_V1	(0x40 + 1)
#define	VEC_V2	(0x40 + 2)
#define	VEC_V8	(0x40 + 8)
#define	VEC_C1	(0x50 + 1)
#define	VEC_C2	(0x50 + 2)
#define	VEC_C8	(0x50 + 8)
#define	VEC_S1	(0x60 + 1)
#define	VEC_S2	(0x60 + 2)
#define	VEC_S8	(0x60 + 8)
#define	VEC_INCL	'I'

typedef ssize_t vep_callback_t(struct vfp_ctx *, void *priv, ssize_t l,
    enum vgz_flag flg);

struct vep_state *VEP_Init(struct vfp_ctx *vc, const struct http *req,
    vep_callback_t *cb, void *cb_priv);
void VEP_Parse(struct vep_state *, const char *p, size_t l);
struct vsb *VEP_Finish(struct vep_state *);
