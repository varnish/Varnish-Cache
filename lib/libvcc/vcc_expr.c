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
 * XXX: add VRT_count()'s
 */

#include "config.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

static const char *
vcc_Type(enum var_type fmt)
{
	switch(fmt) {
#define VCC_TYPE(a)	case a: return(#a);
#include "tbl/vcc_types.h"
#undef VCC_TYPE
	default:
		assert("Unknown Type");
		return(NULL);
	}
}

/*--------------------------------------------------------------------
 * Recognize and convert units of time, return seconds.
 */

static double
vcc_TimeUnit(struct vcc *tl)
{
	double sc = 1.0;

	assert(tl->t->tok == ID);
	if (vcc_IdIs(tl->t, "ms"))
		sc = 1e-3;
	else if (vcc_IdIs(tl->t, "s"))
		sc = 1.0;
	else if (vcc_IdIs(tl->t, "m"))
		sc = 60.0;
	else if (vcc_IdIs(tl->t, "h"))
		sc = 60.0 * 60.0;
	else if (vcc_IdIs(tl->t, "d"))
		sc = 60.0 * 60.0 * 24.0;
	else if (vcc_IdIs(tl->t, "w"))
		sc = 60.0 * 60.0 * 24.0 * 7.0;
	else if (vcc_IdIs(tl->t, "y"))
		sc = 60.0 * 60.0 * 24.0 * 365.0;
	else {
		VSB_printf(tl->sb, "Unknown time unit ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb, ".  Legal are 'ms', 's', 'm', 'h', 'd', 'w' and 'y'\n");
		vcc_ErrWhere(tl, tl->t);
		return (1.0);
	}
	vcc_NextToken(tl);
	return (sc);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM } to unsigned value
 * The tokenizer made sure we only get digits.
 */

unsigned
vcc_UintVal(struct vcc *tl)
{
	unsigned d = 0;
	const char *p;

	Expect(tl, CNUM);
	for (p = tl->t->b; p < tl->t->e; p++) {
		d *= 10;
		d += *p - '0';
	}
	vcc_NextToken(tl);
	return (d);
}

/*--------------------------------------------------------------------
 * Recognize and convert { CNUM [ '.' [ CNUM ] ] } to double value
 * The tokenizer made sure we only get digits and a '.'
 */

static void
vcc_NumVal(struct vcc *tl, double *d, int *frac)
{
	double e = 0.1;
	const char *p;

	*frac = 0;
	*d = 0.0;
	Expect(tl, CNUM);
	if (tl->err) {
		*d = NAN;
		return;
	}
	for (p = tl->t->b; p < tl->t->e; p++) {
		*d *= 10;
		*d += *p - '0';
	}
	vcc_NextToken(tl);
	if (tl->t->tok != '.')
		return;
	*frac = 1;
	vcc_NextToken(tl);
	if (tl->t->tok != CNUM)
		return;
	for (p = tl->t->b; p < tl->t->e; p++) {
		*d += (*p - '0') * e;
		e *= 0.1;
	}
	vcc_NextToken(tl);
}

double
vcc_DoubleVal(struct vcc *tl)
{
	double d;
	int i;

	vcc_NumVal(tl, &d, &i);
	return (d);
}

/*--------------------------------------------------------------------*/

void
vcc_Duration(struct vcc *tl, double *d)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	ExpectErr(tl, ID);
	sc = vcc_TimeUnit(tl);
	*d = v * sc;
}

/*--------------------------------------------------------------------*/

