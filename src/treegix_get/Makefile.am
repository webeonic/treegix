## Process this file with automake to produce Makefile.in

bin_PROGRAMS = treegix_get

treegix_get_SOURCES = treegix_get.c

treegix_get_LDADD = \
	$(top_srcdir)/src/libs/trxcomms/libtrxcomms.a \
	$(top_srcdir)/src/libs/trxcommon/libtrxcommon.a \
	$(top_srcdir)/src/libs/trxlog/libtrxlog.a \
	$(top_srcdir)/src/libs/trxcrypto/libtrxcrypto.a \
	$(top_srcdir)/src/libs/trxsys/libtrxsys.a \
	$(top_srcdir)/src/libs/trxnix/libtrxnix.a \
	$(top_srcdir)/src/libs/trxconf/libtrxconf.a \
	$(top_srcdir)/src/libs/trxcompress/libtrxcompress.a

treegix_get_LDADD += @TRXGET_LIBS@

treegix_get_LDFLAGS = @TRXGET_LDFLAGS@
