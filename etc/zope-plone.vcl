#
# This is an example VCL configuration file for varnish, meant for the
# Plone CMS running within Zope.  It defines a "default" backend for
# serving static content from a normal web server, and a "zope"
# backend for requests to the Zope CMS
#
# See the vcl(7) man page for details on VCL syntax and semantics.
#
# $Id$
#

# Default backend definition.  Set this to point to your content
# server.

# Default backend is the Zope CMS
backend default {
	.host = "127.0.0.1";
	.port = "9673";
}

acl purge {
	"localhost";
	"192.0.2.0"/24;
}

sub vcl_recv {

        # Hotfix to fix CVE 2011-3587 for Zope 2.12 + 2.13: Products.Zope_Hotfix_CVE_2011_3587
        if (req.url ~ "/p_/webdav/?(.*)"){
               set req.url = "/";
        }

        # Normalize host headers, and do rewriting for the zope sites.  Reject
        # requests for unknown hosts
        if (req.http.host ~ "(www.)?example.com") {
                set req.http.host = "example.com";
                set req.url = "/VirtualHostBase/http/example.com:80/example.com/VirtualHostRoot" + req.url;
        } elsif (req.http.host ~ "(www.)?example.org") {
                set req.http.host = "example.org";
                set req.url = "/VirtualHostBase/http/example.org:80/example.org/VirtualHostRoot" + req.url;
        } else {
                error 404 "Unknown virtual host.";
        }

        # Handle special requests
        if (req.method != "GET" && req.method != "HEAD") {

                # POST - Logins and edits
                if (req.method == "POST") {
                        return(pass);
                }
                
                # PURGE - The CacheFu product can invalidate updated URLs
                if (req.method == "PURGE") {
                        if (!client.ip ~ purge) {
                                error 405 "Not allowed.";
                        }
                        return(lookup);
                }

                # Do not cache the creation of objects in Plone
                if (req.url ~ "createObject"){
                        return(pass);
                }

                if (req.url ~ "^/.*/resolveuid/?"){
                       remove req.http.cookie;
                       return(lookup);
                }
        }

        # Don't cache authenticated requests
        if (req.http.Cookie && req.http.Cookie ~ "__ac(|_(name|password|persistent))=") {

		# Force lookup of specific urls unlikely to need protection
        if (req.url ~ "^/[^-]*\-cachekey\d{4}\.(css|kss|js)$") {
                        remove req.http.cookie;
                        return(lookup);
                }
                return(pass);
        }

        # The default vcl_recv is used from here.
 }

# Do the PURGE thing
sub vcl_hit {
        if (req.method == "PURGE") {
                purge;
                error 200 "Purged";
        }
}
sub vcl_miss {
        if (req.method == "PURGE") {
                purge;
                error 200 "Purged";
        }
}

# Enforce a minimum TTL, since we can PURGE changed objects actively
# from Zope by using the CacheFu product

sub vcl_response {
        if (beresp.ttl < 3600s) {
                set beresp.ttl = 3600s;
        }
}
