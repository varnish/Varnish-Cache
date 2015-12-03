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
 */

#include "config.h"

#include <stdio.h>

#include "mgt/mgt.h"

#include "mgt/mgt_param.h"


#define MEMPOOL_TEXT							\
	"The three numbers are:\n"					\
	"\tmin_pool\tminimum size of free pool.\n"			\
	"\tmax_pool\tmaximum size of free pool.\n"			\
	"\tmax_age\tmax age of free element."

struct parspec mgt_parspec[] = {
#define PARAM(nm, ty, mi, ma, de, un, fl, st, lt, fn)		\
	{ #nm, tweak_##ty, &mgt_param.nm, mi, ma, st, fl, de, un },
#include "tbl/params.h"
#undef PARAM

	{ "default_ttl", tweak_timeout, &mgt_param.default_ttl,
		"0", NULL,
		"The TTL assigned to objects if neither the backend nor "
		"the VCL code assigns one.",
		OBJ_STICKY,
		"120", "seconds" },
	{ "default_grace", tweak_timeout, &mgt_param.default_grace,
		"0", NULL,
		"Default grace period.  We will deliver an object "
		"this long after it has expired, provided another thread "
		"is attempting to get a new copy.",
		OBJ_STICKY,
		"10", "seconds" },
	{ "default_keep", tweak_timeout, &mgt_param.default_keep,
		"0", NULL,
		"Default keep period.  We will keep a useless object "
		"around this long, making it available for conditional "
		"backend fetches.  "
		"That means that the object will be removed from the "
		"cache at the end of ttl+grace+keep.",
		OBJ_STICKY,
		"0", "seconds" },
	{ "workspace_session",
		tweak_bytes_u, &mgt_param.workspace_session,
		"256", NULL,
		"Allocation size for session structure and workspace.  "
		"  The workspace is primarily used for TCP connection "
		"addresses."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"512", "bytes" },
	{ "workspace_client",
		tweak_bytes_u, &mgt_param.workspace_client,
		"9k", NULL,
		"Bytes of HTTP protocol workspace for clients HTTP req/resp."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"64k", "bytes" },
	{ "workspace_backend",
		tweak_bytes_u, &mgt_param.workspace_backend,
		"1024", NULL,
		"Bytes of HTTP protocol workspace for backend HTTP req/resp."
		"  If larger than 4k, use a multiple of 4k for VM efficiency.",
		DELAYED_EFFECT,
		"64k", "bytes" },
	{ "workspace_thread",
		tweak_bytes_u, &mgt_param.workspace_thread,
		"256", "8192",
		"Bytes of auxiliary workspace per thread.\n"
		"This workspace is used for certain temporary data structures"
		" during the operation of a worker thread.\n"
		"One use is for the io-vectors for writing requests and"
		" responses to sockets, having too little space will"
		" result in more writev(2) system calls, having too much"
		" just wastes the space.",
		DELAYED_EFFECT,
		"2048", "bytes" },
	{ "http_req_hdr_len",
		tweak_bytes_u, &mgt_param.http_req_hdr_len,
		"40", NULL,
		"Maximum length of any HTTP client request header we will "
		"allow.  The limit is inclusive its continuation lines.",
		0,
		"8k", "bytes" },
	{ "http_req_size",
		tweak_bytes_u, &mgt_param.http_req_size,
		"256", NULL,
		"Maximum number of bytes of HTTP client request we will deal "
		"with.  This is a limit on all bytes up to the double blank "
		"line which ends the HTTP request.\n"
		"The memory for the request is allocated from the client "
		"workspace (param: workspace_client) and this parameter limits "
		"how much of that the request is allowed to take up.",
		0,
		"32k", "bytes" },
	{ "http_resp_hdr_len",
		tweak_bytes_u, &mgt_param.http_resp_hdr_len,
		"40", NULL,
		"Maximum length of any HTTP backend response header we will "
		"allow.  The limit is inclusive its continuation lines.",
		0,
		"8k", "bytes" },
	{ "http_resp_size",
		tweak_bytes_u, &mgt_param.http_resp_size,
		"256", NULL,
		"Maximum number of bytes of HTTP backend response we will deal "
		"with.  This is a limit on all bytes up to the double blank "
		"line which ends the HTTP request.\n"
		"The memory for the request is allocated from the worker "
		"workspace (param: thread_pool_workspace) and this parameter "
		"limits how much of that the request is allowed to take up.",
		0,
		"32k", "bytes" },
	{ "http_max_hdr", tweak_uint, &mgt_param.http_max_hdr,
		"32", "65535",
		"Maximum number of HTTP header lines we allow in "
		"{req|resp|bereq|beresp}.http "
		"(obj.http is autosized to the exact number of headers).\n"
		"Cheap, ~20 bytes, in terms of workspace memory.\n"
		"Note that the first line occupies five header lines.",
		0,
		"64", "header lines" },
	{ "vsl_buffer",
		tweak_vsl_buffer, &mgt_param.vsl_buffer,
		"1024", NULL,
		"Bytes of (req-/backend-)workspace dedicated to buffering"
		" VSL records.\n"
		"Setting this too high costs memory, setting it too low"
		" will cause more VSL flushes and likely increase"
		" lock-contention on the VSL mutex.\n\n"
		"The minimum tracks the vsl_reclen parameter + 12 bytes.",
		0,
		"4k", "bytes" },
	{ "vsl_reclen",
		tweak_vsl_reclen, &mgt_param.vsl_reclen,
		"16", "65535",
		"Maximum number of bytes in SHM log record.\n\n"
		"The maximum tracks the vsl_buffer parameter - 12 bytes.",
		0,
		"255", "bytes" },
	{ "shm_reclen",
		tweak_vsl_reclen, &mgt_param.vsl_reclen,
		"16", "65535",
		"Old name for vsl_reclen, use that instead.",
		0,
		"255", "bytes" },
	{ "timeout_idle", tweak_timeout, &mgt_param.timeout_idle,
		"0", NULL,
		"Idle timeout for client connections.\n"
		"A connection is considered idle, until we have "
		"received the full request headers.",
		0,
		"5", "seconds" },
	{ "pipe_timeout", tweak_timeout, &mgt_param.pipe_timeout,
		"0", NULL,
		"Idle timeout for PIPE sessions. "
		"If nothing have been received in either direction for "
		"this many seconds, the session is closed.",
		0,
		"60", "seconds" },
	{ "send_timeout", tweak_timeout, &mgt_param.send_timeout,
		"0", NULL,
		"Send timeout for client connections. "
		"If the HTTP response hasn't been transmitted in this many\n"
		"seconds the session is closed.\n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"600", "seconds" },
	{ "idle_send_timeout", tweak_timeout, &mgt_param.idle_send_timeout,
		"0", NULL,
		"Time to wait with no data sent. "
		"If no data has been transmitted in this many\n"
		"seconds the session is closed.\n"
		"See setsockopt(2) under SO_SNDTIMEO for more information.",
		DELAYED_EFFECT,
		"60", "seconds" },
	{ "nuke_limit",
		tweak_uint, &mgt_param.nuke_limit,
		"0", NULL,
		"Maximum number of objects we attempt to nuke in order"
		"to make space for a object body.",
		EXPERIMENTAL,
		"50", "allocations" },
	{ "fetch_chunksize",
		tweak_bytes, &mgt_param.fetch_chunksize,
		"4096", NULL,
		"The default chunksize used by fetcher. "
		"This should be bigger than the majority of objects with "
		"short TTLs.\n"
		"Internal limits in the storage_file module makes increases "
		"above 128kb a dubious idea.",
		EXPERIMENTAL,
		"16k", "bytes" },
	{ "fetch_maxchunksize",
		tweak_bytes, &mgt_param.fetch_maxchunksize,
		"65536", NULL,
		"The maximum chunksize we attempt to allocate from storage. "
		"Making this too large may cause delays and storage "
		"fragmentation.",
		EXPERIMENTAL,
		"256m", "bytes" },
	{ "listen_depth", tweak_uint, &mgt_param.listen_depth,
		"0", NULL,
		"Listen queue depth.",
		MUST_RESTART,
		"1024", "connections" },
	{ "cli_buffer",
		tweak_bytes_u, &mgt_param.cli_buffer,
		"4096", NULL,
		"Size of buffer for CLI command input."
		"\nYou may need to increase this if you have big VCL files "
		"and use the vcl.inline CLI command.\n"
		"NB: Must be specified with -p to have effect.",
		0,
		"8k", "bytes" },
	{ "cli_limit",
		tweak_bytes_u, &mgt_param.cli_limit,
		"128", "99999999",
		"Maximum size of CLI response.  If the response exceeds"
		" this limit, the response code will be 201 instead of"
		" 200 and the last line will indicate the truncation.",
		0,
		"48k", "bytes" },
	{ "cli_timeout", tweak_timeout, &mgt_param.cli_timeout,
		"0", NULL,
		"Timeout for the childs replies to CLI requests from "
		"the mgt_param.",
		0,
		"60", "seconds" },
	{ "ping_interval", tweak_uint, &mgt_param.ping_interval,
		"0", NULL,
		"Interval between pings from parent to child.\n"
		"Zero will disable pinging entirely, which makes "
		"it possible to attach a debugger to the child.",
		MUST_RESTART,
		"3", "seconds" },
	{ "lru_interval", tweak_timeout, &mgt_param.lru_interval,
		"0", NULL,
		"Grace period before object moves on LRU list.\n"
		"Objects are only moved to the front of the LRU "
		"list if they have not been moved there already inside "
		"this timeout period.  This reduces the amount of lock "
		"operations necessary for LRU list access.",
		EXPERIMENTAL,
		"2", "seconds" },
	{ "cc_command", tweak_string, &mgt_cc_cmd,
		NULL, NULL,
		"Command used for compiling the C source code to a "
		"dlopen(3) loadable object.  Any occurrence of %s in "
		"the string will be replaced with the source file name, "
		"and %o will be replaced with the output file name.",
		MUST_RELOAD,
		VCC_CC , NULL },
	{ "max_restarts", tweak_uint, &mgt_param.max_restarts,
		"0", NULL,
		"Upper limit on how many times a request can restart."
		"\nBe aware that restarts are likely to cause a hit against "
		"the backend, so don't increase thoughtlessly.",
		0,
		"4", "restarts" },
	{ "max_retries", tweak_uint, &mgt_param.max_retries,
		"0", NULL,
		"Upper limit on how many times a backend fetch can retry.",
		0,
		"4", "retries" },
	{ "max_esi_depth", tweak_uint, &mgt_param.max_esi_depth,
		"0", NULL,
		"Maximum depth of esi:include processing.",
		0,
		"5", "levels" },
	{ "connect_timeout", tweak_timeout, &mgt_param.connect_timeout,
		"0", NULL,
		"Default connection timeout for backend connections. "
		"We only try to connect to the backend for this many "
		"seconds before giving up. "
		"VCL can override this default value for each backend and "
		"backend request.",
		0,
		"3.5", "seconds" },
	{ "connect_bindany", tweak_bool, &mgt_param.connect_bindany,
		NULL, NULL,
		"Bind any before connect: move the 64k local ports limit"
		"to 64k connections per destination.",
		0,
		"off", "bool" },
	{ "clock_skew", tweak_uint, &mgt_param.clock_skew,
		"0", NULL,
		"How much clockskew we are willing to accept between the "
		"backend and our own clock.",
		0,
		"10", "seconds" },
	{ "prefer_ipv6", tweak_bool, &mgt_param.prefer_ipv6,
		NULL, NULL,
		"Prefer IPv6 address when connecting to backends which "
		"have both IPv4 and IPv6 addresses.",
		0,
		"off", "bool" },
	{ "session_max", tweak_uint,
		&mgt_param.max_sess,
		"1000", NULL,
		"Maximum number of sessions we will allocate from one pool "
		"before just dropping connections.\n"
		"This is mostly an anti-DoS measure, and setting it plenty "
		"high should not hurt, as long as you have the memory for "
		"it.",
		0,
		"100000", "sessions" },
	{ "timeout_linger", tweak_timeout, &mgt_param.timeout_linger,
		"0", NULL,
		"How long the worker thread lingers on an idle session "
		"before handing it over to the waiter.\n"
		"When sessions are reused, as much as half of all reuses "
		"happen within the first 100 msec of the previous request "
		"completing.\n"
		"Setting this too high results in worker threads not doing "
		"anything for their keep, setting it too low just means that "
		"more sessions take a detour around the waiter.",
		EXPERIMENTAL,
		"0.050", "seconds" },
	{ "syslog_cli_traffic", tweak_bool, &mgt_param.syslog_cli_traffic,
		NULL, NULL,
		"Log all CLI traffic to syslog(LOG_INFO).",
		0,
		"on", "bool" },
	{ "http_range_support", tweak_bool, &mgt_param.http_range_support,
		NULL, NULL,
		"Enable support for HTTP Range headers.",
		0,
		"on", "bool" },
	{ "http_gzip_support", tweak_bool, &mgt_param.http_gzip_support,
		NULL, NULL,
		"Enable gzip support. When enabled Varnish request compressed "
		"objects from the backend and store them compressed. "
		"If a client does not support gzip encoding Varnish will "
		"uncompress compressed objects on demand. Varnish will also "
		"rewrite the Accept-Encoding header of clients indicating "
		"support for gzip to:\n"
		"  Accept-Encoding: gzip\n\n"
		"Clients that do not support gzip will have their "
		"Accept-Encoding header removed. For more information on how "
		"gzip is implemented please see the chapter on gzip in the "
		"Varnish reference.",
		0,
		"on", "bool" },
	{ "gzip_level", tweak_uint, &mgt_param.gzip_level,
		"0", "9",
		"Gzip compression level: 0=debug, 1=fast, 9=best",
		0,
		"6", ""},
	{ "gzip_memlevel", tweak_uint, &mgt_param.gzip_memlevel,
		"1", "9",
		"Gzip memory level 1=slow/least, 9=fast/most compression.\n"
		"Memory impact is 1=1k, 2=2k, ... 9=256k.",
		0,
		"8", ""},
	{ "gzip_buffer",
		tweak_bytes_u, &mgt_param.gzip_buffer,
		"2048", NULL,
		"Size of malloc buffer used for gzip processing.\n"
		"These buffers are used for in-transit data,"
		" for instance gunzip'ed data being sent to a client."
		"Making this space to small results in more overhead,"
		" writes to sockets etc, making it too big is probably"
		" just a waste of memory.",
		EXPERIMENTAL,
		"32k", "bytes" },
	{ "shortlived", tweak_timeout,
		&mgt_param.shortlived,
		"0", NULL,
		"Objects created with (ttl+grace+keep) shorter than this"
		" are always put in transient storage.",
		0,
		"10", "seconds" },
	{ "critbit_cooloff", tweak_timeout,
		&mgt_param.critbit_cooloff,
		"60", "254",
		"How long the critbit hasher keeps deleted objheads "
		"on the cooloff list.",
		WIZARD,
		"180", "seconds" },
	{ "sigsegv_handler", tweak_bool, &mgt_param.sigsegv_handler,
		NULL, NULL,
		"Install a signal handler which tries to dump debug"
		" information on segmentation faults, bus errors and abort"
		" signals.",
		MUST_RESTART,
		"on", "bool" },
	{ "vcl_dir", tweak_string, &mgt_vcl_dir,
		NULL, NULL,
		"Directory from which relative VCL filenames (vcl.load and "
		"include) are opened.",
		0,
		VARNISH_VCL_DIR,
		NULL },
	{ "vmod_dir", tweak_string, &mgt_vmod_dir,
		NULL, NULL,
		"Directory where VCL modules are to be found.",
		0,
		VARNISH_VMOD_DIR,
		NULL },
	{ "vcl_cooldown", tweak_timeout, &mgt_param.vcl_cooldown,
		"0", NULL,
		"How long a VCL is kept warm after being replaced as the"
		" active VCL (granularity approximately 30 seconds).",
		0,
		"600", "seconds" },
	{ "vcc_err_unref", tweak_bool, &mgt_vcc_err_unref,
		NULL, NULL,
		"Unreferenced VCL objects result in error.",
		0,
		"on", "bool" },

	{ "vcc_allow_inline_c", tweak_bool, &mgt_vcc_allow_inline_c,
		NULL, NULL,
		"Allow inline C code in VCL.",
		0,
		"off", "bool" },

	{ "vcc_unsafe_path", tweak_bool, &mgt_vcc_unsafe_path,
		NULL, NULL,
		"Allow '/' in vmod & include paths.\n"
		"Allow 'import ... from ...'.",
		0,
		"on", "bool" },

	{ "pcre_match_limit", tweak_uint,
		&mgt_param.vre_limits.match,
		"1", NULL,
		"The limit for the number of calls to the internal match()"
		" function in pcre_exec().\n\n"
		"(See: PCRE_EXTRA_MATCH_LIMIT in pcre docs.)\n\n"
		"This parameter limits how much CPU time"
		" regular expression matching can soak up.",
		0,
		"10000", ""},
	{ "pcre_match_limit_recursion", tweak_uint,
		&mgt_param.vre_limits.match_recursion,
		"1", NULL,
		"The recursion depth-limit for the internal match() function"
		" in a pcre_exec().\n\n"
		"(See: PCRE_EXTRA_MATCH_LIMIT_RECURSION in pcre docs.)\n\n"
		"This puts an upper limit on the amount of stack used"
		" by PCRE for certain classes of regular expressions.\n\n"
		"We have set the default value low in order to"
		" prevent crashes, at the cost of possible regexp"
		" matching failures.\n\n"
		"Matching failures will show up in the log as VCL_Error"
		" messages with regexp errors -27 or -21.\n\n"
		"Testcase r01576 can be useful when tuning this parameter.",
		0,
		"20", ""},
	{ "vsl_space", tweak_bytes,
		&mgt_param.vsl_space,
		"1M", NULL,
		"The amount of space to allocate for the VSL fifo buffer"
		" in the VSM memory segment."
		"  If you make this too small, varnish{ncsa|log} etc will"
		" not be able to keep up."
		"  Making it too large just costs memory resources.",
		MUST_RESTART,
		"80M", "bytes"},

	{ "vsm_space", tweak_bytes,
		&mgt_param.vsm_space,
		"1M", NULL,
		"The amount of space to allocate for stats counters"
		" in the VSM memory segment."
		"  If you make this too small, some counters will be"
		" invisible."
		"  Making it too large just costs memory resources.",
		MUST_RESTART,
		"1M", "bytes"},

	{ "pool_req", tweak_poolparam, &mgt_param.req_pool,
		NULL, NULL,
		"Parameters for per worker pool request memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},
	{ "pool_sess", tweak_poolparam, &mgt_param.sess_pool,
		NULL, NULL,
		"Parameters for per worker pool session memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},
	{ "pool_vbo", tweak_poolparam, &mgt_param.vbo_pool,
		NULL, NULL,
		"Parameters for backend object fetch memory pool.\n"
		MEMPOOL_TEXT,
		0,
		"10,100,10", ""},

	{ NULL, NULL, NULL }
};
