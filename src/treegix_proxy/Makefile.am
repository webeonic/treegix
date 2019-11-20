## Process this file with automake to produce Makefile.in

SUBDIRS = \
	heart \
	housekeeper \
	proxyconfig \
	datasender \
	taskmanager

sbin_PROGRAMS = treegix_proxy

noinst_LIBRARIES = libtrxproxy.a

libtrxproxy_a_SOURCES = \
	servercomms.c servercomms.h \
	events.c \
	proxy_lld.c \
	proxy_alerter_protocol.c

libtrxproxy_a_CFLAGS = \
	-DTREEGIX_DAEMON

treegix_proxy_SOURCES = proxy.c

treegix_proxy_LDADD = \
	heart/libtrxheart.a \
	$(top_srcdir)/src/treegix_server/dbsyncer/libtrxdbsyncer.a \
	$(top_srcdir)/src/treegix_server/discoverer/libtrxdiscoverer.a \
	housekeeper/libtrxhousekeeper.a \
	$(top_srcdir)/src/treegix_server/httppoller/libtrxhttppoller.a \
	proxyconfig/libtrxproxyconfig.a \
	$(top_srcdir)/src/treegix_server/pinger/libtrxpinger.a \
	$(top_srcdir)/src/treegix_server/poller/libtrxpoller.a \
	$(top_srcdir)/src/treegix_server/poller/libtrxpoller_proxy.a \
	$(top_srcdir)/src/treegix_server/trapper/libtrxtrapper.a \
	$(top_srcdir)/src/treegix_server/snmptrapper/libtrxsnmptrapper.a \
	$(top_srcdir)/src/treegix_server/odbc/libtrxodbc.a \
	datasender/libtrxdatasender.a \
	taskmanager/libtrxtaskmanager.a \
	$(top_srcdir)/src/treegix_server/selfmon/libtrxselfmon.a \
	$(top_srcdir)/src/treegix_server/vmware/libtrxvmware.a \
	$(top_srcdir)/src/treegix_server/scripts/libtrxscripts.a \
	$(top_srcdir)/src/treegix_server/preprocessor/libpreprocessor.a \
	$(top_srcdir)/src/libs/trxsysinfo/libtrxproxysysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/common/libcommonsysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/simple/libsimplesysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/$(ARCH)/libspechostnamesysinfo.a \
	$(top_srcdir)/src/libs/trxlog/libtrxlog.a \
	$(top_srcdir)/src/libs/trxserver/libtrxserver.a \
	$(top_srcdir)/src/libs/trxserver/libtrxserver_proxy.a \
	$(top_srcdir)/src/libs/trxdbcache/libtrxdbcache.a \
	$(top_srcdir)/src/libs/trxhistory/libtrxhistory.a \
	$(top_srcdir)/src/libs/trxmemory/libtrxmemory.a \
	$(top_srcdir)/src/libs/trxregexp/libtrxregexp.a \
	$(top_srcdir)/src/libs/trxself/libtrxself.a \
	$(top_srcdir)/src/libs/trxnix/libtrxnix.a \
	$(top_srcdir)/src/libs/trxipcservice/libtrxipcservice.a \
	$(top_srcdir)/src/libs/trxsys/libtrxsys.a \
	$(top_srcdir)/src/libs/trxconf/libtrxconf.a \
	$(top_srcdir)/src/libs/trxcommon/libtrxcommon.a \
	$(top_srcdir)/src/libs/trxcrypto/libtrxcrypto.a \
	$(top_srcdir)/src/libs/trxcomms/libtrxcomms.a \
	$(top_srcdir)/src/libs/trxcommshigh/libtrxcommshigh.a \
	$(top_srcdir)/src/libs/trxjson/libtrxjson.a \
	$(top_srcdir)/src/libs/trxhttp/libtrxhttp.a \
	$(top_srcdir)/src/libs/trxexec/libtrxexec.a \
	$(top_srcdir)/src/libs/trxicmpping/libtrxicmpping.a \
	$(top_srcdir)/src/libs/trxdbupgrade/libtrxdbupgrade.a \
	$(top_srcdir)/src/libs/trxdbhigh/libtrxdbhigh.a \
	$(top_srcdir)/src/libs/trxdb/libtrxdb.a \
	$(top_srcdir)/src/libs/trxmodules/libtrxmodules.a \
	$(top_srcdir)/src/libs/trxtasks/libtrxtasks.a \
	$(top_srcdir)/src/libs/trxcompress/libtrxcompress.a \
	$(top_srcdir)/src/libs/trxembed/libtrxembed.a \
	$(top_srcdir)/src/libs/trxprometheus/libtrxprometheus.a \
	libtrxproxy.a

treegix_proxy_LDADD += $(top_srcdir)/src/libs/trxalgo/libtrxalgo.a

if HAVE_IPMI
treegix_proxy_LDADD += $(top_srcdir)/src/treegix_server/ipmi/libipmi.a
endif

treegix_proxy_LDADD += @PROXY_LIBS@

treegix_proxy_LDFLAGS = @PROXY_LDFLAGS@

treegix_proxy_CFLAGS = \
	-DDEFAULT_SSL_CERT_LOCATION="\"@CURL_SSL_CERT_LOCATION@\"" \
	-DDEFAULT_SSL_KEY_LOCATION="\"@CURL_SSL_KEY_LOCATION@\"" \
	-DDEFAULT_CONFIG_FILE="\"@PROXY_CONFIG_FILE@\"" \
	-DDEFAULT_EXTERNAL_SCRIPTS_PATH="\"@EXTERNAL_SCRIPTS_PATH@\"" \
	-DDEFAULT_LOAD_MODULE_PATH="\"@LOAD_MODULE_PATH@\""

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)@PROXY_CONFIG_FILE@.d"
	$(MKDIR_P) "$(DESTDIR)@EXTERNAL_SCRIPTS_PATH@"
	$(MKDIR_P) "$(DESTDIR)@LOAD_MODULE_PATH@"
	test -f "$(DESTDIR)@PROXY_CONFIG_FILE@" || cp "../../conf/treegix_proxy.conf" "$(DESTDIR)@PROXY_CONFIG_FILE@"
