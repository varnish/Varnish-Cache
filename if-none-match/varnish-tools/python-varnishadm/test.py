#!/usr/bin/env python
# Yes, this is very much a hack, but it's something...
from varnishadmin import VarnishAdmin

v = VarnishAdmin()
print v.status()
print v.status()
print v.status()
print v.status()
print v.purge('^/test1$')
print v.status()
print v.purge('^/test2$')
print v.status()
print v.stats()
print v.purge('^/$')
print v.showparam("backend_http11")
#print v.allparams()
print v.setparam("backend_http11","on")
print v.showparam("backend_http11")

