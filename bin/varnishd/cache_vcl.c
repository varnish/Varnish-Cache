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
 *
 * Interface *to* compiled VCL code:  Loading, unloading, calling into etc.
 *
 * The interface *from* the compiled VCL code is in cache_vrt.c.
 */

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "cli.h"
#include "cli_priv.h"
#include "shmlog.h"
#include "vcl.h"
#include "cache.h"

struct vcls {
	unsigned		magic;
#define VCLS_MAGIC		0x214188f2
	VTAILQ_ENTRY(vcls)	list;
	char			*name;
	void			*dlh;
	struct VCL_conf		conf[1];
};

/*
 * XXX: Presently all modifications to this list happen from the
 * CLI event-engine, so no locking is necessary
 */
static VTAILQ_HEAD(, vcls)	vcl_head =
    VTAILQ_HEAD_INITIALIZER(vcl_head);


static MTX			vcl_mtx;
static struct vcls		*vcl_active; /* protected by vcl_mtx */

/*--------------------------------------------------------------------*/

void
VCL_Refresh(struct VCL_conf **vcc)
{
	if (*vcc == vcl_active->conf)
		return;
	if (*vcc != NULL)
		VCL_Rel(vcc);	/* XXX: optimize locking */
	VCL_Get(vcc);
}

void
VCL_Get(struct VCL_conf **vcc)
{

	LOCK(&vcl_mtx);
	AN(vcl_active);
	*vcc = vcl_active->conf;
	AN(*vcc);
	AZ((*vcc)->discard);
	(*vcc)->busy++;
	UNLOCK(&vcl_mtx);
}

void
VCL_Rel(struct VCL_conf **vcc)
{
	struct VCL_conf *vc;

	vc = *vcc;
	*vcc = NULL;

	LOCK(&vcl_mtx);
	assert(vc->busy > 0);
	vc->busy--;
	/*
	 * We do not garbage collect discarded VCL's here, that happens
	 * in VCL_Poll() which is called from the CLI thread.
	 */
	UNLOCK(&vcl_mtx);
}

/*--------------------------------------------------------------------*/

static struct vcls *
vcl_find(const char *name)
{
	struct vcls *vcl;

	ASSERT_CLI();
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		if (vcl->conf->discard)
			continue;
		if (!strcmp(vcl->name, name))
			return (vcl);
	}
	return (NULL);
}

static int
VCL_Load(const char *fn, const char *name, struct cli *cli)
{
	struct vcls *vcl;
	struct VCL_conf const *cnf;

	ASSERT_CLI();
	vcl = vcl_find(name);
	if (vcl != NULL) {
		cli_out(cli, "Config '%s' already loaded", name);
		return (1);
	}

	ALLOC_OBJ(vcl, VCLS_MAGIC);
	XXXAN(vcl);

	vcl->dlh = dlopen(fn, RTLD_NOW | RTLD_LOCAL);

	if (vcl->dlh == NULL) {
		cli_out(cli, "dlopen(%s): %s\n", fn, dlerror());
		FREE_OBJ(vcl);
		return (1);
	}
	cnf = dlsym(vcl->dlh, "VCL_conf");
	if (cnf == NULL) {
		cli_out(cli, "Internal error: No VCL_conf symbol\n");
		(void)dlclose(vcl->dlh);
		FREE_OBJ(vcl);
		return (1);
	}
	memcpy(vcl->conf, cnf, sizeof *cnf);

	if (vcl->conf->magic != VCL_CONF_MAGIC) {
		cli_out(cli, "Wrong VCL_CONF_MAGIC\n");
		(void)dlclose(vcl->dlh);
		FREE_OBJ(vcl);
		return (1);
	}
	REPLACE(vcl->name, name);
	VTAILQ_INSERT_TAIL(&vcl_head, vcl, list);
	LOCK(&vcl_mtx);
	if (vcl_active == NULL)
		vcl_active = vcl;
	UNLOCK(&vcl_mtx);
	cli_out(cli, "Loaded \"%s\" as \"%s\"", fn , name);
	vcl->conf->init_func(cli);
	VSL_stats->n_vcl++;
	VSL_stats->n_vcl_avail++;
	return (0);
}

/*--------------------------------------------------------------------
 * This function is polled from the CLI thread to dispose of any non-busy
 * VCLs which have been discarded.
 */

static void
VCL_Nuke(struct vcls *vcl)
{

	ASSERT_CLI();
	assert(vcl != vcl_active);
	assert(vcl->conf->discard);
	assert(vcl->conf->busy == 0);
	VTAILQ_REMOVE(&vcl_head, vcl, list);
	vcl->conf->fini_func(NULL);
	free(vcl->name);
	(void)dlclose(vcl->dlh);
	FREE_OBJ(vcl);
	VSL_stats->n_vcl--;
	VSL_stats->n_vcl_discard--;
}

