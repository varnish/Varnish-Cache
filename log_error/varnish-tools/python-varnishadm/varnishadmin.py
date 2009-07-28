"""
Copyright (c) 2009 Redpill Linpro AB
All rights reserved.

Author: Magnus Hagander <magnus.hagander@redpill-linpro.com>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer
   in this position and unchanged.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.

$Id$

"""

import socket

class VarnishAdmin(object):
	"""
	VarnishAdmin is a thin wrapper class around the admin interface to
	varnish (see www.varnish-cache.org). At this point, it contains
	the most common operations only - use the "help" command in the
	admin interface to find out about other availble commands.
	"""
	def __init__(self, port=6082, host="localhost"):
		self.port = port
		self.host = host
		self.file = None

	def __connect(self):
		""" If a connection has not yet been made, make one. """
		if self.file:
			return
		sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		sock.connect((self.host, self.port))
		self.file = sock.makefile()
		sock.close() # it's been dup()ed into self.file

	def disconnect(self):
		""" Disconnect from the varnish server. Will automatically
		    reconnect if needed later. """
		self.file.close()
		self.file = None

	def __command(self, command):
		""" Execute a command and return a string with the full
		    response from the varnish server. """
		self.__connect()
		self.file.write("%s\n" % command)
		self.file.flush()

		status = self.file.readline()
		(code, blen) = status.split()

		msg = self.file.read(int(blen)+1) # 1 extra for newline

		if code != "200":
			raise Exception("Error code %s returned from Varnish, message is '%s'" % (code, msg))
		return msg.rstrip()

	def status(self):
		""" Get the status of the server. """
		return self.__command("status")

	def start(self):
		""" Start the server (if stopped). """
		return self.__command("start")

	def stop(self):
		""" Stop the server (if started). """
		return self.__command("stop")

	def purge(self, purgere):
		""" Purge URLs matching a regexp from the cache. """
		return self.__command("url.purge %s" % purgere)

	def stats(self):
		""" Get all statistics from the server as a
		    dictionary. """
		d = {}
		for (val,key) in [l.strip().split(None,1) for l in 
			self.__command("stats").splitlines()]:
				d[key] = int(val)
		return d

	def __parseparam(self, param):
		""" Parse parameter output into dictionary of dictionaries. """
		params = {}
		current = ""
		for l in self.__command("param.show %s" % param).splitlines():
			if not l.strip():
				continue
			if l.startswith(" "):
				# Starts with space, append to the current one
				params[current]['description'] += l.lstrip() + " "
			else:
				# Starts with something else, this is a new parameter
				(current, txt) = l.split(None, 1)
				params[current] = {
					'value': txt,
					'description': '',
				}
		return params

	def showparam(self, param):
		""" Return the value of a single parameter as a dictionary with
		    keys 'description' and 'value'. """
		return self.__parseparam(param)[param]

	def allparams(self):
		""" Return a dictionary containing all parameters. The
		    dictionary is indexed by parameter name, and each entry
		    is a dictionary with keys 'description' and 'value'. """
		return self.__parseparam("-l")

	def setparam(self, param, value):
		""" Set the value of a parameter. """
		return self.__command("param.set %s %s" % (param, value))

