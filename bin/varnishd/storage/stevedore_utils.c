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
 * Utility functions for stevedores and storage modules
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_MOUNT_H
#  include <sys/param.h>
#  include <sys/mount.h>
#endif
#ifdef HAVE_SYS_STATVFS_H
#  include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_VFS_H
#  include <sys/vfs.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"

#include "storage/storage.h"
#include "vnum.h"
#include "vfil.h"

#ifndef O_LARGEFILE
#define O_LARGEFILE	0
#endif

/*--------------------------------------------------------------------
 * Get a storage file.
 *
 * The fn argument can be an existing file, an existing directory or
 * a nonexistent filename in an existing directory.
 *
 * If a directory is specified, the file will be anonymous (unlinked)
 *
 * Return:
 *	 0 if the file was preexisting.
 *	 1 if the file was created.
 *	 2 if the file is anonymous.
 *
 * Uses ARGV_ERR to exit in case of trouble.
 */

int
STV_GetFile(const char *fn, int *fdp, const char **fnp, const char *ctx)
{
	int fd;
	struct stat st;
	int retval = 1;
	char buf[FILENAME_MAX];

	AN(fn);
	AN(fnp);
	AN(fdp);
	*fnp = NULL;
	*fdp = -1;

	/* try to create a new file of this name */
	VJ_master(JAIL_MASTER_STORAGE);
	fd = open(fn, O_RDWR | O_CREAT | O_EXCL | O_LARGEFILE, 0600);
	if (fd >= 0) {
		VJ_storage_file(fd);
		*fdp = fd;
		*fnp = fn;
		VJ_master(JAIL_MASTER_LOW);
		return (retval);
	}

	if (stat(fn, &st))
		ARGV_ERR(
		    "(%s) \"%s\" does not exist and could not be created\n",
		    ctx, fn);

	if (S_ISDIR(st.st_mode)) {
		bprintf(buf, "%s/varnish.XXXXXX", fn);
		fd = mkstemp(buf);
		if (fd < 0)
			ARGV_ERR("(%s) \"%s\" mkstemp(%s) failed (%s)\n",
			    ctx, fn, buf, strerror(errno));
		AZ(unlink(buf));
		*fnp = strdup(buf);
		AN(*fnp);
		retval = 2;
	} else if (S_ISREG(st.st_mode)) {
		fd = open(fn, O_RDWR | O_LARGEFILE);
		if (fd < 0)
			ARGV_ERR("(%s) \"%s\" could not open (%s)\n",
			    ctx, fn, strerror(errno));
		*fnp = fn;
		retval = 0;
	} else
		ARGV_ERR(
		    "(%s) \"%s\" is neither file nor directory\n", ctx, fn);

	AZ(fstat(fd, &st));
	if (!S_ISREG(st.st_mode))
		ARGV_ERR("(%s) \"%s\" was not a file after opening\n",
		    ctx, fn);

	*fdp = fd;
	VJ_storage_file(fd);
	VJ_master(JAIL_MASTER_LOW);
	return (retval);
}

/*--------------------------------------------------------------------
 * Decide file size.
 *
 * If the size specification is empty and the file exists with non-zero
 * size, use that, otherwise, interpret the specification.
 *
 * Handle off_t sizes and pointer width limitations.
 */

uintmax_t
STV_FileSize(int fd, const char *size, unsigned *granularity, const char *ctx)
{
	uintmax_t l, fssize;
	unsigned bs;
	const char *q;
	int i;
	off_t o;
	struct stat st;

	AN(granularity);
	AN(ctx);

	AZ(fstat(fd, &st));
	xxxassert(S_ISREG(st.st_mode));

	AZ(VFIL_fsinfo(fd, &bs, &fssize, NULL));
	/* Increase granularity if it is lower than the filesystem block size */
	if (*granularity < bs)
		*granularity = bs;

	if ((size == NULL || *size == '\0') && st.st_size != 0) {
		/*
		 * We have no size specification, but an existing file,
		 * use its existing size.
		 */
		l = st.st_size;
	} else if (size == NULL || *size == '\0') {
		ARGV_ERR("(%s) no size specified\n",
		    ctx);
	} else {
		AN(size);
		q = VNUM_2bytes(size, &l, 0);

		if (q != NULL)
			ARGV_ERR("(%s) size \"%s\": %s\n", ctx, size, q);

		if (l < 1024*1024)
			ARGV_ERR("(%s) size \"%s\": too small, "
			    "did you forget to specify M or G?\n", ctx, size);

		if (l > fssize)
			ARGV_ERR("(%s) size \"%s\": larger than file system\n",
			    ctx, size);
	}

	/*
	 * This trickery wouldn't be necessary if X/Open would
	 * just add OFF_MAX to <limits.h>...
	 */
	i = 0;
	while(1) {
		o = l;
		if (o == l && o > 0)
			break;
		l >>= 1;
		i++;
	}
	if (i)
		fprintf(stderr, "WARNING: (%s) file size reduced"
		    " to %ju due to system \"off_t\" limitations\n", ctx, l);

	if (sizeof(void *) == 4 && l > INT32_MAX) { /*lint !e506 !e774 !e845 */
		fprintf(stderr,
		    "NB: Storage size limited to 2GB on 32 bit architecture,\n"
		    "NB: otherwise we could run out of address space.\n"
		);
		l = INT32_MAX;
	}

	/* Round down */
	l -= (l % *granularity);

	return(l);
}
