/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)queue.h	8.5 (Berkeley) 8/20/94
 * $FreeBSD: head/sys/sys/queue.h 251887 2013-06-18 02:57:56Z lstewart $
 */

#ifndef VARNISH_QUEUE_H
#define	VARNISH_QUEUE_H

/*
 * This file defines four types of data structures: singly-linked lists,
 * singly-linked tail queues, lists and tail queues.
 *
 * A singly-linked list is headed by a single forward pointer. The elements
 * are singly linked for minimum space and pointer manipulation overhead at
 * the expense of O(n) removal for arbitrary elements. New elements can be
 * added to the list after an existing element or at the head of the list.
 * Elements being removed from the head of the list should use the explicit
 * macro for this purpose for optimum efficiency. A singly-linked list may
 * only be traversed in the forward direction.  Singly-linked lists are ideal
 * for applications with large datasets and few or no removals or for
 * implementing a LIFO queue.
 *
 * A singly-linked tail queue is headed by a pair of pointers, one to the
 * head of the list and the other to the tail of the list. The elements are
 * singly linked for minimum space and pointer manipulation overhead at the
 * expense of O(n) removal for arbitrary elements. New elements can be added
 * to the list after an existing element, at the head of the list, or at the
 * end of the list. Elements being removed from the head of the tail queue
 * should use the explicit macro for this purpose for optimum efficiency.
 * A singly-linked tail queue may only be traversed in the forward direction.
 * Singly-linked tail queues are ideal for applications with large datasets
 * and few or no removals or for implementing a FIFO queue.
 *
 * A list is headed by a single forward pointer (or an array of forward
 * pointers for a hash table header). The elements are doubly linked
 * so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before
 * or after an existing element or at the head of the list. A list
 * may be traversed in either direction.
 *
 * A tail queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or
 * after an existing element, at the head of the list, or at the end of
 * the list. A tail queue may be traversed in either direction.
 *
 * For details on the use of these macros, see the queue(3) manual page.
 *
 *
 *				VSLIST	VLIST	VSTAILQ	VTAILQ
 * _HEAD			+	+	+	+
 * _HEAD_INITIALIZER		+	+	+	+
 * _ENTRY			+	+	+	+
 * _INIT			+	+	+	+
 * _EMPTY			+	+	+	+
 * _FIRST			+	+	+	+
 * _NEXT			+	+	+	+
 * _PREV			-	+	-	+
 * _LAST			-	-	+	+
 * _FOREACH			+	+	+	+
 * _FOREACH_FROM		+	+	+	+
 * _FOREACH_SAFE		+	+	+	+
 * _FOREACH_FROM_SAFE		+	+	+	+
 * _FOREACH_REVERSE		-	-	-	+
 * _FOREACH_REVERSE_FROM	-	-	-	+
 * _FOREACH_REVERSE_SAFE	-	-	-	+
 * _FOREACH_REVERSE_FROM_SAFE	-	-	-	+
 * _INSERT_HEAD			+	+	+	+
 * _INSERT_BEFORE		-	+	-	+
 * _INSERT_AFTER		+	+	+	+
 * _INSERT_TAIL			-	-	+	+
 * _CONCAT			-	-	+	+
 * _REMOVE_AFTER		+	-	+	-
 * _REMOVE_HEAD			+	-	+	-
 * _REMOVE			+	+	+	+
 * _SWAP			+	+	+	+
 *
 */
#define	TRACEBUF
#define	TRACEBUF_INITIALIZER
#define	TRASHIT(x)

/*
 * Singly-linked List declarations.
 */
#define	VSLIST_HEAD(name, type)						\
struct name {								\
	struct type *vslh_first;	/* first element */		\
}

#define	VSLIST_HEAD_INITIALIZER(head)					\
	{ NULL }

#define	VSLIST_ENTRY(type)						\
struct {								\
	struct type *vsle_next;	/* next element */			\
}

/*
 * Singly-linked List functions.
 */
#define	VSLIST_EMPTY(head)	((head)->vslh_first == NULL)

