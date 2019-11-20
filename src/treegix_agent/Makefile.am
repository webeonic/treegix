## Process this file with automake to produce Makefile.in

SUBDIRS = \
	logfiles
	
EXTRA_DIST = \
	eventlog.c eventlog.h \
	perfstat.c perfstat.h

sbin_PROGRAMS = treegix_agentd

noinst_LIBRARIES = libtrxagent.a

libtrxagent_a_SOURCES = \
	active.c active.h \
	stats.c stats.h \
	cpustat.c cpustat.h \
	diskdevices.c diskdevices.h \
	vmstats.c vmstats.h \
	trxconf.c trxconf.h \
	listener.c listener.h \
	procstat.c procstat.h \
	metrics.h

libtrxagent_a_CFLAGS = \
	-DTREEGIX_DAEMON

treegix_agentd_SOURCES = treegix_agentd.c

treegix_agentd_LDADD = \
	libtrxagent.a \
	logfiles/libtrxlogfiles.a \
	$(top_srcdir)/src/libs/trxsysinfo/libtrxagentsysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/$(ARCH)/libspecsysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/$(ARCH)/libspechostnamesysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/agent/libagentsysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/common/libcommonsysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/simple/libsimplesysinfo.a \
	$(top_srcdir)/src/libs/trxlog/libtrxlog.a \
	$(top_srcdir)/src/libs/trxregexp/libtrxregexp.a \
	$(top_srcdir)/src/libs/trxalgo/libtrxalgo.a \
	$(top_srcdir)/src/libs/trxsys/libtrxsys.a \
	$(top_srcdir)/src/libs/trxnix/libtrxnix.a \
	$(top_srcdir)/src/libs/trxcomms/libtrxcomms.a \
	$(top_srcdir)/src/libs/trxconf/libtrxconf.a \
	$(top_srcdir)/src/libs/trxjson/libtrxjson.a \
	$(top_srcdir)/src/libs/trxcommon/libtrxcommon.a \
	$(top_srcdir)/src/libs/trxcrypto/libtrxcrypto.a \
	$(top_srcdir)/src/libs/trxexec/libtrxexec.a \
	$(top_srcdir)/src/libs/trxcompress/libtrxcompress.a \
	$(top_srcdir)/src/libs/trxmodules/libtrxmodules.a \
	$(top_srcdir)/src/libs/trxhttp/libtrxhttp.a

treegix_agentd_LDADD += @AGENT_LIBS@

treegix_agentd_LDFLAGS = @AGENT_LDFLAGS@

treegix_agentd_CFLAGS = \
	-DTREEGIX_DAEMON \
	-DDEFAULT_CONFIG_FILE="\"@AGENT_CONFIG_FILE@\"" \
	-DDEFAULT_LOAD_MODULE_PATH="\"@LOAD_MODULE_PATH@\""

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)@AGENT_CONFIG_FILE@.d"
	$(MKDIR_P) "$(DESTDIR)@LOAD_MODULE_PATH@"
	test -f "$(DESTDIR)@AGENT_CONFIG_FILE@" || cp "../../conf/treegix_agentd.conf" "$(DESTDIR)@AGENT_CONFIG_FILE@"
