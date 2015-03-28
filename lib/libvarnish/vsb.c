/*-
 * Copyright (c) 2000-2008 Poul-Henning Kamp
 * Copyright (c) 2000-2008 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
__FBSDID("$FreeBSD: head/sys/kern/subr_vsb.c 222004 2011-05-17 06:36:32Z phk $")
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vdef.h"
#include "vas.h"
#include "vsb.h"

#define	KASSERT(e, m)		assert(e)
#define	SBMALLOC(size)		malloc(size)
#define	SBFREE(buf)		free(buf)

#define	roundup2(x, y)	(((x)+((y)-1))&(~((y)-1))) /* if y is powers of two */

/*
 * Predicates
 */
#define	VSB_ISDYNAMIC(s)	((s)->s_flags & VSB_DYNAMIC)
#define	VSB_ISDYNSTRUCT(s)	((s)->s_flags & VSB_DYNSTRUCT)
#define	VSB_HASROOM(s)		((s)->s_len < (s)->s_size - 1)
#define	VSB_FREESPACE(s)	((s)->s_size - ((s)->s_len + 1))
#define	VSB_CANEXTEND(s)	((s)->s_flags & VSB_AUTOEXTEND)

/*
 * Set / clear flags
 */
#define	VSB_SETFLAG(s, f)	do { (s)->s_flags |= (f); } while (0)
#define	VSB_CLEARFLAG(s, f)	do { (s)->s_flags &= ~(f); } while (0)

#define	VSB_MINEXTENDSIZE	16		/* Should be power of 2. */

#ifdef PAGE_SIZE
#define	VSB_MAXEXTENDSIZE	PAGE_SIZE
#define	VSB_MAXEXTENDINCR	PAGE_SIZE
#else
#define	VSB_MAXEXTENDSIZE	4096
#define	VSB_MAXEXTENDINCR	4096
#endif

/*
 * Debugging support
 */
#if !defined(NDEBUG)
static void
_assert_VSB_integrity(const char *fun, const struct vsb *s)
{

	(void)fun;
	(void)s;
	KASSERT(s != NULL,
	    ("%s called with a NULL vsb pointer", fun));
	KASSERT(s->magic == VSB_MAGIC,
	    ("%s called wih an bogus vsb pointer", fun));
	KASSERT(s->s_buf != NULL,
	    ("%s called with uninitialized or corrupt vsb", fun));
	KASSERT(s->s_len < s->s_size,
	    ("wrote past end of vsb (%d >= %d)", s->s_len, s->s_size));
}

static void
_assert_VSB_state(const char *fun, const struct vsb *s, int state)
{

	(void)fun;
	(void)s;
	(void)state;
	KASSERT((s->s_flags & VSB_FINISHED) == state,
	    ("%s called with %sfinished or corrupt vsb", fun,
	    (state ? "un" : "")));
}
#define	assert_VSB_integrity(s) _assert_VSB_integrity(__func__, (s))
#define	assert_VSB_state(s, i)	 _assert_VSB_state(__func__, (s), (i))
#else
#define	assert_VSB_integrity(s) do { } while (0)
#define	assert_VSB_state(s, i)	 do { } while (0)
#endif

#ifdef CTASSERT
CTASSERT(powerof2(VSB_MAXEXTENDSIZE));
CTASSERT(powerof2(VSB_MAXEXTENDINCR));
#endif


static int
VSB_extendsize(int size)
{
	int newsize;

	if (size < (int)VSB_MAXEXTENDSIZE) {
		newsize = VSB_MINEXTENDSIZE;
		while (newsize < size)
			newsize *= 2;
	} else {
		newsize = roundup2(size, VSB_MAXEXTENDINCR);
	}
	KASSERT(newsize >= size, ("%s: %d < %d\n", __func__, newsize, size));
	return (newsize);
}

/*
 * Extend an vsb.
 */
static int
VSB_extend(struct vsb *s, int addlen)
{
	char *newbuf;
	int newsize;

	if (!VSB_CANEXTEND(s))
		return (-1);
	newsize = VSB_extendsize(s->s_size + addlen);
	newbuf = SBMALLOC(newsize);
	if (newbuf == NULL)
		return (-1);
	memcpy(newbuf, s->s_buf, s->s_size);
	if (VSB_ISDYNAMIC(s))
		SBFREE(s->s_buf);
	else
		VSB_SETFLAG(s, VSB_DYNAMIC);
	s->s_buf = newbuf;
	s->s_size = newsize;
	return (0);
}