#define	VSLIST_FIRST(head)	((head)->vslh_first)

#define	VSLIST_FOREACH(var, head, field)				\
	for ((var) = VSLIST_FIRST((head));				\
	    (var);							\
	    (var) = VSLIST_NEXT((var), field))

#define	VSLIST_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : VSLIST_FIRST((head)));		\
	    (var);							\
	    (var) = VSLIST_NEXT((var), field))

#define	VSLIST_FOREACH_SAFE(var, head, field, tvar)			\
	for ((var) = VSLIST_FIRST((head));				\
	    (var) && ((tvar) = VSLIST_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	VSLIST_FOREACH_FROM_SAFE(var, head, field, tvar)			\
	for ((var) = ((var) ? (var) : VSLIST_FIRST((head)));		\
	    (var) && ((tvar) = VSLIST_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	VSLIST_FOREACH_PREVPTR(var, varp, head, field)			\
	for ((varp) = &VSLIST_FIRST((head));				\
	    ((var) = *(varp)) != NULL;					\
	    (varp) = &VSLIST_NEXT((var), field))

#define	VSLIST_INIT(head) do {						\
	VSLIST_FIRST((head)) = NULL;					\
} while (0)

#define	VSLIST_INSERT_AFTER(slistelm, elm, field) do {			\
	VSLIST_NEXT((elm), field) = VSLIST_NEXT((slistelm), field);	\
	VSLIST_NEXT((slistelm), field) = (elm);				\
} while (0)

#define	VSLIST_INSERT_HEAD(head, elm, field) do {			\
	VSLIST_NEXT((elm), field) = VSLIST_FIRST((head));		\
	VSLIST_FIRST((head)) = (elm);					\
} while (0)

#define	VSLIST_NEXT(elm, field)	((elm)->field.vsle_next)

#define	VSLIST_REMOVE(head, elm, type, field) do {			\
	if (VSLIST_FIRST((head)) == (elm)) {				\
		VSLIST_REMOVE_HEAD((head), field);			\
	}								\
	else {								\
		struct type *curelm = VSLIST_FIRST((head));		\
		while (VSLIST_NEXT(curelm, field) != (elm))		\
			curelm = VSLIST_NEXT(curelm, field);		\
		VSLIST_REMOVE_AFTER(curelm, field);			\
	}								\
	TRASHIT(*oldnext);						\
} while (0)

#define VSLIST_REMOVE_AFTER(elm, field) do {				\
	VSLIST_NEXT(elm, field) =					\
	    VSLIST_NEXT(VSLIST_NEXT(elm, field), field);			\
} while (0)

#define	VSLIST_REMOVE_HEAD(head, field) do {				\
	VSLIST_FIRST((head)) = VSLIST_NEXT(VSLIST_FIRST((head)), field);\
} while (0)

#define VSLIST_SWAP(head1, head2, type) do {				\
	struct type *swap_first = VSLIST_FIRST(head1);			\
	VSLIST_FIRST(head1) = VSLIST_FIRST(head2);			\
	VSLIST_FIRST(head2) = swap_first;				\
} while (0)

/*
 * Singly-linked Tail queue declarations.
 */
#define	VSTAILQ_HEAD(name, type)					\
struct name {								\
	struct type *vstqh_first;/* first element */			\
	struct type **vstqh_last;/* addr of last next element */	\
}

#define	VSTAILQ_HEAD_INITIALIZER(head)					\
	{ NULL, &(head).vstqh_first }

#define	VSTAILQ_ENTRY(type)						\
struct {								\
	struct type *vstqe_next;	/* next element */		\
}

/*
 * Singly-linked Tail queue functions.
 */
#define	VSTAILQ_CONCAT(head1, head2) do {				\
	if (!VSTAILQ_EMPTY((head2))) {					\
		*(head1)->vstqh_last = (head2)->vstqh_first;		\
		(head1)->vstqh_last = (head2)->vstqh_last;		\
		VSTAILQ_INIT((head2));					\
	}								\
} while (0)

