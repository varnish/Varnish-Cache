/*-
 * Copyright (c) 2015 Varnish Software AS
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
 * PARAM(nm, ty, mi, ma, de, un, fl, st, lt, fn)
 */

/*lint -save -e525 -e539 */

PARAM(
	/* name */	accept_filter,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	MUST_RESTART,
	/* s-text */
	"Enable kernel accept-filters (if available in the kernel).",
	/* l-text */	NULL,
	/* func */	NULL
)

PARAM(
	/* name */	acceptor_sleep_decay,
	/* typ */	double,
	/* min */	"0",
	/* max */	"1",
	/* default */	"0.9",
	/* units */	NULL,
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"If we run out of resources, such as file descriptors or worker "
	"threads, the acceptor will sleep between accepts.\n"
	"This parameter (multiplicatively) reduce the sleep duration for "
	"each successful accept. (ie: 0.9 = reduce by 10%)",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	acceptor_sleep_incr,
	/* typ */	timeout,
	/* min */	"0",
	/* max */	"1",
	/* default */	"0",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"If we run out of resources, such as file descriptors or worker "
	"threads, the acceptor will sleep between accepts.\n"
	"This parameter control how much longer we sleep, each time we "
	"fail to accept a new connection.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	acceptor_sleep_max,
	/* typ */	timeout,
	/* min */	"0",
	/* max */	"10",
	/* default */	"0.05",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"If we run out of resources, such as file descriptors or worker "
	"threads, the acceptor will sleep between accepts.\n"
	"This parameter limits how long it can sleep between attempts to "
	"accept new connections.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	auto_restart,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Automatically restart the child/worker process if it dies.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	ban_dups,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Eliminate older identical bans when a new ban is added.  This saves "
	"CPU cycles by not comparing objects to identical bans.\n"
	"This is a waste of time if you have many bans which are never "
	"identical.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	ban_lurker_age,
	/* typ */	timeout,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"60",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"The ban lurker will ignore bans until they are this old.  "
	"When a ban is added, the active traffic will be tested against it "
	"as part of object lookup.  This parameter "
	"holds the ban-lurker off, until the rush is over.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	ban_lurker_batch,
	/* typ */	uint,
	/* min */	"1",
	/* max */	NULL,
	/* default */	"1000",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"The ban lurker sleeps ${ban_lurker_sleep} after examining this "
	"many objects."
	"  Use this to pace the ban-lurker if it eats too many resources.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	ban_lurker_sleep,
	/* typ */	timeout,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"0.010",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"How long the ban lurker sleeps after examining ${ban_lurker_batch} "
	"objects."
	"  Use this to pace the ban-lurker if it eats too many resources.\n"
	"A value of zero will disable the ban lurker entirely.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	first_byte_timeout,
	/* typ */	timeout,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"60",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"Default timeout for receiving first byte from backend. We only "
	"wait for this many seconds for the first byte before giving up. A "
	"value of 0 means it will never time out. VCL can override this "
	"default value for each backend and backend request. This "
	"parameter does not apply to pipe.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	between_bytes_timeout,
	/* typ */	timeout,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"60",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"We only wait for this many seconds between bytes received from "
	"the backend before giving up the fetch.\n"
	"A value of zero means never give up.\n"
	"VCL values, per backend or per backend request take precedence.\n"
	"This parameter does not apply to pipe'ed requests.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	backend_idle_timeout,
	/* typ */	timeout,
	/* min */	"1",
	/* max */	NULL,
	/* default */	"60",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"Timeout before we close unused backend connections.",
	/* l-text */	"",
	/* func */	NULL
)

/**********************************************************************/
#if 0 /* NOT YET */

