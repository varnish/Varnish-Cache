/*-
 * Copyright (c) 2006-2011 Varnish Software AS
 * Copyright (c) 2011-2015 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *	   Nils Goroll <nils.goroll@uplex.de>
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
 * "Jailing" *1) child processes on Solaris and Solaris-derivates *2)
 * ==================================================================
 *
 * *1) The name is motivated by the availability of the -j command line
 *     option. Jailing Varnish is not to be confused with BSD Jails or
 *     Solaris Zones.
 *
 *     In Solaris parlour, jail == least privileges
 *
 * *2) e.g. illumos, SmartOS, OmniOS etc.
 *
 *
 * Note on use of symbolic PRIV_* constants
 * ----------------------------------------
 *
 * We assume backwards compatibility only for Solaris Releases after the
 * OpenSolaris Launch. For privileges which existed at the time of the
 * OpenSolaris Launch, we use the constants from sys/priv_names.h and assert
 * that priv_addset must succeed.
 *
 * For privileges which have been added later, we need to use priv strings in
 * order not to break builds of varnish on older platforms. To remain binary
 * compatible, we can't assert that priv_addset succeeds, but we may assert that
 * it either succeeds or fails with EINVAL.
 *
 * See priv_setop_check()
 *
 * Note on introduction of new privileges (or: lack of forward compatibility)
 * --------------------------------------------------------------------------
 *
 * For optimal build and binary forward compatibility, we could use subtractive
 * set specs like
 *
 *       basic,!file_link_any,!proc_exec,!proc_fork,!proc_info,!proc_session
 *
 * which would implicitly keep any privileges newly introduced to the 'basic'
 * set.
 *
 * But we have a preference for making an informed decision about which
 * privileges varnish subprocesses should have, so we prefer to risk breaking
 * varnish temporarily on newer kernels and be notified of missing privileges
 * through bug reports.
 *
 * Notes on the SNOCD flag
 * -----------------------
 *
 * On Solaris, any uid/gid fiddling which can be interpreted as 'waiving
 * privileges' will lead to the processes' SNOCD flag being set, disabling core
 * dumps unless explicitly allowed using coreadm (see below). There is no
 * equivalent to Linux PR_SET_DUMPABLE. The only way to clear the flag is a call
 * to some form of exec(). The presence of the SNOCD flag also prevents many
 * process manipulations from other processes with the same uid/gid unless the
 * latter have the proc_owner privilege.
 *
 * Thus, if we want to run subprocesses with a different uid/gid than the master
 * process, we cannot avoid the SNOCD flag for those subprocesses not exec'ing
 * (VCC, VCLLOAD, WORKER).
 *
 *
 * We should, however, avoid to accidentally set the SNOCD flag when setting
 * privileges (see https://www.varnish-cache.org/trac/ticket/671 )
 *
 * When changing the logic herein, always check with mdb -k. Replace _PID_ with
 * the pid of your varnish child, the result should be 0, otherwise a regression
 * has been introduced.
 *
 * > 0t_PID_::pid2proc | ::print proc_t p_flag | >a
 * > (<a & 0x10000000)=X
 *		0
 *
 * (a value of 0x10000000 indicates that SNOCD is set)
 *
 * How to get core dumps of the worker process on Solaris
 * ------------------------------------------------------
 *
 * (see previous paragraph for explanation).
 *
 * Two options:
 *
 * - start the varnish master process under the same user/group given for the -u
 *   / -g command line option and elevated privileges but without proc_setid,
 *   e.g.:
 *
 *	pfexec ppriv -e -s A=basic,net_privaddr,sys_resource varnishd ...
 *
 * - allow coredumps of setid processes (ignoring SNOCD)
 *
 *   See coreadm(1M) - global-setid / proc-setid
 *
 * brief history of privileges introduced since OpenSolaris Launch
 * ---------------------------------------------------------------
 *
 * (from hg log -gp usr/src/uts/common/os/priv_defs
 *    or git log -p usr/src/uts/common/os/priv_defs)
 *
 * ARC cases are not necessarily accurate (induced from commit msg)
 *
 * privileges used here marked with *
 *
 * Illumos ticket
 * ARC case	    hg/git commit  first release
 *
 * PSARC/2006/155?  37f4a3e2bd99   onnv_37
 * - file_downgrade_sl
 * - file_upgrade_sl
 * - net_bindmlp
 * - net_mac_aware
 * - sys_trans_label
 * - win_colormap
 * - win_config
 * - win_dac_read
 * - win_dac_write
 * - win_devices
 * - win_dga
 * - win_downgrade_sl
 * - win_fontpath
 * - win_mac_read
 * - win_mac_write
 * - win_selection
 * - win_upgrade_sl
 *
 * PSARC/2006/218   5dbf296c1e57   onnv_39
 * - graphics_access
 * - graphics_map
 *
 * PSARC/2006/366   aaf16568054b   onnv_57
 * - net_config
 *
 * PSARC/2007/315?  3047ad28a67b   onnv_77
 * - file_flag_set
 *
 * PSARC/2007/560?  3047ad28a67b   onnv_77
 * - sys_smb
 *
 * PSARC 2008/046   47f6aa7a8077   onnv_85
 * - contract_identify
 *
 * PSARC 2008/289   79a9dac325d9   onnv_92
 * - virt_manage
 * - xvm_control
 *
 * PSARC 2008/473   eff7960d93cd   onnv_98
 * - sys_dl_config
 *
 * PSARC/2006/475   faf256d5c16c   onnv_103
 * - net_observability
 *
 * PSARC/2009/317   8e29565352fc   onnv_117
 * - sys_ppp_config
 *
 * PSARC/2009/373   3be00c4a6835   onnv_125
 * - sys_iptun_config
 *
 * PSARC/2008/252   e209937a4f19   onnv_128
 * - net_mac_implicit
 *
 * PSARC/2009/685   8eca52188202   onnv_132
 * * net_access
 *
 * PSARC/2009/378   63678502e95e   onnv_140
 * * file_read
 * * file_write
 *
 * PSARC/2010/181   15439b11d535   onnv_142
 * - sys_res_bind
 *
 * unknown	    unknown	   Solaris11
 * - sys_flow_config
 * - sys_share
 *
 * IL3923	    24d819e6779c   Illumos
 * - proc_prioup
 *
 */

