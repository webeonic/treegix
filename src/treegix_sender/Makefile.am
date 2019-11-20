## Process this file with automake to produce Makefile.in

bin_PROGRAMS = treegix_sender

treegix_sender_SOURCES = treegix_sender.c

treegix_sender_LDADD = \
	$(top_srcdir)/src/libs/trxjson/libtrxjson.a \
	$(top_srcdir)/src/libs/trxalgo/libtrxalgo.a \
	$(top_srcdir)/src/libs/trxregexp/libtrxregexp.a \
	$(top_srcdir)/src/libs/trxcomms/libtrxcomms.a \
	$(top_srcdir)/src/libs/trxcommon/libtrxcommon.a \
	$(top_srcdir)/src/libs/trxlog/libtrxlog.a \
	$(top_srcdir)/src/libs/trxsys/libtrxsys.a \
	$(top_srcdir)/src/libs/trxnix/libtrxnix.a \
	$(top_srcdir)/src/libs/trxcrypto/libtrxcrypto.a \
	$(top_srcdir)/src/libs/trxconf/libtrxconf.a \
	$(top_srcdir)/src/libs/trxcompress/libtrxcompress.a

treegix_sender_LDADD += @SENDER_LIBS@

treegix_sender_LDFLAGS = @SENDER_LDFLAGS@

EXTRA_DIST = win32
