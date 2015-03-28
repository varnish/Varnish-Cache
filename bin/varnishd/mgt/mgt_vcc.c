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
 *
 * VCL compiler stuff
 */

#include "config.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "common/params.h"
#include "mgt/mgt.h"

#include "libvcc.h"
#include "vcl.h"
#include "vcli.h"
#include "vcli_priv.h"
#include "vfil.h"
#include "vsub.h"

struct vcc_priv {
	unsigned	magic;
#define VCC_PRIV_MAGIC	0x70080cb8
	const char	*src;
	char		*srcfile;
	char		*libfile;
};

char *mgt_cc_cmd;
const char *mgt_vcl_dir;
const char *mgt_vmod_dir;
unsigned mgt_vcc_err_unref;
unsigned mgt_vcc_allow_inline_c;
unsigned mgt_vcc_unsafe_path;

static struct vcc *vcc;

/*--------------------------------------------------------------------*/

static const char * const builtin_vcl =
#include "builtin_vcl.h"
    ""	;

/*--------------------------------------------------------------------
 * Invoke system VCC compiler in a sub-process
 */

static void
run_vcc(void *priv)
{
	char *csrc;
	struct vsb *sb;
	struct vcc_priv *vp;
	int fd, i, l;

	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);
	VJ_subproc(JAIL_SUBPROC_VCC);
	sb = VSB_new_auto();
	XXXAN(sb);
	VCC_VCL_dir(vcc, mgt_vcl_dir);
	VCC_VMOD_dir(vcc, mgt_vmod_dir);
	VCC_Err_Unref(vcc, mgt_vcc_err_unref);
	VCC_Allow_InlineC(vcc, mgt_vcc_allow_inline_c);
	VCC_Unsafe_Path(vcc, mgt_vcc_unsafe_path);
	csrc = VCC_Compile(vcc, sb, vp->src);
	AZ(VSB_finish(sb));
	if (VSB_len(sb))
		printf("%s", VSB_data(sb));
	VSB_delete(sb);
	if (csrc == NULL)
		exit(2);

	fd = open(vp->srcfile, O_WRONLY|O_TRUNC|O_CREAT, 0600);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s", vp->srcfile);
		exit(2);
	}
	l = strlen(csrc);
	i = write(fd, csrc, l);
	if (i != l) {
		fprintf(stderr, "Cannot write %s", vp->srcfile);
		exit(2);
	}
	AZ(close(fd));
	free(csrc);
	exit(0);
}

/*--------------------------------------------------------------------
 * Invoke system C compiler in a sub-process
 */

static void
run_cc(void *priv)
{
	struct vcc_priv *vp;
	struct vsb *sb;
	int pct;
	char *p;

	VJ_subproc(JAIL_SUBPROC_CC);
	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);

	sb = VSB_new_auto();
	AN(sb);
	for (p = mgt_cc_cmd, pct = 0; *p; ++p) {
		if (pct) {
			switch (*p) {
			case 's':
				VSB_cat(sb, vp->srcfile);
				break;
			case 'o':
				VSB_cat(sb, vp->libfile);
				break;
			case '%':
				VSB_putc(sb, '%');
				break;
			default:
				VSB_putc(sb, '%');
				VSB_putc(sb, *p);
				break;
			}
			pct = 0;
		} else if (*p == '%') {
			pct = 1;
		} else {
			VSB_putc(sb, *p);
		}
	}
	if (pct)
		VSB_putc(sb, '%');
	AZ(VSB_finish(sb));

	(void)umask(077);
	(void)execl("/bin/sh", "/bin/sh", "-c", VSB_data(sb), (char*)0);
	VSB_delete(sb);				// For flexelint
}

/*--------------------------------------------------------------------
 * Attempt to open compiled VCL in a sub-process
 */