#include "config.h"

#ifdef HAVE_SETPPRIV

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "mgt/mgt.h"

#include "common/heritage.h"
#include "common/params.h"

#ifdef HAVE_PRIV_H
#include <priv.h>
#endif

/* ============================================================
 * the real thing
 */

// XXX @phk can we merge jail_subproc_e and jail_master_e please?

#define JAILG_SHIFT 16

enum jail_gen_e {
	JAILG_SUBPROC_VCC = JAIL_SUBPROC_VCC,
	JAILG_SUBPROC_CC = JAIL_SUBPROC_CC,
	JAILG_SUBPROC_VCLLOAD = JAIL_SUBPROC_VCLLOAD,
	JAILG_SUBPROC_WORKER = JAIL_SUBPROC_WORKER,

	JAILG_MASTER_LOW = JAIL_MASTER_LOW << JAILG_SHIFT,
	JAILG_MASTER_STORAGE = JAIL_MASTER_STORAGE << JAILG_SHIFT,
	JAILG_MASTER_PRIVPORT = JAIL_MASTER_PRIVPORT << JAILG_SHIFT
};

static inline enum jail_gen_e
jail_subproc_gen(enum jail_subproc_e e)
{
	assert(e < (1 << JAILG_SHIFT));
	return (enum jail_gen_e)e;
}

static inline enum jail_gen_e
jail_master_gen(enum jail_master_e e)
{
	return (enum jail_gen_e)(e << JAILG_SHIFT);
}

static int __match_proto__(jail_init_f)
vjs_init(char **args)
{
	(void) args;
	return 0;
}

/* for priv_delset() and priv_addset() */
static inline int
priv_setop_check(int a) {
	if (a == 0)
		return (1);
	if (errno == EINVAL)
		return (1);
	return (0);
}