/*--------------------------------------------------------------------*/

void
VCL_Poll(void)
{
	struct vcls *vcl, *vcl2;

	ASSERT_CLI();
	VTAILQ_FOREACH_SAFE(vcl, &vcl_head, list, vcl2) 
		if (vcl->conf->discard && vcl->conf->busy == 0) 
			VCL_Nuke(vcl);
}

/*--------------------------------------------------------------------*/

static void
ccf_config_list(struct cli *cli, const char * const *av, void *priv)
{
	struct vcls *vcl;
	const char *flg;

	(void)av;
	(void)priv;
	ASSERT_CLI();
	VTAILQ_FOREACH(vcl, &vcl_head, list) {
		if (vcl == vcl_active) {
			flg = "active";
		} else if (vcl->conf->discard) {
			flg = "discarded";
		} else
			flg = "available";
		cli_out(cli, "%-10s %6u %s\n",
		    flg,
		    vcl->conf->busy,
		    vcl->name);
	}
}

static void
ccf_config_load(struct cli *cli, const char * const *av, void *priv)
{

	(void)av;
	(void)priv;
	ASSERT_CLI();
	if (VCL_Load(av[3], av[2], cli))
		cli_result(cli, CLIS_PARAM);
	return;
}

static void
ccf_config_discard(struct cli *cli, const char * const *av, void *priv)
{
	struct vcls *vcl;

	ASSERT_CLI();
	(void)av;
	(void)priv;
	vcl = vcl_find(av[2]);
	if (vcl == NULL) {
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "VCL '%s' unknown", av[2]);
		return;
	}
	LOCK(&vcl_mtx);
	if (vcl == vcl_active) {
		UNLOCK(&vcl_mtx);
		cli_result(cli, CLIS_PARAM);
		cli_out(cli, "VCL %s is the active VCL", av[2]);
		return;
	}
	VSL_stats->n_vcl_discard++;
	VSL_stats->n_vcl_avail--;
	vcl->conf->discard = 1;
	UNLOCK(&vcl_mtx);
	if (vcl->conf->busy == 0)
		VCL_Nuke(vcl);
}

static void
ccf_config_use(struct cli *cli, const char * const *av, void *priv)
{
	struct vcls *vcl;

	(void)av;
	(void)priv;
	vcl = vcl_find(av[2]);
	if (vcl == NULL) {
		cli_out(cli, "No VCL named '%s'", av[2]);
		cli_result(cli, CLIS_PARAM);
		return;
	}
	LOCK(&vcl_mtx);
	vcl_active = vcl;
	UNLOCK(&vcl_mtx);
}

/*--------------------------------------------------------------------*/

static const char *
vcl_handlingname(unsigned u)
{

	switch (u) {
#define VCL_RET_MAC(a, b, c,d)	case VCL_RET_##b: return(#a);
#define VCL_RET_MAC_E(a, b, c,d)	case VCL_RET_##b: return(#a);
#include "vcl_returns.h"
#undef VCL_RET_MAC
#undef VCL_RET_MAC_E
	default:
		return (NULL);
	}
}

#define VCL_RET_MAC(l,u,b,n)

#define VCL_MET_MAC(func, upper, bitmap)				\
void									\
VCL_##func##_method(struct sess *sp)					\
{									\
									\
	sp->handling = 0;						\
	sp->cur_method = VCL_MET_ ## upper;				\
	WSP(sp, SLT_VCL_call, "%s", #func); 				\
	sp->vcl->func##_func(sp);					\
	WSP(sp, SLT_VCL_return, "%s", vcl_handlingname(sp->handling));	\
	sp->cur_method = 0;						\
	assert(sp->handling & bitmap);					\
	assert(!(sp->handling & ~bitmap));				\
}

#include "vcl_returns.h"
#undef VCL_MET_MAC
#undef VCL_RET_MAC

/*--------------------------------------------------------------------*/

static struct cli_proto vcl_cmds[] = {
	{ CLI_VCL_LOAD,         ccf_config_load },
	{ CLI_VCL_LIST,         ccf_config_list },
	{ CLI_VCL_DISCARD,      ccf_config_discard },
	{ CLI_VCL_USE,          ccf_config_use },
	{ NULL }        
};

void
VCL_Init()
{

	CLI_AddFuncs(MASTER_CLI, vcl_cmds);
	MTX_INIT(&vcl_mtx);
}
