# $Id$

CHROME_CONTENT =						\
	chrome/content/varnish-icon-red-32.gif			\
	chrome/content/varnish.xul				\

FILES =								\
	install.rdf						\
	chrome.manifest						\
	${CHROME_CONTENT}

all: varnish.xpi

varnish.xpi: ${FILES}
	zip $@ ${FILES}