#define priv_setop_assert(a) assert(priv_setop_check(a))

/*
 * we try to add all possible privileges to waive them later.
 *
 * when doing so, we need to expect EPERM
 */

/* for setppriv */
static inline int
setppriv_check(int a) {
	if (a == 0)
		return (1);
	if (errno == EPERM)
		return (1);
	return (0);
}

#define setppriv_assert(a) assert(setppriv_check(a))

static void
vjs_add_inheritable(priv_set_t *pset, enum jail_gen_e jge)
{
	switch (jge) {
	case JAILG_SUBPROC_VCC:
		break;
	case JAILG_SUBPROC_CC:
		priv_setop_assert(priv_addset(pset, PRIV_PROC_EXEC));
		priv_setop_assert(priv_addset(pset, PRIV_PROC_FORK));
		priv_setop_assert(priv_addset(pset, "file_read"));
		priv_setop_assert(priv_addset(pset, "file_write"));
		break;
	case JAILG_SUBPROC_VCLLOAD:
		break;
	case JAILG_SUBPROC_WORKER:
		break;
	default:
		INCOMPL();
	}
}

static void
vjs_add_effective(priv_set_t *pset, enum jail_gen_e jge)
{
	switch (jge) {
	case JAILG_SUBPROC_VCC:
		// open vmods
		priv_setop_assert(priv_addset(pset, "file_read"));
		// write .c output
		priv_setop_assert(priv_addset(pset, "file_write"));
		break;
	case JAILG_SUBPROC_CC:
		priv_setop_assert(priv_addset(pset, PRIV_PROC_EXEC));
		priv_setop_assert(priv_addset(pset, PRIV_PROC_FORK));
		priv_setop_assert(priv_addset(pset, "file_read"));
		priv_setop_assert(priv_addset(pset, "file_write"));
		break;
	case JAILG_SUBPROC_VCLLOAD:
		priv_setop_assert(priv_addset(pset, "file_read"));
		break;
	case JAILG_SUBPROC_WORKER:
		priv_setop_assert(priv_addset(pset, "net_access"));
		priv_setop_assert(priv_addset(pset, "file_read"));
		priv_setop_assert(priv_addset(pset, "file_write"));
		break;
	default:
		INCOMPL();
	}
}

/*
 * permitted is initialized from effective (see vjs_waive)
 * so only additionally required privileges need to be added here
 */

static void
vjs_add_permitted(priv_set_t *pset, enum jail_gen_e jge)
{
	switch (jge) {
	case JAILG_SUBPROC_VCC:
	case JAILG_SUBPROC_CC:
	case JAILG_SUBPROC_VCLLOAD:
		break;
	case JAILG_SUBPROC_WORKER:
		/* for raising limits in cache_waiter_ports.c */
		AZ(priv_addset(pset, PRIV_SYS_RESOURCE));
		break;
	default:
		INCOMPL();
	}
}

/*
 * additional privileges needed by vjs_privsep -
 * will get waived in vjs_waive
 */
static void
vjs_add_initial(priv_set_t *pset, enum jail_gen_e jge)
{
	(void)jge;

	/* for setgid/setuid */
	AZ(priv_addset(pset, PRIV_PROC_SETID));
}

/*
 * if we are not yet privilege-aware already (ie we have been started
 * not-privilege aware with euid 0), we try to grab any privileges we
 * will need later.
 * We will reduce to least privileges in vjs_waive
 *
 * We need to become privilege-aware to avoid setuid resetting them.
 */

static void
vjs_setup(enum jail_gen_e jge)
{
	priv_set_t *priv_all;

	if (! (priv_all = priv_allocset())) {
		REPORT(LOG_ERR,
		    "Solaris Jail warning: "
		    " vjs_setup - priv_allocset failed: errno=%d (%s)",
		    errno, strerror(errno));
		return;
	}

	priv_emptyset(priv_all);

	vjs_add_inheritable(priv_all, jge);
	vjs_add_effective(priv_all, jge);
	vjs_add_permitted(priv_all, jge);
	vjs_add_initial(priv_all, jge);

	/* try to get all possible privileges, expect EPERM here */
	setppriv_assert(setppriv(PRIV_ON, PRIV_PERMITTED, priv_all));
	setppriv_assert(setppriv(PRIV_ON, PRIV_EFFECTIVE, priv_all));
	setppriv_assert(setppriv(PRIV_ON, PRIV_INHERITABLE, priv_all));

	priv_freeset(priv_all);
}