PARAM(
	/* name */	cli_buffer,
	/* typ */	bytes_u,
	/* min */	"4k",
	/* max */	NULL,
	/* default */	"8k",
	/* units */	"bytes",
	/* flags */	0,
	/* s-text */
	"Size of buffer for CLI command input.\n"
	"You may need to increase this if you have big VCL files and use "
	"the vcl.inline CLI command.\n"
	"NB: Must be specified with -p to have effect.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	cli_limit,
	/* typ */	bytes_u,
	/* min */	"128b",
	/* max */	"99999999b",
	/* default */	"48k",
	/* units */	"bytes",
	/* flags */	0,
	/* s-text */
	"Maximum size of CLI response.  If the response exceeds this "
	"limit, the response code will be 201 instead of 200 and the last "
	"line will indicate the truncation.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	cli_timeout,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"60.000",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"Timeout for the childs replies to CLI requests from the "
	"mgt_param.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	clock_skew,
	/* typ */	uint,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"10",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"How much clockskew we are willing to accept between the backend "
	"and our own clock.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	connect_timeout,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"3.500",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"Default connection timeout for backend connections. We only try "
	"to connect to the backend for this many seconds before giving up. "
	"VCL can override this default value for each backend and backend "
	"request.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	connect_bindany,
	/* tweak */	tweak_bool,
	/* var */	connect_bindany,
	/* min */	none,
	/* max */	none,
	/* default */	off,
	/* units */	bool,
	/* flags */	00,
	/* s-text */
	"Bind any before connect: move the 64k local ports limit"
	"to 64k connections per destination.\n",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	critbit_cooloff,
	/* typ */	timeout,
	/* min */	"60.000",
	/* max */	"254.000",
	/* default */	"180.000",
	/* units */	"seconds",
	/* flags */	WIZARD,
	/* s-text */
	"How long the critbit hasher keeps deleted objheads on the cooloff "
	"list.",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	debug,
	/* typ */	debug,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	NULL,
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Enable/Disable various kinds of debugging.\n"
	"	none	Disable all debugging\n"
	"\n"
	"Use +/- prefix to set/reset individual bits:\n"
	"	req_state	VSL Request state engine\n"
	"	workspace	VSL Workspace operations\n"
	"	waiter	VSL Waiter internals\n"
	"	waitinglist	VSL Waitinglist events\n"
	"	syncvsl	Make VSL synchronous\n"
	"	hashedge	Edge cases in Hash\n"
	"	vclrel	Rapid VCL release\n"
	"	lurker	VSL Ban lurker\n"
	"	esi_chop	Chop ESI fetch to bits\n"
	"	flush_head	Flush after http1 head\n"
	"	vtc_mode	Varnishtest Mode",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	default_grace,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"10.000",
	/* units */	"seconds",
	/* flags */	OBJ_STICKY,
	/* s-text */
	"Default grace period.  We will deliver an object this long after "
	"it has expired, provided another thread is attempting to get a "
	"new copy.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	default_keep,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"0.000",
	/* units */	"seconds",
	/* flags */	OBJ_STICKY,
	/* s-text */
	"Default keep period.  We will keep a useless object around this "
	"long, making it available for conditional backend fetches.  That "
	"means that the object will be removed from the cache at the end "
	"of ttl+grace+keep.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	default_ttl,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"120.000",
	/* units */	"seconds",
	/* flags */	OBJ_STICKY,
	/* s-text */
	"The TTL assigned to objects if neither the backend nor the VCL "
	"code assigns one.",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	feature,
	/* typ */	feature,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	NULL,
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Enable/Disable various minor features.\n"
	"	none	Disable all features.\n"
	"\n"
	"Use +/- prefix to enable/disable individual feature:\n"
	"	short_panic	Short panic message.\n"
	"	wait_silo	Wait for persistent silo.\n"
	"	no_coredump	No coredumps.\n"
	"	esi_ignore_https	Treat HTTPS as HTTP in ESI:includes\n"
	"	esi_disable_xml_check	Don't check of body looks like XML\n"
	"	esi_ignore_other_elements	Ignore non-esi XML-elements\n"
	"	esi_remove_bom	Remove UTF-8 BOM",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	fetch_chunksize,
	/* typ */	bytes,
	/* min */	"4k",
	/* max */	NULL,
	/* default */	"16k",
	/* units */	"bytes",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"The default chunksize used by fetcher. This should be bigger than "
	"the majority of objects with short TTLs.\n"
	"Internal limits in the storage_file module makes increases above "
	"128kb a dubious idea.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	fetch_maxchunksize,
	/* typ */	bytes,
	/* min */	"64k",
	/* max */	NULL,
	/* default */	"0.25G",
	/* units */	"bytes",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"The maximum chunksize we attempt to allocate from storage. Making "
	"this too large may cause delays and storage fragmentation.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	gzip_buffer,
	/* typ */	bytes_u,
	/* min */	"2k",
	/* max */	NULL,
	/* default */	"32k",
	/* units */	"bytes",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"Size of malloc buffer used for gzip processing.\n"
	"These buffers are used for in-transit data, for instance "
	"gunzip'ed data being sent to a client.Making this space to small "
	"results in more overhead, writes to sockets etc, making it too "
	"big is probably just a waste of memory.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	gzip_level,
	/* typ */	uint,
	/* min */	"0",
	/* max */	"9",
	/* default */	"6",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Gzip compression level: 0=debug, 1=fast, 9=best",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	gzip_memlevel,
	/* typ */	uint,
	/* min */	"1",
	/* max */	"9",
	/* default */	"8",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Gzip memory level 1=slow/least, 9=fast/most compression.\n"
	"Memory impact is 1=1k, 2=2k, ... 9=256k.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_gzip_support,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Enable gzip support. When enabled Varnish request compressed "
	"objects from the backend and store them compressed. If a client "
	"does not support gzip encoding Varnish will uncompress compressed "
	"objects on demand. Varnish will also rewrite the Accept-Encoding "
	"header of clients indicating support for gzip to:\n"
	"  Accept-Encoding: gzip\n"
	"\n"
	"Clients that do not support gzip will have their Accept-Encoding "
	"header removed. For more information on how gzip is implemented "
	"please see the chapter on gzip in the Varnish reference.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_max_hdr,
	/* typ */	uint,
	/* min */	"32",
	/* max */	"65535",
	/* default */	"64",
	/* units */	"header lines",
	/* flags */	0,
	/* s-text */
	"Maximum number of HTTP header lines we allow in "
	"{req|resp|bereq|beresp}.http (obj.http is autosized to the exact "
	"number of headers).\n"
	"Cheap, ~20 bytes, in terms of workspace memory.\n"
	"Note that the first line occupies five header lines.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_range_support,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Enable support for HTTP Range headers.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_req_hdr_len,
	/* typ */	bytes_u,
	/* min */	"40b",
	/* max */	NULL,
	/* default */	"8k",
	/* units */	"bytes",
	/* flags */	0,
	/* s-text */
	"Maximum length of any HTTP client request header we will allow.  "
	"The limit is inclusive its continuation lines.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_req_size,
	/* typ */	bytes_u,
	/* min */	"0.25k",
	/* max */	NULL,
	/* default */	"32k",
	/* units */	"bytes",
	/* flags */	0,
	/* s-text */
	"Maximum number of bytes of HTTP client request we will deal with. "
	" This is a limit on all bytes up to the double blank line which "
	"ends the HTTP request.\n"
	"The memory for the request is allocated from the client workspace "
	"(param: workspace_client) and this parameter limits how much of "
	"that the request is allowed to take up.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_resp_hdr_len,
	/* typ */	bytes_u,
	/* min */	"40b",
	/* max */	NULL,
	/* default */	"8k",
	/* units */	"bytes",
	/* flags */	0,
	/* s-text */
	"Maximum length of any HTTP backend response header we will allow. "
	" The limit is inclusive its continuation lines.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	http_resp_size,
	/* typ */	bytes_u,
	/* min */	"0.25k",
	/* max */	NULL,
	/* default */	"32k",
	/* units */	"bytes",
	/* flags */	0,
	/* s-text */
	"Maximum number of bytes of HTTP backend response we will deal "
	"with.  This is a limit on all bytes up to the double blank line "
	"which ends the HTTP request.\n"
	"The memory for the request is allocated from the worker workspace "
	"(param: thread_pool_workspace) and this parameter limits how much "
	"of that the request is allowed to take up.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	idle_send_timeout,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"60.000",
	/* units */	"seconds",
	/* flags */	DELAYED_EFFECT,
	/* s-text */
	"Time to wait with no data sent. If no data has been transmitted "
	"in this many\n"
	"seconds the session is closed.\n"
	"See setsockopt(2) under SO_SNDTIMEO for more information.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	listen_depth,
	/* typ */	uint,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"1024",
	/* units */	"connections",
	/* flags */	MUST_RESTART,
	/* s-text */
	"Listen queue depth.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	lru_interval,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"2.000",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"Grace period before object moves on LRU list.\n"
	"Objects are only moved to the front of the LRU list if they have "
	"not been moved there already inside this timeout period.  This "
	"reduces the amount of lock operations necessary for LRU list "
	"access.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	max_esi_depth,
	/* typ */	uint,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"5",
	/* units */	"levels",
	/* flags */	0,
	/* s-text */
	"Maximum depth of esi:include processing.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	max_restarts,
	/* typ */	uint,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"4",
	/* units */	"restarts",
	/* flags */	0,
	/* s-text */
	"Upper limit on how many times a request can restart.\n"
	"Be aware that restarts are likely to cause a hit against the "
	"backend, so don't increase thoughtlessly.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	max_retries,
	/* typ */	uint,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"4",
	/* units */	"retries",
	/* flags */	0,
	/* s-text */
	"Upper limit on how many times a backend fetch can retry.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	nuke_limit,
	/* typ */	uint,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"50",
	/* units */	"allocations",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"Maximum number of objects we attempt to nuke in order to make "
	"space for a object body.",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	pcre_match_limit,
	/* typ */	uint,
	/* min */	"1",
	/* max */	NULL,
	/* default */	"1.000",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"The limit for the  number of internal matching function calls in "
	"a pcre_exec() execution.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	pcre_match_limit_recursion,
	/* typ */	uint,
	/* min */	"1",
	/* max */	NULL,
	/* default */	"1.000",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"The limit for the  number of internal matching function "
	"recursions in a pcre_exec() execution.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	ping_interval,
	/* typ */	uint,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"3",
	/* units */	"seconds",
	/* flags */	MUST_RESTART,
	/* s-text */
	"Interval between pings from parent to child.\n"
	"Zero will disable pinging entirely, which makes it possible to "
	"attach a debugger to the child.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	pipe_timeout,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"60.000",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"Idle timeout for PIPE sessions. If nothing have been received in "
	"either direction for this many seconds, the session is closed.",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	pool_req,
	/* typ */	poolparam,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"10,100,10",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Parameters for per worker pool request memory pool.\n"
	MEMPOOL_TEXT,
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	pool_sess,
	/* typ */	poolparam,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"10,100,10",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Parameters for per worker pool session memory pool.\n"
	MEMPOOL_TEXT,
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	pool_vbo,
	/* typ */	poolparam,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"10,100,10",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Parameters for backend object fetch memory pool.\n"
	MEMPOOL_TEXT,
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	prefer_ipv6,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"off",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Prefer IPv6 address when connecting to backends which have both "
	"IPv4 and IPv6 addresses.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	rush_exponent,
	/* typ */	uint,
	/* min */	"2",
	/* max */	NULL,
	/* default */	"3",
	/* units */	"requests per request",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"How many parked request we start for each completed request on "
	"the object.\n"
	"NB: Even with the implict delay of delivery, this parameter "
	"controls an exponential increase in number of worker threads.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	send_timeout,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"600.000",
	/* units */	"seconds",
	/* flags */	DELAYED_EFFECT,
	/* s-text */
	"Send timeout for client connections. If the HTTP response hasn't "
	"been transmitted in this many\n"
	"seconds the session is closed.\n"
	"See setsockopt(2) under SO_SNDTIMEO for more information.",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	session_max,
	/* typ */	uint,
	/* min */	"1000",
	/* max */	NULL,
	/* default */	"10.000",
	/* units */	"sessions",
	/* flags */	0,
	/* s-text */
	"Maximum number of sessions we will allocate from one pool before "
	"just dropping connections.\n"
	"This is mostly an anti-DoS measure, and setting it plenty high "
	"should not hurt, as long as you have the memory for it.",
	/* l-text */	"",
	/* func */	NULL
)

