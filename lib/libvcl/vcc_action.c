/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2008 Linpro AS
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
 * $Id$
 */

#include "config.h"

#include <stdio.h>

#include "config.h"
#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------*/

#define VCL_RET_MAC(l,u,b,i) 				\
static void						\
parse_##l(struct tokenlist *tl)				\
{							\
							\
	Fb(tl, 1, "VRT_done(sp, VCL_RET_%s);\n", #u); 	\
	vcc_ProcAction(tl->curproc, i, tl->t); 		\
	vcc_NextToken(tl);				\
}

#include "vcl_returns.h"
#undef VCL_RET_MAC

/*--------------------------------------------------------------------*/

static void
parse_restart_real(struct tokenlist *tl)
{
	struct token *t1;
	
	t1 = VTAILQ_NEXT(tl->t, list);
	if (t1->tok == ID && vcc_IdIs(t1, "rollback")) {
		Fb(tl, 1, "VRT_Rollback(sp);\n");
		vcc_NextToken(tl);
	} else if (t1->tok != ';') {
		vsb_printf(tl->sb, "Expected \"rollback\" or semicolon.\n");
		vcc_ErrWhere(tl, t1);
		ERRCHK(tl);
	}
	parse_restart(tl);
}

/*--------------------------------------------------------------------*/

static void
parse_call(struct tokenlist *tl)
{

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	vcc_AddCall(tl, tl->t);
	vcc_AddRef(tl, tl->t, R_FUNC);
	Fb(tl, 1, "if (VGC_function_%.*s(sp))\n", PF(tl->t));
	Fb(tl, 1, "\treturn (1);\n");
	vcc_NextToken(tl);
	return;
}

/*--------------------------------------------------------------------*/

static void
parse_error(struct tokenlist *tl)
{
	struct var *vp;

	vcc_NextToken(tl);
	if (tl->t->tok == VAR) {
		vp = vcc_FindVar(tl, tl->t, vcc_vars);
		ERRCHK(tl);
		assert(vp != NULL);
		if (vp->fmt == INT) {
			Fb(tl, 1, "VRT_error(sp, %s", vp->rname);
			vcc_NextToken(tl);
		} else {
			Fb(tl, 1, "VRT_error(sp, 0");
		}
	} else if (tl->t->tok == CNUM) {
		Fb(tl, 1, "VRT_error(sp, %u", vcc_UintVal(tl));
		vcc_NextToken(tl);
	} else
		Fb(tl, 1, "VRT_error(sp, 0");
	if (tl->t->tok == CSTR) {
		Fb(tl, 0, ", %.*s", PF(tl->t));
		vcc_NextToken(tl);
	} else if (tl->t->tok == VAR) {
		Fb(tl, 0, ", ");
		if (!vcc_StringVal(tl)) {
			ERRCHK(tl);
			vcc_ExpectedStringval(tl);
			return;
		}
	} else {
		Fb(tl, 0, ", (const char *)0");
	}
	Fb(tl, 0, ");\n");
	Fb(tl, 1, "VRT_done(sp, VCL_RET_ERROR);\n");
}

/*--------------------------------------------------------------------*/

static void
illegal_assignment(const struct tokenlist *tl, const char *type)
{

	vsb_printf(tl->sb, "Invalid assignment operator ");
	vcc_ErrToken(tl, tl->t);
	vsb_printf(tl->sb,
	    " only '=' is legal for %s\n", type);
}

static void
check_writebit(struct tokenlist *tl, const struct var *vp)
{

	if (vp->access == V_RW || vp->access == V_WO)
		return;
	vsb_printf(tl->sb, "Variable %.*s cannot be modified.\n", PF(tl->t));
	vcc_ErrWhere(tl, tl->t);
}

static void
parse_set(struct tokenlist *tl)
{
	struct var *vp;
	struct token *at, *vt;

	vcc_NextToken(tl);
	ExpectErr(tl, VAR);
	vt = tl->t;
	vp = vcc_FindVar(tl, tl->t, vcc_vars);
	ERRCHK(tl);
	assert(vp != NULL);
	check_writebit(tl, vp);
	ERRCHK(tl);
	Fb(tl, 1, "%s", vp->lname);
	vcc_NextToken(tl);
	switch (vp->fmt) {
	case INT:
	case SIZE:
	case RATE:
	case TIME:
	case RTIME:
	case FLOAT:
		if (tl->t->tok != '=')
			Fb(tl, 0, "%s %c ", vp->rname, *tl->t->b);
		at = tl->t;
		vcc_NextToken(tl);
		switch (at->tok) {
		case T_MUL:
		case T_DIV:
			Fb(tl, 0, "%g", vcc_DoubleVal(tl));
			break;
		case T_INCR:
		case T_DECR:
		case '=':
			if (vp->fmt == TIME)
				vcc_TimeVal(tl);
			else if (vp->fmt == RTIME)
				vcc_RTimeVal(tl);
			else if (vp->fmt == SIZE)
				vcc_SizeVal(tl);
			else if (vp->fmt == RATE)
				vcc_RateVal(tl);
			else if (vp->fmt == FLOAT)
				Fb(tl, 0, "%g", vcc_DoubleVal(tl));
			else if (vp->fmt == INT) {
				Fb(tl, 0, "%u", vcc_UintVal(tl));
				vcc_NextToken(tl);
			} else {
				vsb_printf(tl->sb, "Cannot assign this variable type.\n");
				vcc_ErrWhere(tl, vt);
				return;
			}
			break;
		default:
			vsb_printf(tl->sb, "Invalid assignment operator.\n");
			vcc_ErrWhere(tl, at);
			return;
		}
		Fb(tl, 0, ");\n");
		break;
#if 0	/* XXX: enable if we find a legit use */
	case IP:
		if (tl->t->tok != '=') {
			illegal_assignment(tl, "IP numbers");
			return;
		}
		vcc_NextToken(tl);
		u = vcc_vcc_IpVal(tl);
		Fb(tl, 0, "= %uU; /* %u.%u.%u.%u */\n",
		    u,
		    (u >> 24) & 0xff,
		    (u >> 16) & 0xff,
		    (u >> 8) & 0xff,
		    u & 0xff);
		break;
#endif
	case BACKEND:
		if (tl->t->tok != '=') {
			illegal_assignment(tl, "backend");
			return;
		}
		vcc_NextToken(tl);
		vcc_ExpectCid(tl);
		ERRCHK(tl);
		vcc_AddRef(tl, tl->t, R_BACKEND);
		Fb(tl, 0, "VGC_backend_%.*s", PF(tl->t));
		vcc_NextToken(tl);
		Fb(tl, 0, ");\n");
		break;
	case HASH:
		ExpectErr(tl, T_INCR);
		vcc_NextToken(tl);
		if (!vcc_StringVal(tl)) {
			ERRCHK(tl);
			vcc_ExpectedStringval(tl);
			return;
		}
		Fb(tl, 0, ");\n");
		/*
		 * We count the number of operations on the req.hash
		 * variable, so that varnishd can preallocate the worst case
		 * number of slots for composing the hash string.
		 */
		tl->nhashcount++;
		break;
	case STRING:
		if (tl->t->tok != '=') {
			illegal_assignment(tl, "strings");
			return;
		}
		vcc_NextToken(tl);
		if (!vcc_StringVal(tl)) {
			ERRCHK(tl);
			vcc_ExpectedStringval(tl);
			return;
		}
		do 
			Fb(tl, 0, ", ");
		while (vcc_StringVal(tl));
		if (tl->t->tok != ';') {
			ERRCHK(tl);
			vsb_printf(tl->sb,
			    "Expected variable, string or semicolon\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		Fb(tl, 0, "vrt_magic_string_end);\n");
		break;
	case BOOL:
		if (tl->t->tok != '=') {
			illegal_assignment(tl, "boolean");
			return;
		}
		vcc_NextToken(tl);
		ExpectErr(tl, ID);
		if (vcc_IdIs(tl->t, "true")) {
			Fb(tl, 0, " 1);\n", vp->lname);
		} else if (vcc_IdIs(tl->t, "false")) {
			Fb(tl, 0, " 0);\n", vp->lname);
		} else {
			vsb_printf(tl->sb,
			    "Expected true or false\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		break;
	default:
		vsb_printf(tl->sb,
		    "Assignments not possible for type of '%s'\n", vp->name);
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}

/*--------------------------------------------------------------------*/

static void
parse_unset(struct tokenlist *tl)
{
	struct var *vp;

	vcc_NextToken(tl);
	ExpectErr(tl, VAR);
	vp = vcc_FindVar(tl, tl->t, vcc_vars);
	ERRCHK(tl);
	assert(vp != NULL);
	if (vp->fmt != STRING || vp->hdr == NULL) {
		vsb_printf(tl->sb, "Only http header lines can be unset.\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	check_writebit(tl, vp);
	ERRCHK(tl);
	Fb(tl, 1, "%s0);\n", vp->lname);
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------*/

static void
parse_purge_url(struct tokenlist *tl)
{

	vcc_NextToken(tl);
	
	Fb(tl, 1, "VRT_purge(");
	
	Expect(tl, '(');
	vcc_NextToken(tl);
	
	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return;
	}
	
	Expect(tl, ')');
	vcc_NextToken(tl);
	Fb(tl, 0, ", 0);\n");
}


/*--------------------------------------------------------------------*/

static void
parse_purge_hash(struct tokenlist *tl)
{

	vcc_NextToken(tl);
	
	Fb(tl, 1, "VRT_purge(");
	
	Expect(tl, '(');
	vcc_NextToken(tl);
	
	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return;
	}
	
	Expect(tl, ')');
	vcc_NextToken(tl);
	Fb(tl, 0, ", 1);\n");
}

static void
parse_esi(struct tokenlist *tl)
{

	vcc_NextToken(tl);
	Fb(tl, 1, "VRT_ESI(sp);\n");
}

/*--------------------------------------------------------------------*/

static void
parse_panic(struct tokenlist *tl)
{
	vcc_NextToken(tl);
	
	Fb(tl, 1, "VRT_panic(sp, ");
	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return;
	}
	do 
		Fb(tl, 0, ", ");
	while (vcc_StringVal(tl));
	Fb(tl, 0, " vrt_magic_string_end);\n");
}

/*--------------------------------------------------------------------*/

static void
parse_synthetic(struct tokenlist *tl)
{
	vcc_NextToken(tl);
	
	Fb(tl, 1, "VRT_synth_page(sp, 0, ");
	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return;
	}
	do 
		Fb(tl, 0, ", ");
	while (vcc_StringVal(tl));
	Fb(tl, 0, " vrt_magic_string_end);\n");
}

/*--------------------------------------------------------------------*/

typedef void action_f(struct tokenlist *tl);

static struct action_table {
	const char		*name;
	action_f		*func;
} action_table[] = {
	{ "restart",	parse_restart_real },
#define VCL_RET_MAC(l, u, b, i) { #l, parse_##l },
#define VCL_RET_MAC_E(l, u, b, i) VCL_RET_MAC(l, u, b, i) 
#include "vcl_returns.h"
#undef VCL_RET_MAC
#undef VCL_RET_MAC_E

	/* Keep list sorted from here */
	{ "call", 		parse_call },
	{ "esi",		parse_esi },
	{ "panic",		parse_panic },
	{ "purge_hash",		parse_purge_hash },
	{ "purge_url",		parse_purge_url },
	{ "remove", 		parse_unset }, /* backward compatibility */
	{ "set", 		parse_set },
	{ "synthetic", 		parse_synthetic },
	{ "unset", 		parse_unset },
	{ NULL,			NULL }
};

void
vcc_ParseAction(struct tokenlist *tl)
{
	struct token *at;
	struct action_table *atp;

	at = tl->t;
	if (at->tok == ID) {
		for(atp = action_table; atp->name != NULL; atp++) {
			if (vcc_IdIs(at, atp->name)) {
				atp->func(tl);
				return;
			}
		}
	}
	vsb_printf(tl->sb, "Expected action, 'if' or '}'\n");
	vcc_ErrWhere(tl, at);
}
