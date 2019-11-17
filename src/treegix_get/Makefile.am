## Process this file with automake to produce Makefile.in

bin_PROGRAMS = treegix_get

treegix_get_SOURCES = treegix_get.c

treegix_get_LDADD = \
	$(top_srcdir)/src/libs/zbxcomms/libzbxcomms.a \
	$(top_srcdir)/src/libs/zbxcommon/libzbxcommon.a \
	$(top_srcdir)/src/libs/zbxlog/libzbxlog.a \
	$(top_srcdir)/src/libs/zbxcrypto/libzbxcrypto.a \
	$(top_srcdir)/src/libs/zbxsys/libzbxsys.a \
	$(top_srcdir)/src/libs/zbxnix/libzbxnix.a \
	$(top_srcdir)/src/libs/zbxconf/libzbxconf.a \
	$(top_srcdir)/src/libs/zbxcompress/libzbxcompress.a

treegix_get_LDADD += @TRXGET_LIBS@

treegix_get_LDFLAGS = @TRXGET_LDFLAGS@