static void
vjs_privsep(enum jail_gen_e jge)
{
	(void)jge;

	if (priv_ineffect(PRIV_PROC_SETID)) {
		if (getgid() != mgt_param.gid)
			XXXAZ(setgid(mgt_param.gid));
		if (getuid() != mgt_param.uid)
			XXXAZ(setuid(mgt_param.uid));
	} else {
		REPORT(LOG_INFO,
		    "Privilege %s missing, will not change uid/gid",
		    PRIV_PROC_SETID);
	}
}

/*
 * Waive most privileges in the child
 *
 * as of onnv_151a, we should end up with:
 *
 * > ppriv -v #pid of varnish child
 * PID:  .../varnishd ...
 * flags = PRIV_AWARE
 *      E: file_read,file_write,net_access
 *      I: none
 *      P: file_read,file_write,net_access,sys_resource
 *      L: file_read,file_write,net_access,sys_resource
 *
 * We should keep sys_resource in P in order to adjust our limits if we need to
 */

static void
vjs_waive(enum jail_gen_e jge)
{
	priv_set_t *effective, *inheritable, *permitted, *limited;

	if (!(effective = priv_allocset()) ||
	    !(inheritable = priv_allocset()) ||
	    !(permitted = priv_allocset()) ||
	    !(limited = priv_allocset())) {
		REPORT(LOG_ERR,
		    "Solaris Jail warning: "
		    " vjs_waive - priv_allocset failed: errno=%d (%s)",
		    errno, strerror(errno));
		return;
	}

	/*
	 * inheritable and effective are distinct sets
	 * effective is a subset of permitted
	 * limit is the union of all
	 */

	priv_emptyset(inheritable);
	vjs_add_inheritable(inheritable, jge);

	priv_emptyset(effective);
	vjs_add_effective(effective, jge);

	priv_copyset(effective, permitted);
	vjs_add_permitted(permitted, jge);

	priv_copyset(inheritable, limited);
	priv_union(permitted, limited);
	/*
	 * invert the sets and clear privileges such that setppriv will always
	 * succeed
	 */
	priv_inverse(limited);
	priv_inverse(permitted);
	priv_inverse(effective);
	priv_inverse(inheritable);

	AZ(setppriv(PRIV_OFF, PRIV_LIMIT, limited));
	AZ(setppriv(PRIV_OFF, PRIV_PERMITTED, permitted));
	AZ(setppriv(PRIV_OFF, PRIV_EFFECTIVE, effective));
	AZ(setppriv(PRIV_OFF, PRIV_INHERITABLE, inheritable));

	priv_freeset(limited);
	priv_freeset(permitted);
	priv_freeset(effective);
	priv_freeset(inheritable);
}

static void __match_proto__(jail_subproc_f)
vjs_subproc(enum jail_subproc_e jse)
{
	enum jail_gen_e jge = jail_subproc_gen(jse);
	vjs_setup(jge);
	vjs_privsep(jge);
	vjs_waive(jge);
}

static void __match_proto__(jail_master_f)
vjs_master(enum jail_master_e jme)
{
	enum jail_gen_e jge = jail_master_gen(jme);
	(void)jge;
/*
	if (jme == JAILG_MASTER_HIGH)
		AZ(seteuid(0));
	else
		AZ(seteuid(vju_uid));
*/
}

const struct jail_tech jail_tech_solaris = {
	.magic =	JAIL_TECH_MAGIC,
	.name =	"solaris",
	.init =	vjs_init,
	.master =	vjs_master,
//	.make_workdir =	vjs_make_workdir,
//	.storage_file =	vjs_storage_file,
	.subproc =	vjs_subproc,
};

#endif /* HAVE_SETPPRIV */