#define	VSTAILQ_EMPTY(head)	((head)->vstqh_first == NULL)

#define	VSTAILQ_FIRST(head)	((head)->vstqh_first)

#define	VSTAILQ_FOREACH(var, head, field)				\
	for((var) = VSTAILQ_FIRST((head));				\
	   (var);							\
	   (var) = VSTAILQ_NEXT((var), field))

#define	VSTAILQ_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : VSTAILQ_FIRST((head)));		\
	   (var);							\
	   (var) = VSTAILQ_NEXT((var), field))

#define	VSTAILQ_FOREACH_SAFE(var, head, field, tvar)			\
	for ((var) = VSTAILQ_FIRST((head));				\
	    (var) && ((tvar) = VSTAILQ_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	VSTAILQ_FOREACH_FROM_SAFE(var, head, field, tvar)		\
	for ((var) = ((var) ? (var) : VSTAILQ_FIRST((head)));		\
	    (var) && ((tvar) = VSTAILQ_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	VSTAILQ_INIT(head) do {						\
	VSTAILQ_FIRST((head)) = NULL;					\
	(head)->vstqh_last = &VSTAILQ_FIRST((head));			\
} while (0)

#define	VSTAILQ_INSERT_AFTER(head, tqelm, elm, field) do {		\
	if ((VSTAILQ_NEXT((elm), field) = VSTAILQ_NEXT((tqelm), field)) == NULL)\
		(head)->vstqh_last = &VSTAILQ_NEXT((elm), field);	\
	VSTAILQ_NEXT((tqelm), field) = (elm);				\
} while (0)

#define	VSTAILQ_INSERT_HEAD(head, elm, field) do {			\
	if ((VSTAILQ_NEXT((elm), field) = VSTAILQ_FIRST((head))) == NULL)\
		(head)->vstqh_last = &VSTAILQ_NEXT((elm), field);	\
	VSTAILQ_FIRST((head)) = (elm);					\
} while (0)

#define	VSTAILQ_INSERT_TAIL(head, elm, field) do {			\
	VSTAILQ_NEXT((elm), field) = NULL;				\
	*(head)->vstqh_last = (elm);					\
	(head)->vstqh_last = &VSTAILQ_NEXT((elm), field);		\
} while (0)

#define	VSTAILQ_LAST(head, type, field)					\
	(VSTAILQ_EMPTY((head)) ? NULL :					\
	    __containerof((head)->vstqh_last, struct type, field.vstqe_next))

#define	VSTAILQ_NEXT(elm, field)	((elm)->field.vstqe_next)

#define	VSTAILQ_REMOVE(head, elm, type, field) do {			\
	if (VSTAILQ_FIRST((head)) == (elm)) {				\
		VSTAILQ_REMOVE_HEAD((head), field);			\
	}								\
	else {								\
		struct type *curelm = VSTAILQ_FIRST((head));		\
		while (VSTAILQ_NEXT(curelm, field) != (elm))		\
			curelm = VSTAILQ_NEXT(curelm, field);		\
		VSTAILQ_REMOVE_AFTER(head, curelm, field);		\
	}								\
	TRASHIT(*oldnext);						\
} while (0)

#define VSTAILQ_REMOVE_AFTER(head, elm, field) do {			\
	if ((VSTAILQ_NEXT(elm, field) =					\
	     VSTAILQ_NEXT(VSTAILQ_NEXT(elm, field), field)) == NULL)	\
		(head)->vstqh_last = &VSTAILQ_NEXT((elm), field);		\
} while (0)

#define	VSTAILQ_REMOVE_HEAD(head, field) do {				\
	if ((VSTAILQ_FIRST((head)) =					\
	     VSTAILQ_NEXT(VSTAILQ_FIRST((head)), field)) == NULL)	\
		(head)->vstqh_last = &VSTAILQ_FIRST((head));		\
} while (0)

