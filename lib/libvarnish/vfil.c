/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Dag-Erling Smørgrav <des@des.no>
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

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
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

#include "vas.h"
#include "vdef.h"
#include "vfil.h"

char *
VFIL_readfd(int fd, ssize_t *sz)
{
	struct stat st;
	char *f;
	int i;

	AZ(fstat(fd, &st));
	if (!S_ISREG(st.st_mode))
		return (NULL);
	f = malloc(st.st_size + 1);
	assert(f != NULL);
	i = read(fd, f, st.st_size + 1);
	if (i != st.st_size) {
		free(f);
		return (NULL);
	}
	f[i] = '\0';
	if (sz != NULL)
		*sz = st.st_size;
	return (f);
}

char *
VFIL_readfile(const char *pfx, const char *fn, ssize_t *sz)
{
	int fd, err;
	char *r;
	char fnb[PATH_MAX + 1];

	if (fn[0] == '/')
		fd = open(fn, O_RDONLY);
	else if (pfx != NULL) {
		bprintf(fnb, "/%s/%s", pfx, fn);
		    /* XXX: graceful length check */
		fd = open(fnb, O_RDONLY);
	} else
		fd = open(fn, O_RDONLY);
	if (fd < 0)
		return (NULL);
	r = VFIL_readfd(fd, sz);
	err = errno;
	AZ(close(fd));
	errno = err;
	return (r);
}

int
VFIL_nonblocking(int fd)
{
	int i;

	i = fcntl(fd, F_GETFL);
	assert(i != -1);
	i |= O_NONBLOCK;
	i = fcntl(fd, F_SETFL, i);
	assert(i != -1);
	return (i);
}

/*
 * Get file system information from an fd
 * Returns block size, total size and space available in the passed pointers
 * Returns 0 on success, or -1 on failure with errno set
 */
int
VFIL_fsinfo(int fd, unsigned *pbs, uintmax_t *psize, uintmax_t *pspace)
{
	unsigned bs;
	uintmax_t size, space;
#if defined(HAVE_SYS_STATVFS_H)
	struct statvfs fsst;

	if (fstatvfs(fd, &fsst))
		return (-1);
	bs = fsst.f_frsize;
	size = fsst.f_blocks * fsst.f_frsize;
	space = fsst.f_bavail * fsst.f_frsize;
#elif defined(HAVE_SYS_MOUNT_H) || defined(HAVE_SYS_VFS_H)
	struct statfs fsst;

	if (fstatfs(fd, &fsst))
		return (-1);
	bs = fsst.f_bsize;
	size = fsst.f_blocks * fsst.f_bsize;
	space = fsst.f_bavail * fsst.f_bsize;
#else
#error no struct statfs / struct statvfs
#endif

	if (pbs)
		*pbs = bs;
	if (psize)
		*psize = size;
	if (pspace)
		*pspace = space;
	return (0);
}

/* Make sure that the file system can accomodate the file of the given
 * size. Will use fallocate if available. If fallocate is not available
 * and insist is true, it will write size zero bytes.
 *
 * Returns 0 on success, -1 on failure with errno set.
 */
int
VFIL_allocate(int fd, off_t size, int insist)
{
	struct stat st;
	uintmax_t fsspace;
	size_t l;
	ssize_t l2;
	char buf[64 * 1024];

	if (ftruncate(fd, size))
		return (-1);
	if (fstat(fd, &st))
		return (-1);
	if (VFIL_fsinfo(fd, NULL, NULL, &fsspace))
		return (-1);
	if ((st.st_blocks * 512) + fsspace < size) {
		/* Sum of currently allocated blocks and available space
		   is less than requested size */
		errno = ENOSPC;
		return (-1);
	}
#ifdef HAVE_FALLOCATE
	if (!fallocate(fd, 0, 0, size))
		return (0);
	if (errno == ENOSPC)
		return (-1);
#endif
	if (!insist)
		return (0);

	/* Write size zero bytes to make sure the entire file is allocated
	   in the file system */
	memset(buf, 0, sizeof buf);
	assert(lseek(fd, 0, SEEK_SET) == 0);
	for (l = 0; l < size; l += l2) {
		l2 = sizeof buf;
		if (l + l2 > size)
			l2 = size - l;
		l2 = write(fd, buf, l2);
		if (l2 < 0)
			return (-1);
	}
	assert(lseek(fd, 0, SEEK_SET) == 0);
	return (0);
}
