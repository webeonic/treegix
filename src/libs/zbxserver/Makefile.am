## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libtrxserver.a libtrxserver_server.a libtrxserver_proxy.a

libtrxserver_a_SOURCES = \
	evalfunc.c evalfunc.h \
	expression.c \
	macrofunc.c macrofunc.h \
	treegix_stats.c treegix_stats.h

libtrxserver_server_a_SOURCES = \
	treegix_stats_server.c treegix_stats.h

libtrxserver_proxy_a_SOURCES = \
	treegix_stats_proxy.c treegix_stats.h

libtrxserver_a_CFLAGS = -I@top_srcdir@/src/libs/trxdbcache @LIBXML2_CFLAGS@

libtrxserver_server_a_CFLAGS = -I@top_srcdir@/src/libs/trxdbcache