#define VSTAILQ_SWAP(head1, head2, type) do {				\
	struct type *swap_first = VSTAILQ_FIRST(head1);			\
	struct type **swap_last = (head1)->vstqh_last;			\
	VSTAILQ_FIRST(head1) = VSTAILQ_FIRST(head2);			\
	(head1)->vstqh_last = (head2)->vstqh_last;			\
	VSTAILQ_FIRST(head2) = swap_first;				\
	(head2)->vstqh_last = swap_last;					\
	if (VSTAILQ_EMPTY(head1))					\
		(head1)->vstqh_last = &VSTAILQ_FIRST(head1);		\
	if (VSTAILQ_EMPTY(head2))					\
		(head2)->vstqh_last = &VSTAILQ_FIRST(head2);		\
} while (0)


/*
 * List declarations.
 */
#define	VLIST_HEAD(name, type)						\
struct name {								\
	struct type *vlh_first;	/* first element */			\
}

#define	VLIST_HEAD_INITIALIZER(head)					\
	{ NULL }

#define	VLIST_ENTRY(type)						\
struct {								\
	struct type *vle_next;	/* next element */			\
	struct type **vle_prev;	/* address of previous next element */	\
}

/*
 * List functions.
 */


#define	VLIST_EMPTY(head)	((head)->vlh_first == NULL)

#define	VLIST_FIRST(head)	((head)->vlh_first)

#define	VLIST_FOREACH(var, head, field)					\
	for ((var) = VLIST_FIRST((head));				\
	    (var);							\
	    (var) = VLIST_NEXT((var), field))

#define	VLIST_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : VLIST_FIRST((head)));		\
	    (var);							\
	    (var) = VLIST_NEXT((var), field))

#define	VLIST_FOREACH_SAFE(var, head, field, tvar)			\
	for ((var) = VLIST_FIRST((head));				\
	    (var) && ((tvar) = VLIST_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	VLIST_FOREACH_FROM_SAFE(var, head, field, tvar)			\
	for ((var) = ((var) ? (var) : VLIST_FIRST((head)));		\
	    (var) && ((tvar) = VLIST_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	VLIST_INIT(head) do {						\
	VLIST_FIRST((head)) = NULL;					\
} while (0)

#define	VLIST_INSERT_AFTER(listelm, elm, field) do {			\
	if ((VLIST_NEXT((elm), field) = VLIST_NEXT((listelm), field)) != NULL)\
		VLIST_NEXT((listelm), field)->field.vle_prev =		\
		    &VLIST_NEXT((elm), field);				\
	VLIST_NEXT((listelm), field) = (elm);				\
	(elm)->field.vle_prev = &VLIST_NEXT((listelm), field);		\
} while (0)

#define	VLIST_INSERT_BEFORE(listelm, elm, field) do {			\
	(elm)->field.vle_prev = (listelm)->field.vle_prev;		\
	VLIST_NEXT((elm), field) = (listelm);				\
	*(listelm)->field.vle_prev = (elm);				\
	(listelm)->field.vle_prev = &VLIST_NEXT((elm), field);		\
} while (0)

#define	VLIST_INSERT_HEAD(head, elm, field) do {			\
	if ((VLIST_NEXT((elm), field) = VLIST_FIRST((head))) != NULL)	\
		VLIST_FIRST((head))->field.vle_prev = &VLIST_NEXT((elm), field);\
	VLIST_FIRST((head)) = (elm);					\
	(elm)->field.vle_prev = &VLIST_FIRST((head));			\
} while (0)

#define	VLIST_NEXT(elm, field)	((elm)->field.vle_next)

#define	VLIST_PREV(elm, head, type, field)				\
	((elm)->field.vle_prev == &VLIST_FIRST((head)) ? NULL :		\
	    __containerof((elm)->field.vle_prev, struct type, field.vle_next))

#define	VLIST_REMOVE(elm, field) do {					\
	if (VLIST_NEXT((elm), field) != NULL)				\
		VLIST_NEXT((elm), field)->field.vle_prev =		\
		    (elm)->field.vle_prev;				\
	*(elm)->field.vle_prev = VLIST_NEXT((elm), field);		\
	TRASHIT(*oldnext);						\
	TRASHIT(*oldprev);						\
} while (0)

