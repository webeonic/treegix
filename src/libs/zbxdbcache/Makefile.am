## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libtrxdbcache.a

libtrxdbcache_a_SOURCES = \
	dbcache.c \
	dbconfig.c \
	dbconfig.h \
	dbsync.c \
	dbsync.h \
	valuecache.c \
	valuecache.h \
	dbconfig_dump.c \
	dbconfig_maintenance.c

libtrxdbcache_a_CFLAGS = \
	-I@top_srcdir@/src/treegix_server/ \
	-I@top_srcdir@/src/libs/trxalgo \
	$(TEST_FLAGS)