/*
 * Initialize the internals of an vsb.
 * If buf is non-NULL, it points to a static or already-allocated string
 * big enough to hold at least length characters.
 */
static struct vsb *
VSB_newbuf(struct vsb *s, char *buf, int length, int flags)
{

	memset(s, 0, sizeof(*s));
	s->magic = VSB_MAGIC;
	s->s_flags = flags;
	s->s_size = length;
	s->s_buf = buf;

	if ((s->s_flags & VSB_AUTOEXTEND) == 0) {
		KASSERT(s->s_size > 1,
		    ("attempt to create a too small vsb"));
	}

	if (s->s_buf != NULL)
		return (s);

	if ((flags & VSB_AUTOEXTEND) != 0)
		s->s_size = VSB_extendsize(s->s_size);

	s->s_buf = SBMALLOC(s->s_size);
	if (s->s_buf == NULL)
		return (NULL);
	VSB_SETFLAG(s, VSB_DYNAMIC);
	return (s);
}

/*
 * Initialize an vsb.
 * If buf is non-NULL, it points to a static or already-allocated string
 * big enough to hold at least length characters.
 */
struct vsb *
VSB_new(struct vsb *s, char *buf, int length, int flags)
{

	KASSERT(length >= 0,
	    ("attempt to create an vsb of negative length (%d)", length));
	KASSERT((flags & ~VSB_USRFLAGMSK) == 0,
	    ("%s called with invalid flags", __func__));

	flags &= VSB_USRFLAGMSK;
	if (s != NULL)
		return (VSB_newbuf(s, buf, length, flags));

	s = SBMALLOC(sizeof(*s));
	if (s == NULL)
		return (NULL);
	if (VSB_newbuf(s, buf, length, flags) == NULL) {
		SBFREE(s);
		return (NULL);
	}
	VSB_SETFLAG(s, VSB_DYNSTRUCT);
	return (s);
}

/*
 * Clear an vsb and reset its position.
 */
void
VSB_clear(struct vsb *s)
{

	assert_VSB_integrity(s);
	/* don't care if it's finished or not */

	VSB_CLEARFLAG(s, VSB_FINISHED);
	s->s_error = 0;
	s->s_len = 0;
}

/*
 * Append a byte to an vsb.  This is the core function for appending
 * to an vsb and is the main place that deals with extending the
 * buffer and marking overflow.
 */
static void
VSB_put_byte(struct vsb *s, int c)
{

	assert_VSB_integrity(s);
	assert_VSB_state(s, 0);

	if (s->s_error != 0)
		return;
	if (VSB_FREESPACE(s) <= 0) {
		if (VSB_extend(s, 1) < 0)
			s->s_error = ENOMEM;
		if (s->s_error != 0)
			return;
	}
	s->s_buf[s->s_len++] = (char)c;
}

/*
 * Append a byte string to an vsb.
 */
int
VSB_bcat(struct vsb *s, const void *buf, size_t len)
{
	const char *str = buf;
	const char *end = str + len;

	assert_VSB_integrity(s);
	assert_VSB_state(s, 0);

	if (s->s_error != 0)
		return (-1);
	for (; str < end; str++) {
		VSB_put_byte(s, *str);
		if (s->s_error != 0)
			return (-1);
	}
	return (0);
}

/*
 * Append a string to an vsb.
 */
int
VSB_cat(struct vsb *s, const char *str)
{

	assert_VSB_integrity(s);
	assert_VSB_state(s, 0);

	if (s->s_error != 0)
		return (-1);

	while (*str != '\0') {
		VSB_put_byte(s, *str++);
		if (s->s_error != 0)
			return (-1);
	}
	return (0);
}

/*
 * Format the given argument list and append the resulting string to an vsb.
 */