#define VLIST_SWAP(head1, head2, type, field) do {			\
	struct type *swap_tmp = VLIST_FIRST((head1));			\
	VLIST_FIRST((head1)) = VLIST_FIRST((head2));			\
	VLIST_FIRST((head2)) = swap_tmp;					\
	if ((swap_tmp = VLIST_FIRST((head1))) != NULL)			\
		swap_tmp->field.vle_prev = &VLIST_FIRST((head1));		\
	if ((swap_tmp = VLIST_FIRST((head2))) != NULL)			\
		swap_tmp->field.vle_prev = &VLIST_FIRST((head2));		\
} while (0)

/*
 * Tail queue declarations.
 */
#define	VTAILQ_HEAD(name, type)						\
struct name {								\
	struct type *vtqh_first;	/* first element */		\
	struct type **vtqh_last;	/* addr of last next element */	\
	TRACEBUF							\
}

#define	VTAILQ_HEAD_INITIALIZER(head)					\
	{ NULL, &(head).vtqh_first, TRACEBUF_INITIALIZER }

#define	VTAILQ_ENTRY(type)						\
struct {								\
	struct type *vtqe_next;	/* next element */			\
	struct type **vtqe_prev;	/* address of previous next element */\
	TRACEBUF							\
} /*lint -e(18) */

/*
 * Tail queue functions.
 */

#define	VTAILQ_CONCAT(head1, head2, field) do {				\
	if (!VTAILQ_EMPTY(head2)) {					\
		*(head1)->vtqh_last = (head2)->vtqh_first;		\
		(head2)->vtqh_first->field.vtqe_prev = (head1)->vtqh_last;\
		(head1)->vtqh_last = (head2)->vtqh_last;		\
		VTAILQ_INIT((head2));					\
	}								\
} while (0)

#define	VTAILQ_EMPTY(head)	((head)->vtqh_first == NULL)

#define	VTAILQ_FIRST(head)	((head)->vtqh_first)

#define	VTAILQ_FOREACH(var, head, field)				\
	for ((var) = VTAILQ_FIRST((head));				\
	    (var);							\
	    (var) = VTAILQ_NEXT((var), field))

#define	VTAILQ_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : VTAILQ_FIRST((head)));		\
	    (var);							\
	    (var) = VTAILQ_NEXT((var), field))