static void __match_proto__(sub_func_f)
run_dlopen(void *priv)
{
	void *dlh;
	struct VCL_conf const *cnf;
	struct vcc_priv *vp;

	VJ_subproc(JAIL_SUBPROC_VCLLOAD);
	CAST_OBJ_NOTNULL(vp, priv, VCC_PRIV_MAGIC);

	/* Try to load the object into this sub-process */
	if ((dlh = dlopen(vp->libfile, RTLD_NOW | RTLD_LOCAL)) == NULL) {
		fprintf(stderr, "Compiled VCL program failed to load:\n  %s\n",
		    dlerror());
		exit(1);
	}

	cnf = dlsym(dlh, "VCL_conf");
	if (cnf == NULL) {
		fprintf(stderr, "Compiled VCL program, metadata not found\n");
		exit(1);
	}

	if (cnf->magic != VCL_CONF_MAGIC) {
		fprintf(stderr, "Compiled VCL program, mangled metadata\n");
		exit(1);
	}

	if (dlclose(dlh)) {
		fprintf(stderr,
		    "Compiled VCL program failed to unload:\n  %s\n",
		    dlerror());
		exit(1);
	}
	exit(0);
}

/*--------------------------------------------------------------------
 * Touch a filename and make it available to privsep-privs
 */

static int
mgt_vcc_touchfile(const char *fn, struct vsb *sb)
{
	int i;

	i = open(fn, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (i < 0) {
		VSB_printf(sb, "Failed to create %s: %s", fn, strerror(errno));
		return (2);
	}
	if (fchown(i, mgt_param.uid, mgt_param.gid) != 0)
		if (geteuid() == 0)
			VSB_printf(sb, "Failed to change owner on %s: %s\n",
			    fn, strerror(errno));
	AZ(close(i));
	return (0);
}

/*--------------------------------------------------------------------
 * Compile a VCL program, return shared object, errors in sb.
 */

static unsigned
mgt_vcc_compile(struct vcc_priv *vp, struct vsb *sb, int C_flag)
{
	char *csrc;
	unsigned subs;

	if (mgt_vcc_touchfile(vp->srcfile, sb))
		return (2);
	if (mgt_vcc_touchfile(vp->libfile, sb))
		return (2);

	subs = VSUB_run(sb, run_vcc, vp, "VCC-compiler", -1);
	if (subs)
		return (subs);

	if (C_flag) {
		csrc = VFIL_readfile(NULL, vp->srcfile, NULL);
		AN(csrc);
		VSB_cat(sb, csrc);
		free(csrc);
	}

	subs = VSUB_run(sb, run_cc, vp, "C-compiler", 10);
	if (subs)
		return (subs);

	subs = VSUB_run(sb, run_dlopen, vp, "dlopen", 10);
	return (subs);
}

/*--------------------------------------------------------------------*/

char *
mgt_VccCompile(struct cli *cli, const char *vclname, const char *vclsrc,
    int C_flag)
{
	struct vcc_priv vp;
	struct vsb *sb;
	unsigned status;

	AN(cli);

	sb = VSB_new_auto();
	XXXAN(sb);

	INIT_OBJ(&vp, VCC_PRIV_MAGIC);
	vp.src = vclsrc;

	VSB_printf(sb, "./vcl_%s.c", vclname);
	AZ(VSB_finish(sb));
	vp.srcfile = strdup(VSB_data(sb));
	AN(vp.srcfile);
	VSB_clear(sb);

	VSB_printf(sb, "./vcl_%s.so", vclname);
	AZ(VSB_finish(sb));
	vp.libfile = strdup(VSB_data(sb));
	AN(vp.srcfile);
	VSB_clear(sb);

	status = mgt_vcc_compile(&vp, sb, C_flag);

	AZ(VSB_finish(sb));
	if (VSB_len(sb) > 0)
		VCLI_Out(cli, "%s", VSB_data(sb));
	VSB_delete(sb);

	(void)unlink(vp.srcfile);
	free(vp.srcfile);

	if (status || C_flag) {
		(void)unlink(vp.libfile);
		free(vp.libfile);
		if (!C_flag) {
			VCLI_Out(cli, "VCL compilation failed");
			VCLI_SetResult(cli, CLIS_PARAM);
		}
		return(NULL);
	}

	VCLI_Out(cli, "VCL compiled.\n");

	return (vp.libfile);
}

/*--------------------------------------------------------------------*/

void
mgt_vcc_init(void)
{

	vcc = VCC_New();
	AN(vcc);
	VCC_Builtin_VCL(vcc, builtin_vcl);
}