PARAM(
	/* name */	shm_reclen,
	/* typ */	vsl_reclen,
	/* min */	"16b",
	/* max */	"4084",
	/* default */	"255b",
	/* units */	"bytes",
	/* flags */	0,
	/* s-text */
	"Old name for vsl_reclen, use that instead.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	shortlived,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"10.000",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"Objects created with (ttl+grace+keep) shorter than this are "
	"always put in transient storage.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	sigsegv_handler,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	MUST_RESTART,
	/* s-text */
	"Install a signal handler which tries to dump debug information on "
	"segmentation faults, bus errors and abort signals.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	syslog_cli_traffic,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Log all CLI traffic to syslog(LOG_INFO).",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	tcp_keepalive_intvl,
	/* typ */	timeout,
	/* min */	"1.000",
	/* max */	"100.000",
	/* default */	"5.000",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"The number of seconds between TCP keep-alive probes.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	tcp_keepalive_probes,
	/* typ */	uint,
	/* min */	"1",
	/* max */	"100",
	/* default */	"5",
	/* units */	"probes",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"The maximum number of TCP keep-alive probes to send before giving "
	"up and killing the connection if no response is obtained from the "
	"other end.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	tcp_keepalive_time,
	/* typ */	timeout,
	/* min */	"1.000",
	/* max */	"7200.000",
	/* default */	"600.000",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"The number of seconds a connection needs to be idle before TCP "
	"begins sending out keep-alive probes.",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	thread_pool_add_delay,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"0.000",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"Wait at least this long after creating a thread.\n"
	"\n"
	"Some (buggy) systems may need a short (sub-second) delay between "
	"creating threads.\n"
	"Set this to a few milliseconds if you see the 'threads_failed' "
	"counter grow too much.\n"
	"Setting this too high results in insuffient worker threads.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_destroy_delay,
	/* typ */	timeout,
	/* min */	"0.010",
	/* max */	NULL,
	/* default */	"1.000",
	/* units */	"seconds",
	/* flags */	DELAYED_EFFECT| EXPERIMENTAL,
	/* s-text */
	"Wait this long after destroying a thread.\n"
	"This controls the decay of thread pools when idle(-ish).",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_fail_delay,
	/* typ */	timeout,
	/* min */	"0.010",
	/* max */	NULL,
	/* default */	"0.200",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"Wait at least this long after a failed thread creation before "
	"trying to create another thread.\n"
	"\n"
	"Failure to create a worker thread is often a sign that  the end "
	"is near, because the process is running out of some resource.  "
	"This delay tries to not rush the end on needlessly.\n"
	"\n"
	"If thread creation failures are a problem, check that "
	"thread_pool_max is not too high.\n"
	"\n"
	"It may also help to increase thread_pool_timeout and "
	"thread_pool_min, to reduce the rate at which treads are destroyed "
	"and later recreated.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_max,
	/* typ */	thread_pool_max,
	/* min */	"100",
	/* max */	NULL,
	/* default */	"5000",
	/* units */	"threads",
	/* flags */	DELAYED_EFFECT,
	/* s-text */
	"The maximum number of worker threads in each pool.\n"
	"\n"
	"Do not set this higher than you have to, since excess worker "
	"threads soak up RAM and CPU and generally just get in the way of "
	"getting work done.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_min,
	/* typ */	thread_pool_min,
	/* min */	NULL,
	/* max */	"5000",
	/* default */	"100",
	/* units */	"threads",
	/* flags */	DELAYED_EFFECT,
	/* s-text */
	"The minimum number of worker threads in each pool.\n"
	"\n"
	"Increasing this may help ramp up faster from low load situations "
	"or when threads have expired."
	"Minimum is 10 threads.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_stack,
	/* typ */	bytes,
	/* min */	"2k",
	/* max */	NULL,
	/* default */	"48k",
	/* units */	"bytes",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"Worker thread stack size.\n"
	"This will likely be rounded up to a multiple of 4k (or whatever "
	"the page_size might be) by the kernel.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pool_timeout,
	/* typ */	timeout,
	/* min */	"10.000",
	/* max */	NULL,
	/* default */	"300.000",
	/* units */	"seconds",
	/* flags */	DELAYED_EFFECT| EXPERIMENTAL,
	/* s-text */
	"Thread idle threshold.\n"
	"\n"
	"Threads in excess of thread_pool_min, which have been idle for at "
	"least this long, will be destroyed.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_pools,
	/* typ */	uint,
	/* min */	"1",
	/* max */	NULL,
	/* default */	"2",
	/* units */	"pools",
	/* flags */	DELAYED_EFFECT| EXPERIMENTAL,
	/* s-text */
	"Number of worker thread pools.\n"
	"\n"
	"Increasing number of worker pools decreases lock contention.\n"
	"\n"
	"Too many pools waste CPU and RAM resources, and more than one "
	"pool for each CPU is probably detrimal to performance.\n"
	"\n"
	"Can be increased on the fly, but decreases require a restart to "
	"take effect.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_queue_limit,
	/* typ */	uint,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"20",
	/* units */	NULL,
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"Permitted queue length per thread-pool.\n"
	"\n"
	"This sets the number of requests we will queue, waiting for an "
	"available thread.  Above this limit sessions will be dropped "
	"instead of queued.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	thread_stats_rate,
	/* typ */	uint,
	/* min */	"0",
	/* max */	NULL,
	/* default */	"10",
	/* units */	"requests",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"Worker threads accumulate statistics, and dump these into the "
	"global stats counters if the lock is free when they finish a job "
	"(request/fetch etc.)\n"
	"This parameters defines the maximum number of jobs a worker "
	"thread may handle, before it is forced to dump its accumulated "
	"stats into the global counters.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	timeout_idle,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"5.000",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"Idle timeout for client connections.\n"
	"A connection is considered idle, until we have received the full "
	"request headers.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	timeout_linger,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"0.050",
	/* units */	"seconds",
	/* flags */	EXPERIMENTAL,
	/* s-text */
	"How long the worker thread lingers on an idle session before "
	"handing it over to the waiter.\n"
	"When sessions are reused, as much as half of all reuses happen "
	"within the first 100 msec of the previous request completing.\n"
	"Setting this too high results in worker threads not doing "
	"anything for their keep, setting it too low just means that more "
	"sessions take a detour around the waiter.",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	vcc_allow_inline_c,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"off",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Allow inline C code in VCL.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
#if 0
PARAM(
	/* name */	vcc_err_unref,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Unreferenced VCL objects result in error.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
#if 0
PARAM(
	/* name */	vcc_unsafe_path,
	/* typ */	bool,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"on",
	/* units */	"bool",
	/* flags */	0,
	/* s-text */
	"Allow '/' in vmod & include paths.\n"
	"Allow 'import ... from ...'.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	vcl_cooldown,
	/* typ */	timeout,
	/* min */	"0.000",
	/* max */	NULL,
	/* default */	"600.000",
	/* units */	"seconds",
	/* flags */	0,
	/* s-text */
	"How long time a VCL is kept warm after being replaced as the "
	"active VCL.  (Granularity approximately 30 seconds.)",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	vcl_dir,
	/* typ */	string,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	/opt/varnish/etc/varnish,
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Directory from which relative VCL filenames (vcl.load and "
	"include) are opened.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	vmod_dir,
	/* typ */	string,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	/opt/varnish/lib/varnish/vmods,
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Directory where VCL modules are to be found.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	vsl_buffer,
	/* typ */	vsl_buffer,
	/* min */	"267",
	/* max */	NULL,
	/* default */	"4k",
	/* units */	"bytes",
	/* flags */	0,
	/* s-text */
	"Bytes of (req-/backend-)workspace dedicated to buffering VSL "
	"records.\n"
	"Setting this too high costs memory, setting it too low will cause "
	"more VSL flushes and likely increase lock-contention on the VSL "
	"mutex.\n"
	"The minimum tracks the vsl_reclen parameter + 12 bytes.",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	vsl_mask,
	/* typ */	vsl_mask,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	"default",
	/* units */	NULL,
	/* flags */	0,
	/* s-text */
	"Mask individual VSL messages from being logged.\n"
	"	default	Set default value\n"
	"\n"
	"Use +/- prefixe in front of VSL tag name, to mask/unmask "
	"individual VSL messages.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	vsl_reclen,
	/* typ */	vsl_reclen,
	/* min */	"16b",
	/* max */	"4084b",
	/* default */	"255b",
	/* units */	"bytes",
	/* flags */	0,
	/* s-text */
	"Maximum number of bytes in SHM log record.\n"
	"The maximum tracks the vsl_buffer parameter - 12 bytes.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	vsl_space,
	/* typ */	bytes,
	/* min */	"1M",
	/* max */	NULL,
	/* default */	"80M",
	/* units */	"bytes",
	/* flags */	MUST_RESTART,
	/* s-text */
	"The amount of space to allocate for the VSL fifo buffer in the "
	"VSM memory segment.  If you make this too small, "
	"varnish{ncsa|log} etc will not be able to keep up.  Making it too "
	"large just costs memory resources.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	vsm_space,
	/* typ */	bytes,
	/* min */	"1M",
	/* max */	NULL,
	/* default */	"1M",
	/* units */	"bytes",
	/* flags */	MUST_RESTART,
	/* s-text */
	"The amount of space to allocate for stats counters in the VSM "
	"memory segment.  If you make this too small, some counters will "
	"be invisible.  Making it too large just costs memory resources.",
	/* l-text */	"",
	/* func */	NULL
)
#if 0
PARAM(
	/* name */	waiter,
	/* typ */	waiter,
	/* min */	NULL,
	/* max */	NULL,
	/* default */	kqueue (possible values: kqueue, poll),
	/* units */	NULL,
	/* flags */	MUST_RESTART| WIZARD,
	/* s-text */
	"Select the waiter kernel interface.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
PARAM(
	/* name */	workspace_backend,
	/* typ */	bytes_u,
	/* min */	"1k",
	/* max */	NULL,
	/* default */	"64k",
	/* units */	"bytes",
	/* flags */	DELAYED_EFFECT,
	/* s-text */
	"Bytes of HTTP protocol workspace for backend HTTP req/resp.  If "
	"larger than 4k, use a multiple of 4k for VM efficiency.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	workspace_client,
	/* typ */	bytes_u,
	/* min */	"9k",
	/* max */	NULL,
	/* default */	"64k",
	/* units */	"bytes",
	/* flags */	DELAYED_EFFECT,
	/* s-text */
	"Bytes of HTTP protocol workspace for clients HTTP req/resp.  If "
	"larger than 4k, use a multiple of 4k for VM efficiency.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	workspace_session,
	/* typ */	bytes_u,
	/* min */	"0.25k",
	/* max */	NULL,
	/* default */	"0.50k",
	/* units */	"bytes",
	/* flags */	DELAYED_EFFECT,
	/* s-text */
	"Allocation size for session structure and workspace.    The "
	"workspace is primarily used for TCP connection addresses.  If "
	"larger than 4k, use a multiple of 4k for VM efficiency.",
	/* l-text */	"",
	/* func */	NULL
)
PARAM(
	/* name */	workspace_thread,
	/* typ */	bytes_u,
	/* min */	"0.25k",
	/* max */	"8k",
	/* default */	"2k",
	/* units */	"bytes",
	/* flags */	DELAYED_EFFECT,
	/* s-text */
	"Bytes of auxiliary workspace per thread.\n"
	"This workspace is used for certain temporary data structures "
	"during the operation of a worker thread.\n"
	"One use is for the io-vectors for writing requests and responses "
	"to sockets, having too little space will result in more writev(2) "
	"system calls, having too much just wastes the space.",
	/* l-text */	"",
	/* func */	NULL
)
#endif
/*lint -restore */