#define	VTAILQ_FOREACH_SAFE(var, head, field, tvar)			\
	for ((var) = VTAILQ_FIRST((head));				\
	    (var) && ((tvar) = VTAILQ_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	VTAILQ_FOREACH_FROM_SAFE(var, head, field, tvar)			\
	for ((var) = ((var) ? (var) : VTAILQ_FIRST((head)));		\
	    (var) && ((tvar) = VTAILQ_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	VTAILQ_FOREACH_REVERSE(var, head, headname, field)		\
	for ((var) = VTAILQ_LAST((head), headname);			\
	    (var);							\
	    (var) = VTAILQ_PREV((var), headname, field))

#define	VTAILQ_FOREACH_REVERSE_FROM(var, head, headname, field)		\
	for ((var) = ((var) ? (var) : VTAILQ_LAST((head), headname));	\
	    (var);							\
	    (var) = VTAILQ_PREV((var), headname, field))

#define	VTAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar)	\
	for ((var) = VTAILQ_LAST((head), headname);			\
	    (var) && ((tvar) = VTAILQ_PREV((var), headname, field), 1);	\
	    (var) = (tvar))

#define	VTAILQ_FOREACH_REVERSE_FROM_SAFE(var, head, headname, field, tvar) \
	for ((var) = ((var) ? (var) : VTAILQ_LAST((head), headname));	\
	    (var) && ((tvar) = VTAILQ_PREV((var), headname, field), 1);	\
	    (var) = (tvar))

#define	VTAILQ_INIT(head) do {						\
	VTAILQ_FIRST((head)) = NULL;					\
	(head)->vtqh_last = &VTAILQ_FIRST((head));			\
} while (0)

#define	VTAILQ_INSERT_AFTER(head, listelm, elm, field) do {		\
	if ((VTAILQ_NEXT((elm), field) = VTAILQ_NEXT((listelm), field)) != NULL)\
		VTAILQ_NEXT((elm), field)->field.vtqe_prev =		\
		    &VTAILQ_NEXT((elm), field);				\
	else {								\
		(head)->vtqh_last = &VTAILQ_NEXT((elm), field);		\
	}								\
	VTAILQ_NEXT((listelm), field) = (elm);				\
	(elm)->field.vtqe_prev = &VTAILQ_NEXT((listelm), field);	\
} while (0)

#define	VTAILQ_INSERT_BEFORE(listelm, elm, field) do {			\
	(elm)->field.vtqe_prev = (listelm)->field.vtqe_prev;		\
	VTAILQ_NEXT((elm), field) = (listelm);				\
	*(listelm)->field.vtqe_prev = (elm);				\
	(listelm)->field.vtqe_prev = &VTAILQ_NEXT((elm), field);	\
} while (0)

#define	VTAILQ_INSERT_HEAD(head, elm, field) do {			\
	if ((VTAILQ_NEXT((elm), field) = VTAILQ_FIRST((head))) != NULL)	\
		VTAILQ_FIRST((head))->field.vtqe_prev =			\
		    &VTAILQ_NEXT((elm), field);				\
	else								\
		(head)->vtqh_last = &VTAILQ_NEXT((elm), field);		\
	VTAILQ_FIRST((head)) = (elm);					\
	(elm)->field.vtqe_prev = &VTAILQ_FIRST((head));			\
} while (0)

#define	VTAILQ_INSERT_TAIL(head, elm, field) do {			\
	VTAILQ_NEXT((elm), field) = NULL;				\
	(elm)->field.vtqe_prev = (head)->vtqh_last;			\
	*(head)->vtqh_last = (elm);					\
	(head)->vtqh_last = &VTAILQ_NEXT((elm), field);			\
} while (0)

#define	VTAILQ_LAST(head, headname)					\
	(*(((struct headname *)((head)->vtqh_last))->vtqh_last))

#define	VTAILQ_NEXT(elm, field) ((elm)->field.vtqe_next)

#define	VTAILQ_PREV(elm, headname, field)				\
	(*(((struct headname *)((elm)->field.vtqe_prev))->vtqh_last))

#define	VTAILQ_REMOVE(head, elm, field) do {				\
	if ((VTAILQ_NEXT((elm), field)) != NULL)			\
		VTAILQ_NEXT((elm), field)->field.vtqe_prev =		\
		    (elm)->field.vtqe_prev;				\
	else {								\
		(head)->vtqh_last = (elm)->field.vtqe_prev;		\
	}								\
	*(elm)->field.vtqe_prev = VTAILQ_NEXT((elm), field);		\
	TRASHIT(*oldnext);						\
	TRASHIT(*oldprev);						\
} while (0)

#define VTAILQ_SWAP(head1, head2, type, field) do {			\
	struct type *swap_first = (head1)->vtqh_first;			\
	struct type **swap_last = (head1)->vtqh_last;			\
	(head1)->vtqh_first = (head2)->vtqh_first;			\
	(head1)->vtqh_last = (head2)->vtqh_last;				\
	(head2)->vtqh_first = swap_first;				\
	(head2)->vtqh_last = swap_last;					\
	if ((swap_first = (head1)->vtqh_first) != NULL)			\
		swap_first->field.vtqe_prev = &(head1)->vtqh_first;	\
	else								\
		(head1)->vtqh_last = &(head1)->vtqh_first;		\
	if ((swap_first = (head2)->vtqh_first) != NULL)			\
		swap_first->field.vtqe_prev = &(head2)->vtqh_first;	\
	else								\
		(head2)->vtqh_last = &(head2)->vtqh_first;		\
} while (0)

#endif /* !VARNISH_QUEUE_H */