int
VSB_vprintf(struct vsb *s, const char *fmt, va_list ap)
{
	va_list ap_copy;
	int len;

	assert_VSB_integrity(s);
	assert_VSB_state(s, 0);

	KASSERT(fmt != NULL,
	    ("%s called with a NULL format string", __func__));

	if (s->s_error != 0)
		return (-1);

	/*
	 * For the moment, there is no way to get vsnprintf(3) to hand
	 * back a character at a time, to push everything into
	 * VSB_putc_func() as was done for the kernel.
	 *
	 * In userspace, while drains are useful, there's generally
	 * not a problem attempting to malloc(3) on out of space.  So
	 * expand a userland vsb if there is not enough room for the
	 * data produced by VSB_[v]printf(3).
	 */

	do {
		va_copy(ap_copy, ap);
		len = vsnprintf(&s->s_buf[s->s_len], VSB_FREESPACE(s) + 1,
		    fmt, ap_copy);
		va_end(ap_copy);
	} while (len > VSB_FREESPACE(s) &&
	    VSB_extend(s, len - VSB_FREESPACE(s)) == 0);

	/*
	 * s->s_len is the length of the string, without the terminating nul.
	 * When updating s->s_len, we must subtract 1 from the length that
	 * we passed into vsnprintf() because that length includes the
	 * terminating nul.
	 *
	 * vsnprintf() returns the amount that would have been copied,
	 * given sufficient space, so don't over-increment s_len.
	 */
	if (VSB_FREESPACE(s) < len)
		len = VSB_FREESPACE(s);
	s->s_len += len;
	if (!VSB_HASROOM(s) && !VSB_CANEXTEND(s))
		s->s_error = ENOMEM;

	KASSERT(s->s_len < s->s_size,
	    ("wrote past end of vsb (%d >= %d)", s->s_len, s->s_size));

	if (s->s_error != 0)
		return (-1);
	return (0);
}

/*
 * Format the given arguments and append the resulting string to an vsb.
 */
int
VSB_printf(struct vsb *s, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = VSB_vprintf(s, fmt, ap);
	va_end(ap);
	return (result);
}

/*
 * Append a character to an vsb.
 */
int
VSB_putc(struct vsb *s, int c)
{

	VSB_put_byte(s, c);
	if (s->s_error != 0)
		return (-1);
	return (0);
}

/*
 * Check if an vsb has an error.
 */
int
VSB_error(const struct vsb *s)
{

	return (s->s_error);
}

/*
 * Finish off an vsb.
 */
int
VSB_finish(struct vsb *s)
{

	assert_VSB_integrity(s);
	assert_VSB_state(s, 0);

	s->s_buf[s->s_len] = '\0';
	VSB_SETFLAG(s, VSB_FINISHED);
	errno = s->s_error;
	if (s->s_error)
		return (-1);
	return (0);
}

/*
 * Return a pointer to the vsb data.
 */
char *
VSB_data(const struct vsb *s)
{

	assert_VSB_integrity(s);
	assert_VSB_state(s, VSB_FINISHED);

	return (s->s_buf);
}

/*
 * Return the length of the vsb data.
 */
ssize_t
VSB_len(const struct vsb *s)
{

	assert_VSB_integrity(s);
	/* don't care if it's finished or not */

	if (s->s_error != 0)
		return (-1);
	return (s->s_len);
}

/*
 * Clear an vsb, free its buffer if necessary.
 */
void
VSB_delete(struct vsb *s)
{
	int isdyn;

	assert_VSB_integrity(s);
	/* don't care if it's finished or not */

	if (VSB_ISDYNAMIC(s))
		SBFREE(s->s_buf);
	isdyn = VSB_ISDYNSTRUCT(s);
	memset(s, 0, sizeof(*s));
	if (isdyn)
		SBFREE(s);
}

/*
 * Quote a string
 */
void
VSB_quote(struct vsb *s, const char *p, int len, int how)
{
	const char *q;
	int quote = 0;

	(void)how;	/* For future enhancements */
	if (len == -1)
		len = strlen(p);

	for (q = p; q < p + len; q++) {
		if (!isgraph(*q) || *q == '"' || *q == '\\') {
			quote++;
			break;
		}
	}
	if (!quote) {
		(void)VSB_bcat(s, p, len);
		return;
	}
	(void)VSB_putc(s, '"');
	for (q = p; q < p + len; q++) {
		switch (*q) {
		case ' ':
			(void)VSB_putc(s, *q);
			break;
		case '\\':
		case '"':
			(void)VSB_putc(s, '\\');
			(void)VSB_putc(s, *q);
			break;
		case '\n':
			(void)VSB_cat(s, "\\n");
			break;
		case '\r':
			(void)VSB_cat(s, "\\r");
			break;
		case '\t':
			(void)VSB_cat(s, "\\t");
			break;
		default:
			if (isgraph(*q))
				(void)VSB_putc(s, *q);
			else
				(void)VSB_printf(s, "\\%o", *q & 0xff);
			break;
		}
	}
	(void)VSB_putc(s, '"');
}