static void
vcc_ByteVal(struct vcc *tl, double *d)
{
	double v, sc;

	v = vcc_DoubleVal(tl);
	ERRCHK(tl);
	if (tl->t->tok != ID) {
		VSB_printf(tl->sb, "Expected BYTES unit (B, KB, MB...) got ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb, "\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	if (vcc_IdIs(tl->t, "B"))
		sc = 1.;
	else if (vcc_IdIs(tl->t, "KB"))
		sc = 1024.;
	else if (vcc_IdIs(tl->t, "MB"))
		sc = 1024. * 1024.;
	else if (vcc_IdIs(tl->t, "GB"))
		sc = 1024. * 1024. * 1024.;
	else if (vcc_IdIs(tl->t, "TB"))
		sc = 1024. * 1024. * 1024. * 1024.;
	else {
		VSB_printf(tl->sb, "Unknown BYTES unit ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb,
		    ".  Legal are 'B', 'KB', 'MB', 'GB' and 'TB'\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	vcc_NextToken(tl);
	*d = v * sc;
}

/*--------------------------------------------------------------------
 * Facility for carrying expressions around and do text-processing on
 * them.
 */

struct expr {
	unsigned	magic;
#define EXPR_MAGIC	0x38c794ab
	enum var_type	fmt;
	struct vsb	*vsb;
	uint8_t		constant;
#define EXPR_VAR	(1<<0)
#define EXPR_CONST	(1<<1)
#define EXPR_STR_CONST	(1<<2)
	struct token	*t1, *t2;
};

static inline int
vcc_isconst(const struct expr *e)
{
	AN(e->constant);
	return (e->constant & EXPR_CONST);
}

static void vcc_expr0(struct vcc *tl, struct expr **e, enum var_type fmt);

static struct expr *
vcc_new_expr(void)
{
	struct expr *e;

	/* XXX: use TlAlloc() ? */
	ALLOC_OBJ(e, EXPR_MAGIC);
	AN(e);
	e->vsb = VSB_new_auto();
	e->fmt = VOID;
	e->constant = EXPR_VAR;
	return (e);
}

static struct expr *
vcc_mk_expr(enum var_type fmt, const char *str, ...)
    __v_printflike(2, 3);

static struct expr *
vcc_mk_expr(enum var_type fmt, const char *str, ...)
{
	va_list ap;
	struct expr *e;

	e = vcc_new_expr();
	e->fmt = fmt;
	va_start(ap, str);
	VSB_vprintf(e->vsb, str, ap);
	va_end(ap);
	AZ(VSB_finish(e->vsb));
	return (e);
}

static void
vcc_delete_expr(struct expr *e)
{
	if (e == NULL)
		return;
	CHECK_OBJ_NOTNULL(e, EXPR_MAGIC);
	VSB_delete(e->vsb);
	FREE_OBJ(e);
}
/*--------------------------------------------------------------------
 * We want to get the indentation right in the emitted C code so we have
 * to represent it symbolically until we are ready to render.
 *
 * Many of the operations have very schematic output syntaxes, so we
 * use the same facility to simplify the text-processing of emitting
 * a given operation on two subexpressions.
 *
 * We use '\v' as the magic escape character.
 *	\v1  insert subexpression 1
 *	\v2  insert subexpression 2
 *	\v+  increase indentation
 *	\v-  increase indentation
 *	anything else is literal
 *
 * When editing, we check if any of the subexpressions contain a newline
 * and issue it as an indented block of so.
 *
 * XXX: check line lengths in edit, should pass indent in for this
 */

static struct expr *
vcc_expr_edit(enum var_type fmt, const char *p, struct expr *e1,
    struct expr *e2)
{
	struct expr *e;
	int nl = 1;

	AN(e1);
	e = vcc_new_expr();
	while (*p != '\0') {
		if (*p != '\v') {
			if (*p != '\n' || !nl)
				VSB_putc(e->vsb, *p);
			nl = (*p == '\n');
			p++;
			continue;
		}
		assert(*p == '\v');
		switch(*++p) {
		case '+': VSB_cat(e->vsb, "\v+"); break;
		case '-': VSB_cat(e->vsb, "\v-"); break;
		case '1':
			VSB_cat(e->vsb, VSB_data(e1->vsb));
			break;
		case '2':
			AN(e2);
			VSB_cat(e->vsb, VSB_data(e2->vsb));
			break;
		default:
			WRONG("Illegal edit in VCC expression");
		}
		p++;
	}
	AZ(VSB_finish(e->vsb));
	e->t1 = e1->t1;
	e->t2 = e1->t2;
	if (e2 != NULL)
		e->t2 = e2->t2;
	vcc_delete_expr(e1);
	vcc_delete_expr(e2);
	e->fmt = fmt;
	return (e);
}

/*--------------------------------------------------------------------
 * Expand finished expression into C-source code
 */

static void
vcc_expr_fmt(struct vsb *d, int ind, const struct expr *e1)
{
	char *p;
	int i;

	for (i = 0; i < ind; i++)
		VSB_cat(d, " ");
	p = VSB_data(e1->vsb);
	while (*p != '\0') {
		if (*p == '\n') {
			VSB_putc(d, '\n');
			if (p[1] != '\0') {
				for (i = 0; i < ind; i++)
					VSB_cat(d, " ");
			}
			p++;
			continue;
		}
		if (*p != '\v') {
			VSB_putc(d, *p);
			p++;
			continue;
		}
		p++;
		switch(*p) {
		case '+': ind += 2; break;
		case '-': ind -= 2; break;
		default:
			WRONG("Illegal format in VCC expression");
		}
		p++;
	}
}

/*--------------------------------------------------------------------
 */

static enum var_type
vcc_arg_type(const char **p)
{

#define VCC_TYPE(a) if (!strcmp(#a, *p)) { *p += strlen(#a) + 1; return (a);}
#include "tbl/vcc_types.h"
#undef VCC_TYPE
	return (VOID);
}

/*--------------------------------------------------------------------
 */

static void
vcc_expr_tostring(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	const char *p;
	uint8_t	constant = EXPR_VAR;

	CHECK_OBJ_NOTNULL(*e, EXPR_MAGIC);
	AN(fmt == STRING || fmt == STRING_LIST);

	p = NULL;
	switch((*e)->fmt) {
	case BACKEND:	p = "VRT_BACKEND_string(\v1)"; break;
	case BOOL:	p = "VRT_BOOL_string(\v1)"; break;
	case DURATION:	p = "VRT_REAL_string(ctx, \v1)"; break;
			 /* XXX: should DURATION insist on "s" suffix ? */
	case INT:
		if (vcc_isconst(*e)) {
			p = "\"\v1\"";
			constant = EXPR_CONST;
		} else {
			p = "VRT_INT_string(ctx, \v1)";
		}
		break;
	case IP:	p = "VRT_IP_string(ctx, \v1)"; break;
	case BYTES:	p = "VRT_REAL_string(ctx, \v1)"; break; /* XXX */
	case REAL:	p = "VRT_REAL_string(ctx, \v1)"; break;
	case TIME:	p = "VRT_TIME_string(ctx, \v1)"; break;
	case HEADER:	p = "VRT_GetHdr(ctx, \v1)"; break;
	case ENUM:
	case STRING:
	case STRING_LIST:
			break;
	case BLOB:
			VSB_printf(tl->sb,
			    "Wrong use of BLOB value.\n"
			    "BLOBs can only be used as arguments to VMOD"
			    " functions.\n");
			vcc_ErrWhere2(tl, (*e)->t1, tl->t);
			return;
	default:
			INCOMPL();
			break;
	}
	if (p != NULL) {
		*e = vcc_expr_edit(fmt, p, *e, NULL);
		(*e)->constant = constant;
	}
}

/*--------------------------------------------------------------------
 */

static void
vcc_Eval_Regsub(struct vcc *tl, struct expr **e, const struct symbol *sym)
{
	struct expr *e2;
	int all = sym->eval_priv == NULL ? 0 : 1;
	const char *p;
	char buf[128];

	vcc_delete_expr(*e);
	SkipToken(tl, ID);
	SkipToken(tl, '(');

	vcc_expr0(tl, &e2, STRING);
	if (e2 == NULL)
		return;
	if (e2->fmt != STRING) {
		vcc_expr_tostring(tl, &e2, STRING);
		ERRCHK(tl);
	}

	SkipToken(tl, ',');
	ExpectErr(tl, CSTR);
	p = vcc_regexp(tl);
	vcc_NextToken(tl);

	bprintf(buf, "VRT_regsub(ctx, %d,\v+\n\v1,\n%s", all, p);
	*e = vcc_expr_edit(STRING, buf, e2, *e);

	SkipToken(tl, ',');
	vcc_expr0(tl, &e2, STRING);
	if (e2 == NULL)
		return;
	if (e2->fmt != STRING) {
		vcc_expr_tostring(tl, &e2, STRING);
		ERRCHK(tl);
	}
	*e = vcc_expr_edit(STRING, "\v1,\n\v2)\v-", *e, e2);
	SkipToken(tl, ')');
}

/*--------------------------------------------------------------------
 */

static void
vcc_Eval_BoolConst(struct vcc *tl, struct expr **e, const struct symbol *sym)
{

	vcc_NextToken(tl);
	*e = vcc_mk_expr(BOOL, "(0==%d)", sym->eval_priv == NULL ? 1 : 0);
	(*e)->constant = EXPR_CONST;
}

/*--------------------------------------------------------------------
 */

void
vcc_Eval_Backend(struct vcc *tl, struct expr **e, const struct symbol *sym)
{

	assert(sym->kind == SYM_BACKEND);

	vcc_ExpectCid(tl);
	vcc_AddRef(tl, tl->t, SYM_BACKEND);
	*e = vcc_mk_expr(BACKEND, "VGCDIR(_%.*s)", PF(tl->t));
	(*e)->constant = EXPR_VAR;	/* XXX ? */
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 */
void
vcc_Eval_Var(struct vcc *tl, struct expr **e, const struct symbol *sym)
{
	const struct var *vp;

	assert(sym->kind == SYM_VAR);
	vcc_AddUses(tl, tl->t, sym->r_methods, "Not available");
	vp = vcc_FindVar(tl, tl->t, 0, "cannot be read");
	ERRCHK(tl);
	assert(vp != NULL);
	*e = vcc_mk_expr(vp->fmt, "%s", vp->rname);
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------
 */

static struct expr *
vcc_priv_arg(struct vcc *tl, const char *p, const char *name)
{
	const char *r;
	struct expr *e2;
	char buf[32];
	struct inifin *ifp;

	if (!strcmp(p, "PRIV_VCL")) {
		r = strchr(name, '.');
		AN(r);
		e2 = vcc_mk_expr(VOID, "&vmod_priv_%.*s",
		    (int) (r - name), name);
	} else if (!strcmp(p, "PRIV_CALL")) {
		bprintf(buf, "vmod_priv_%u", tl->unique++);
		ifp = New_IniFin(tl);
		Fh(tl, 0, "static struct vmod_priv %s;\n", buf);
		VSB_printf(ifp->fin, "\tVRT_priv_fini(&%s);", buf);
		e2 = vcc_mk_expr(VOID, "&%s", buf);
	} else if (!strcmp(p, "PRIV_TASK")) {
		r = strchr(name, '.');
		AN(r);
		e2 = vcc_mk_expr(VOID,
		    "VRT_priv_task(ctx, &VGC_vmod_%.*s)",
		    (int) (r - name), name);
	} else if (!strcmp(p, "PRIV_TOP")) {
		r = strchr(name, '.');
		AN(r);
		e2 = vcc_mk_expr(VOID,
		    "VRT_priv_top(ctx, &VGC_vmod_%.*s)",
		    (int) (r - name), name);
	} else {
		WRONG("Wrong PRIV_ type");
	}
	return (e2);
}

struct func_arg {
	enum var_type		type;
	const char		*enum_bits;
	const char		*name;
	const char		*val;
	struct expr		*result;
	VTAILQ_ENTRY(func_arg)	list;
};

static void
vcc_do_arg(struct vcc *tl, struct func_arg *fa)
{
	const char *p, *r;
	struct expr *e2;

	if (fa->type == ENUM) {
		ExpectErr(tl, ID);
		ERRCHK(tl);
		r = p = fa->enum_bits;
		do {
			if (vcc_IdIs(tl->t, p))
				break;
			p += strlen(p) + 1;
		} while (*p != '\0');
		if (*p == '\0') {
			VSB_printf(tl->sb, "Wrong enum value.");
			VSB_printf(tl->sb, "  Expected one of:\n");
			do {
				VSB_printf(tl->sb, "\t%s\n", r);
				r += strlen(r) + 1;
			} while (*r != '\0');
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		fa->result = vcc_mk_expr(VOID, "\"%.*s\"", PF(tl->t));
		SkipToken(tl, ID);
	} else {
		vcc_expr0(tl, &e2, fa->type);
		ERRCHK(tl);
		if (e2->fmt != fa->type) {
			VSB_printf(tl->sb, "Wrong argument type.");
			VSB_printf(tl->sb, "  Expected %s.",
				vcc_Type(fa->type));
			VSB_printf(tl->sb, "  Got %s.\n",
				vcc_Type(e2->fmt));
			vcc_ErrWhere2(tl, e2->t1, tl->t);
			return;
		}
		assert(e2->fmt == fa->type);
		if (e2->fmt == STRING_LIST) {
			e2 = vcc_expr_edit(STRING_LIST,
			    "\v+\n\v1,\nvrt_magic_string_end\v-",
			    e2, NULL);
		}
		fa->result = e2;
	}
}

static void
vcc_func(struct vcc *tl, struct expr **e, const char *cfunc,
    const char *extra, const char *name, const char *args)
{
	const char *p;
	struct expr *e1;
	struct func_arg *fa, *fa2;
	enum var_type rfmt;
	VTAILQ_HEAD(,func_arg) head;
	struct token *t1;

	AN(cfunc);
	AN(args);
	AN(name);
	SkipToken(tl, '(');
	p = args;
	if (extra == NULL)
		extra = "";
	rfmt = vcc_arg_type(&p);
	VTAILQ_INIT(&head);
	while (*p != '\0') {
		fa = calloc(sizeof *fa, 1);
		AN(fa);
		VTAILQ_INSERT_TAIL(&head, fa, list);
		fa->type = vcc_arg_type(&p);
		if (fa->type == VOID && !memcmp(p, "PRIV_", 5)) {
			fa->result = vcc_priv_arg(tl, p, name);
			fa->name = "";
			p += strlen(p) + 1;
			continue;
		}
		if (fa->type == ENUM) {
			fa->enum_bits = p;
			while (*p != '\0')
				p += strlen(p) + 1;
			p += strlen(p) + 1;
		}
		if (*p == '\1') {
			fa->name = p + 1;
			p = strchr(p, '\0') + 1;
			if (*p == '\2') {
				fa->val = p + 1;
				p = strchr(p, '\0') + 1;
			}
		}
	}

	VTAILQ_FOREACH(fa, &head, list) {
		if (tl->t->tok == ')')
			break;
		if (fa->result != NULL)
			continue;
		if (tl->t->tok == ID) {
			t1 = VTAILQ_NEXT(tl->t, list);
			if (t1->tok == '=')
				break;
		}
		vcc_do_arg(tl, fa);
		ERRCHK(tl);
		if (tl->t->tok == ')')
			break;
		SkipToken(tl, ',');
	}
	while (tl->t->tok == ID) {
		VTAILQ_FOREACH(fa, &head, list) {
			if (fa->name == NULL)
				continue;
			if (vcc_IdIs(tl->t, fa->name))
				break;
		}
		if (fa == NULL) {
			VSB_printf(tl->sb, "Unknown argument '%.*s'\n",
			    PF(tl->t));
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		if (fa->result != NULL) {
			VSB_printf(tl->sb, "Argument '%s' already used\n",
			    fa->name);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		SkipToken(tl, '=');
		vcc_do_arg(tl, fa);
		ERRCHK(tl);
		if (tl->t->tok == ')')
			break;
		SkipToken(tl, ',');
	}

	e1 = vcc_mk_expr(rfmt, "%s(ctx%s\v+", cfunc, extra);
	VTAILQ_FOREACH_SAFE(fa, &head, list, fa2) {
		if (fa->result == NULL && fa->val != NULL)
			fa->result = vcc_mk_expr(fa->type, "%s", fa->val);
		if (fa->result != NULL)
			e1 = vcc_expr_edit(e1->fmt, "\v1,\n\v2",
			    e1, fa->result);
		else {
			VSB_printf(tl->sb, "Argument '%s' missing\n",
			    fa->name);
			vcc_ErrWhere(tl, tl->t);
		}
		free(fa);
	}
	e1 = vcc_expr_edit(e1->fmt, "\v1\n)\v-", e1, NULL);
	*e = e1;

	SkipToken(tl, ')');

}

/*--------------------------------------------------------------------
 */

void
vcc_Eval_Func(struct vcc *tl, const char *cfunc,
    const char *extra, const char *name, const char *args)
{
	struct expr *e = NULL;
	struct token *t1;

	t1 = tl->t;
	vcc_func(tl, &e, cfunc, extra, name, args);
	if (!tl->err) {
		vcc_expr_fmt(tl->fb, tl->indent, e);
		VSB_cat(tl->fb, ";\n");
	} else if (t1 != tl->t) {
		vcc_ErrWhere2(tl, t1, tl->t);
	}
	vcc_delete_expr(e);
}

/*--------------------------------------------------------------------
 */

void
vcc_Eval_SymFunc(struct vcc *tl, struct expr **e, const struct symbol *sym)
{

	assert(sym->kind == SYM_FUNC || sym->kind == SYM_PROC);
	AN(sym->cfunc);
	AN(sym->name);
	AN(sym->args);
	SkipToken(tl, ID);
	vcc_func(tl, e, sym->cfunc, sym->extra, sym->name, sym->args);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr4:
 *	'(' Expr0 ')'
 *	symbol
 *	CNUM
 *	CSTR
 */

static void
vcc_expr4(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e1, *e2;
	const char *ip;
	const struct symbol *sym;
	double d;
	int i;

	*e = NULL;
	if (tl->t->tok == '(') {
		SkipToken(tl, '(');
		vcc_expr0(tl, &e2, fmt);
		ERRCHK(tl);
		SkipToken(tl, ')');
		*e = vcc_expr_edit(e2->fmt, "(\v1)", e2, NULL);
		return;
	}
	switch(tl->t->tok) {
	case ID:
		/*
		 * XXX: what if var and func/proc had same name ?
		 * XXX: look for SYM_VAR first for consistency ?
		 */
		sym = NULL;
		if (fmt == BACKEND)
			sym = VCC_FindSymbol(tl, tl->t, SYM_BACKEND);
		if (sym == NULL)
			sym = VCC_FindSymbol(tl, tl->t, SYM_VAR);
		if (sym == NULL)
			sym = VCC_FindSymbol(tl, tl->t, SYM_FUNC);
		if (sym == NULL)
			sym = VCC_FindSymbol(tl, tl->t, SYM_NONE);
		if (sym == NULL || sym->eval == NULL) {
			VSB_printf(tl->sb, "Symbol not found: ");
			vcc_ErrToken(tl, tl->t);
			VSB_printf(tl->sb, " (expected type %s):\n",
			    vcc_Type(fmt));
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		AN(sym);
		switch(sym->kind) {
		case SYM_VAR:
		case SYM_FUNC:
		case SYM_BACKEND:
			AN(sym->eval);
			AZ(*e);
			sym->eval(tl, e, sym);
			/* Unless asked for a HEADER, fold to string here */
			if (*e && fmt != HEADER && (*e)->fmt == HEADER) {
				vcc_expr_tostring(tl, e, STRING);
				ERRCHK(tl);
			}
			return;
		default:
			break;
		}
		VSB_printf(tl->sb,
		    "Symbol type (%s) can not be used in expression.\n",
		    VCC_SymKind(tl, sym));
		vcc_ErrWhere(tl, tl->t);
		return;
	case CSTR:
		assert(fmt != VOID);
		if (fmt == IP) {
			Resolve_Sockaddr(tl, tl->t->dec, "80",
			    &ip, NULL, &ip, NULL, NULL, 1,
			    tl->t, "IP constant");
			ERRCHK(tl);
			e1 = vcc_mk_expr(IP, "%s", ip);
			ERRCHK(tl);
		} else {
			e1 = vcc_new_expr();
			EncToken(e1->vsb, tl->t);
			e1->fmt = STRING;
			AZ(VSB_finish(e1->vsb));
		}
		e1->t1 = tl->t;
		e1->constant = EXPR_CONST;
		vcc_NextToken(tl);
		*e = e1;
		break;
	case CNUM:
		/*
		 * XXX: %g may not have enough decimals by default
		 * XXX: but %a is ugly, isn't it ?
		 */
		assert(fmt != VOID);
		if (fmt == DURATION) {
			vcc_Duration(tl, &d);
			ERRCHK(tl);
			e1 = vcc_mk_expr(DURATION, "%g", d);
		} else if (fmt == BYTES) {
			vcc_ByteVal(tl, &d);
			ERRCHK(tl);
			e1 = vcc_mk_expr(BYTES, "%.1f", d);
			ERRCHK(tl);
		} else if (fmt == REAL) {
			e1 = vcc_mk_expr(REAL, "%f", vcc_DoubleVal(tl));
			ERRCHK(tl);
		} else if (fmt == INT) {
			e1 = vcc_mk_expr(INT, "%.*s", PF(tl->t));
			vcc_NextToken(tl);
		} else {
			vcc_NumVal(tl, &d, &i);
			if (i)
				e1 = vcc_mk_expr(REAL, "%f", d);
			else
				e1 = vcc_mk_expr(INT, "%ld", (long)d);
		}
		e1->constant = EXPR_CONST;
		*e = e1;
		break;
	default:
		VSB_printf(tl->sb, "Unknown token ");
		vcc_ErrToken(tl, tl->t);
		VSB_printf(tl->sb, " when looking for %s\n\n", vcc_Type(fmt));
		vcc_ErrWhere(tl, tl->t);
		break;
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr3:
 *      Expr4 { {'*'|'/'} Expr4 } *
 */

static void
vcc_expr_mul(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e2;
	enum var_type f2, f3;
	struct token *tk;

	*e = NULL;
	vcc_expr4(tl, e, fmt);
	ERRCHK(tl);
	f3 = f2 = (*e)->fmt;

	switch(f2) {
	case INT:	f2 = INT; break;
	case DURATION:	f2 = REAL; break;
	case BYTES:	f2 = REAL; break;
	default:
		if (tl->t->tok != '*' && tl->t->tok != '/')
			return;
		VSB_printf(tl->sb, "Operator %.*s not possible on type %s.\n",
		    PF(tl->t), vcc_Type(f2));
		vcc_ErrWhere(tl, tl->t);
		return;
	}

	while (tl->t->tok == '*' || tl->t->tok == '/') {
		tk = tl->t;
		vcc_NextToken(tl);
		vcc_expr4(tl, &e2, f2);
		ERRCHK(tl);
		assert(e2->fmt == f2);
		if (tk->tok == '*')
			*e = vcc_expr_edit(f3, "(\v1*\v2)", *e, e2);
		else
			*e = vcc_expr_edit(f3, "(\v1/\v2)", *e, e2);
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprAdd:
 *      ExprMul { {'+'|'-'} ExprMul } *
 *
 * For reasons of memory allocation/copying and general performance,
 * STRINGs in VCL are quite special.   Addition/concatenation is split
 * into it's own subfunction to encapsulate this.
 */

static void
vcc_expr_string_add(struct vcc *tl, struct expr **e, struct expr *e2)
{
	enum var_type f2;

	AN(e);
	AN(*e);
	AN(e2);
	f2 = (*e)->fmt;
	assert (f2 == STRING || f2 == STRING_LIST);

	while (e2 != NULL || tl->t->tok == '+') {
		if (e2 == NULL) {
			vcc_NextToken(tl);
			vcc_expr_mul(tl, &e2, STRING);
		}
		ERRCHK(tl);
		if (e2->fmt != STRING && e2->fmt != STRING_LIST) {
			vcc_expr_tostring(tl, &e2, f2);
			ERRCHK(tl);
		}
		ERRCHK(tl);
		assert(e2->fmt == STRING || e2->fmt == STRING_LIST);

		if (vcc_isconst(*e) && vcc_isconst(e2)) {
			assert((*e)->fmt == STRING);
			assert(e2->fmt == STRING);
			*e = vcc_expr_edit(STRING, "\v1\n\v2", *e, e2);
			(*e)->constant = EXPR_CONST;
		} else if (((*e)->constant & EXPR_STR_CONST) &&
		    vcc_isconst(e2)) {
			assert((*e)->fmt == STRING_LIST);
			assert(e2->fmt == STRING);
			*e = vcc_expr_edit(STRING_LIST, "\v1\n\v2", *e, e2);
			(*e)->constant = EXPR_VAR | EXPR_STR_CONST;
		} else if (e2->fmt == STRING && vcc_isconst(e2)) {
			*e = vcc_expr_edit(STRING_LIST, "\v1,\n\v2", *e, e2);
			(*e)->constant = EXPR_VAR | EXPR_STR_CONST;
		} else {
			*e = vcc_expr_edit(STRING_LIST, "\v1,\n\v2", *e, e2);
			(*e)->constant = EXPR_VAR;
		}
		e2 = NULL;
	}
}

static void
vcc_expr_add(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr  *e2;
	enum var_type f2;
	struct token *tk;

	*e = NULL;
	vcc_expr_mul(tl, e, fmt);
	ERRCHK(tl);
	f2 = (*e)->fmt;

	while (tl->t->tok == '+' || tl->t->tok == '-') {
		tk = tl->t;
		vcc_NextToken(tl);
		if (f2 == TIME)
			vcc_expr_mul(tl, &e2, DURATION);
		else
			vcc_expr_mul(tl, &e2, f2);
		ERRCHK(tl);
		if (tk->tok == '-' && (*e)->fmt == TIME && e2->fmt == TIME) {
			/* OK */
		} else if ((*e)->fmt == TIME && e2->fmt == DURATION) {
			f2 = TIME;
			/* OK */
		} else if ((*e)->fmt == BYTES && e2->fmt == BYTES) {
			/* OK */
		} else if ((*e)->fmt == INT && e2->fmt == INT) {
			/* OK */
		} else if ((*e)->fmt == DURATION && e2->fmt == DURATION) {
			/* OK */
		} else if (tk->tok == '+' &&
		    (*e)->fmt == STRING && e2->fmt == STRING) {
			vcc_expr_string_add(tl, e, e2);
			return;
		} else if (tk->tok == '+' &&
		    (fmt == STRING || fmt == STRING_LIST)) {
			/* Time to fold and add as string */
			vcc_expr_tostring(tl, e, STRING);
			vcc_expr_string_add(tl, e, e2);
			return;
		} else {
			VSB_printf(tl->sb, "%s %.*s %s not possible.\n",
			    vcc_Type((*e)->fmt), PF(tk), vcc_Type(e2->fmt));
			vcc_ErrWhere2(tl, tk, tl->t);
			return;
		}
		if (tk->tok == '+')
			*e = vcc_expr_edit(f2, "(\v1+\v2)", *e, e2);
		else if (f2 == TIME && e2->fmt == TIME)
			*e = vcc_expr_edit(DURATION, "(\v1-\v2)", *e, e2);
		else
			*e = vcc_expr_edit(f2, "(\v1-\v2)", *e, e2);
	}
}

/*--------------------------------------------------------------------
 * Fold the STRING types correctly
 */

static void
vcc_expr_strfold(struct vcc *tl, struct expr **e, enum var_type fmt)
{

	vcc_expr_add(tl, e, fmt);
	ERRCHK(tl);

	if (fmt != STRING_LIST && (*e)->fmt == STRING_LIST)
		*e = vcc_expr_edit(STRING,
		    "\v+VRT_CollectString(ctx,\n\v1,\nvrt_magic_string_end)\v-",
		    *e, NULL);
	if (fmt == STRING_LIST && (*e)->fmt == STRING)
		(*e)->fmt = STRING_LIST;
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprCmp:
 *	ExprAdd
 *      ExprAdd Relation ExprAdd
 *	ExprAdd(STRING) '~' CString
 *	ExprAdd(STRING) '!~' CString
 *	ExprAdd(IP) '~' IP
 *	ExprAdd(IP) '!~' IP
 */

#define NUM_REL(typ)					\
	{typ,		T_EQ,	"(\v1 == \v2)" },	\
	{typ,		T_NEQ,	"(\v1 != \v2)" },	\
	{typ,		T_LEQ,	"(\v1 <= \v2)" },	\
	{typ,		T_GEQ,	"(\v1 >= \v2)" },	\
	{typ,		'<',	"(\v1 < \v2)" },	\
	{typ,		'>',	"(\v1 > \v2)" }

static const struct cmps {
	enum var_type		fmt;
	unsigned		token;
	const char		*emit;
} vcc_cmps[] = {
	NUM_REL(INT),
	NUM_REL(DURATION),
	NUM_REL(BYTES),
	NUM_REL(REAL),
	NUM_REL(TIME),

	{STRING,	T_EQ,	"!VRT_strcmp(\v1, \v2)" },
	{STRING,	T_NEQ,	"VRT_strcmp(\v1, \v2)" },

	{VOID, 0, NULL}
};

#undef NUM_REL

static void
vcc_expr_cmp(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e2;
	const struct cmps *cp;
	char buf[256];
	const char *re;
	const char *not;
	struct token *tk;

	*e = NULL;

	vcc_expr_strfold(tl, e, fmt);
	ERRCHK(tl);

	if ((*e)->fmt == BOOL)
		return;

	tk = tl->t;
	for (cp = vcc_cmps; cp->fmt != VOID; cp++)
		if ((*e)->fmt == cp->fmt && tl->t->tok == cp->token)
			break;
	if (cp->fmt != VOID) {
		vcc_NextToken(tl);
		vcc_expr_strfold(tl, &e2, (*e)->fmt);
		ERRCHK(tl);
		if (e2->fmt != (*e)->fmt) { /* XXX */
			VSB_printf(tl->sb, "Comparison of different types: ");
			VSB_printf(tl->sb, "%s ", vcc_Type((*e)->fmt));
			vcc_ErrToken(tl, tk);
			VSB_printf(tl->sb, " %s\n", vcc_Type(e2->fmt));
			vcc_ErrWhere(tl, tk);
			return;
		}
		*e = vcc_expr_edit(BOOL, cp->emit, *e, e2);
		return;
	}
	if ((*e)->fmt == STRING &&
	    (tl->t->tok == '~' || tl->t->tok == T_NOMATCH)) {
		not = tl->t->tok == '~' ? "" : "!";
		vcc_NextToken(tl);
		ExpectErr(tl, CSTR);
		re = vcc_regexp(tl);
		ERRCHK(tl);
		vcc_NextToken(tl);
		bprintf(buf, "%sVRT_re_match(ctx, \v1, %s)", not, re);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	if ((*e)->fmt == IP &&
	    (tl->t->tok == '~' || tl->t->tok == T_NOMATCH)) {
		not = tl->t->tok == '~' ? "" : "!";
		vcc_NextToken(tl);
		ExpectErr(tl, ID);
		vcc_AddRef(tl, tl->t, SYM_ACL);
		bprintf(buf, "%smatch_acl_named_%.*s(ctx, \v1)",
		    not, PF(tl->t));
		vcc_NextToken(tl);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	if ((*e)->fmt == IP && (tl->t->tok == T_EQ || tl->t->tok == T_NEQ)) {
		vcc_Acl_Hack(tl, buf);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	if ((*e)->fmt == BACKEND &&
	    (tl->t->tok == T_EQ || tl->t->tok == T_NEQ)) {
		vcc_NextToken(tl);
		ExpectErr(tl, ID);
		vcc_AddRef(tl, tl->t, SYM_BACKEND);
		bprintf(buf, "(\v1 %.*s VGCDIR(_%.*s))", PF(tk), PF(tl->t));
		vcc_NextToken(tl);
		*e = vcc_expr_edit(BOOL, buf, *e, NULL);
		return;
	}
	switch (tl->t->tok) {
	case T_EQ:
	case T_NEQ:
	case '<':
	case T_LEQ:
	case '>':
	case T_GEQ:
	case '~':
	case T_NOMATCH:
		VSB_printf(tl->sb, "Operator %.*s not possible on %s\n",
		    PF(tl->t), vcc_Type((*e)->fmt));
		vcc_ErrWhere(tl, tl->t);
		return;
	default:
		break;
	}
	if (fmt == BOOL && (*e)->fmt == STRING) {
		*e = vcc_expr_edit(BOOL, "(\v1 != 0)", *e, NULL);
		return;
	}
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprNot:
 *      '!' ExprCmp
 */

static void
vcc_expr_not(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e2;
	struct token *tk;

	*e = NULL;
	if (fmt != BOOL || tl->t->tok != '!') {
		vcc_expr_cmp(tl, e, fmt);
		return;
	}

	vcc_NextToken(tl);
	tk = tl->t;
	vcc_expr_cmp(tl, &e2, fmt);
	ERRCHK(tl);
	if (e2->fmt == BOOL) {
		*e = vcc_expr_edit(BOOL, "!(\v1)", e2, NULL);
		return;
	}
	VSB_printf(tl->sb, "'!' must be followed by BOOL, found ");
	VSB_printf(tl->sb, "%s.\n", vcc_Type(e2->fmt));
	vcc_ErrWhere2(tl, tk, tl->t);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    ExprCand:
 *      ExprNot { '&&' ExprNot } *
 */

static void
vcc_expr_cand(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e2;
	struct token *tk;

	*e = NULL;
	vcc_expr_not(tl, e, fmt);
	ERRCHK(tl);
	if ((*e)->fmt != BOOL || tl->t->tok != T_CAND)
		return;
	*e = vcc_expr_edit(BOOL, "(\v+\n\v1", *e, NULL);
	while (tl->t->tok == T_CAND) {
		vcc_NextToken(tl);
		tk = tl->t;
		vcc_expr_not(tl, &e2, fmt);
		ERRCHK(tl);
		if (e2->fmt != BOOL) {
			VSB_printf(tl->sb,
			    "'&&' must be followed by BOOL, found ");
			VSB_printf(tl->sb, "%s.\n", vcc_Type(e2->fmt));
			vcc_ErrWhere2(tl, tk, tl->t);
			return;
		}
		*e = vcc_expr_edit(BOOL, "\v1\v-\n&&\v+\n\v2", *e, e2);
	}
	*e = vcc_expr_edit(BOOL, "\v1\v-\n)", *e, NULL);
}

/*--------------------------------------------------------------------
 * SYNTAX:
 *    Expr0:
 *      ExprCand { '||' ExprCand } *
 */

static void
vcc_expr0(struct vcc *tl, struct expr **e, enum var_type fmt)
{
	struct expr *e2;
	struct token *tk;

	*e = NULL;
	vcc_expr_cand(tl, e, fmt);
	ERRCHK(tl);
	if ((*e)->fmt != BOOL || tl->t->tok != T_COR)
		return;
	*e = vcc_expr_edit(BOOL, "(\v+\n\v1", *e, NULL);
	while (tl->t->tok == T_COR) {
		vcc_NextToken(tl);
		tk = tl->t;
		vcc_expr_cand(tl, &e2, fmt);
		ERRCHK(tl);
		if (e2->fmt != BOOL) {
			VSB_printf(tl->sb,
			    "'||' must be followed by BOOL, found ");
			VSB_printf(tl->sb, "%s.\n", vcc_Type(e2->fmt));
			vcc_ErrWhere2(tl, tk, tl->t);
			return;
		}
		*e = vcc_expr_edit(BOOL, "\v1\v-\n||\v+\n\v2", *e, e2);
	}
	*e = vcc_expr_edit(BOOL, "\v1\v-\n)", *e, NULL);
}

/*--------------------------------------------------------------------
 * This function parses and emits the C-code to evaluate an expression
 *
 * We know up front what kind of type we want the expression to be,
 * and this function is the backstop if that doesn't succeed.
 */

void
vcc_Expr(struct vcc *tl, enum var_type fmt)
{
	struct expr *e;
	struct token *t1;

	assert(fmt != VOID);

	t1 = tl->t;
	vcc_expr0(tl, &e, fmt);
	ERRCHK(tl);
	e->t1 = t1;
	if (fmt == STRING || fmt == STRING_LIST) {
		vcc_expr_tostring(tl, &e, fmt);
		ERRCHK(tl);
	}
	if (!tl->err && fmt != e->fmt)  {
		VSB_printf(tl->sb, "Expression has type %s, expected %s\n",
		    vcc_Type(e->fmt), vcc_Type(fmt));
		tl->err = 1;
	}
	if (!tl->err) {
		if (e->fmt == STRING_LIST) {
			e = vcc_expr_edit(STRING_LIST,
			    "\v+\n\v1,\nvrt_magic_string_end\v-", e, NULL);
		}
		vcc_expr_fmt(tl->fb, tl->indent, e);
		VSB_putc(tl->fb, '\n');
	} else {
		if (t1 != tl->t)
			vcc_ErrWhere2(tl, t1, tl->t);
	}
	vcc_delete_expr(e);
}

/*--------------------------------------------------------------------
 */

void
vcc_Expr_Call(struct vcc *tl, const struct symbol *sym)
{

	struct expr *e;
	struct token *t1;

	t1 = tl->t;
	e = NULL;
	vcc_Eval_SymFunc(tl, &e, sym);
	if (!tl->err) {
		vcc_expr_fmt(tl->fb, tl->indent, e);
		VSB_cat(tl->fb, ";\n");
	} else if (t1 != tl->t) {
		vcc_ErrWhere2(tl, t1, tl->t);
	}
	vcc_delete_expr(e);
}

/*--------------------------------------------------------------------
 */

void
vcc_Expr_Init(struct vcc *tl)
{
	struct symbol *sym;

	sym = VCC_AddSymbolStr(tl, "regsub", SYM_FUNC);
	AN(sym);
	sym->eval = vcc_Eval_Regsub;
	sym->eval_priv = NULL;

	sym = VCC_AddSymbolStr(tl, "regsuball", SYM_FUNC);
	AN(sym);
	sym->eval = vcc_Eval_Regsub;
	sym->eval_priv = sym;

	sym = VCC_AddSymbolStr(tl, "true", SYM_FUNC);
	AN(sym);
	sym->eval = vcc_Eval_BoolConst;
	sym->eval_priv = sym;

	sym = VCC_AddSymbolStr(tl, "false", SYM_FUNC);
	AN(sym);
	sym->eval = vcc_Eval_BoolConst;
	sym->eval_priv = NULL;
}
