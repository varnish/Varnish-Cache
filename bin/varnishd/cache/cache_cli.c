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
 * Caching process CLI handling.
 *
 * We only have one CLI source, the stdin/stdout pipes from the manager
 * process, but we complicate things by having undocumented commands that
 * we do not want to show in a plain help, and by having commands that the
 * manager has already shown in help before asking us.
 */

#include "config.h"

#include "cache.h"
#include "common/heritage.h"

#include "vcli.h"
#include "vcli_common.h"
#include "vcli_priv.h"
#include "vcli_serve.h"

pthread_t		cli_thread;
static struct lock	cli_mtx;
static int		add_check;
static struct VCLS	*cls;

/*
 * The CLI commandlist is split in three:
 *  - Commands we get from/share with the manager, we don't show these
 *	in help, as the manager already did that.
 *  - Cache process commands, show in help
 *  - Undocumented debug commands, show in undocumented "help -d"
 */

/*--------------------------------------------------------------------
 * Add CLI functions to the appropriate command set
 */

void
CLI_AddFuncs(struct cli_proto *p)
{

	AZ(add_check);
	Lck_Lock(&cli_mtx);
	AZ(VCLS_AddFunc(cls, 0, p));
	Lck_Unlock(&cli_mtx);
}

static void
cli_cb_before(const struct cli *cli)
{

	ASSERT_CLI();
	VSL(SLT_CLI, 0, "Rd %s", cli->cmd);
	VCL_Poll();
	Lck_Lock(&cli_mtx);
}

static void
cli_cb_after(const struct cli *cli)
{

	ASSERT_CLI();
	Lck_Unlock(&cli_mtx);
	VSL(SLT_CLI, 0, "Wr %03u %zd %s",
	    cli->result, VSB_len(cli->sb), VSB_data(cli->sb));
}

void
CLI_Run(void)
{
	int i;

	add_check = 1;

	AN(VCLS_AddFd(cls, heritage.cli_in, heritage.cli_out, NULL, NULL));

	do {
		i = VCLS_Poll(cls, -1);
	} while(i > 0);
	VSL(SLT_CLI, 0, "EOF on CLI connection, worker stops");
	VCA_Shutdown();
}

/*--------------------------------------------------------------------*/

static struct cli_proto cli_cmds[] = {
	{ CLI_PING,		"i", VCLS_func_ping },
	{ CLI_HELP,             "i", VCLS_func_help },
	{ NULL }
};

/*--------------------------------------------------------------------
 * Initialize the CLI subsystem
 */

void
CLI_Init(void)
{

	Lck_New(&cli_mtx, lck_cli);
	cli_thread = pthread_self();

	cls = VCLS_New(cli_cb_before, cli_cb_after,
	    &cache_param->cli_buffer, &cache_param->cli_limit);
	AN(cls);

	CLI_AddFuncs(cli_cmds);
}
