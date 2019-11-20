## Process this file with automake to produce Makefile.in

SUBDIRS = \
	alerter \
	dbsyncer \
	dbconfig \
	discoverer \
	housekeeper \
	httppoller \
	pinger \
	poller \
	snmptrapper \
	timer \
	trapper \
	escalator \
	proxypoller \
	selfmon \
	vmware \
	taskmanager \
	ipmi \
	odbc \
	scripts \
	preprocessor \
	lld

sbin_PROGRAMS = treegix_server

noinst_LIBRARIES = libtrxserver.a

libtrxserver_a_SOURCES = \
	actions.c actions.h \
	operations.c operations.h \
	events.c events.h \
	postinit.c postinit.h

libtrxserver_a_CFLAGS = \
	-DTREEGIX_DAEMON \
	-I@top_srcdir@/src/libs/trxdbcache

treegix_server_SOURCES = server.c

treegix_server_LDADD = \
	alerter/libtrxalerter.a \
	dbsyncer/libtrxdbsyncer.a \
	dbconfig/libtrxdbconfig.a \
	discoverer/libtrxdiscoverer.a \
	pinger/libtrxpinger.a \
	poller/libtrxpoller.a \
	housekeeper/libtrxhousekeeper.a \
	timer/libtrxtimer.a \
	trapper/libtrxtrapper.a \
	snmptrapper/libtrxsnmptrapper.a \
	httppoller/libtrxhttppoller.a \
	escalator/libtrxescalator.a \
	proxypoller/libtrxproxypoller.a \
	selfmon/libtrxselfmon.a \
	vmware/libtrxvmware.a \
	taskmanager/libtrxtaskmanager.a \
	odbc/libtrxodbc.a \
	scripts/libtrxscripts.a \
	preprocessor/libpreprocessor.a \
	$(top_srcdir)/src/libs/trxembed/libtrxembed.a \
	poller/libtrxpoller_server.a \
	lld/libtrxlld.a \
	libtrxserver.a \
	$(top_srcdir)/src/libs/trxprometheus/libtrxprometheus.a \
	$(top_srcdir)/src/libs/trxsysinfo/libtrxserversysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/common/libcommonsysinfo.a \
	$(top_srcdir)/src/libs/trxsysinfo/simple/libsimplesysinfo.a \
	$(top_srcdir)/src/libs/trxlog/libtrxlog.a \
	$(top_srcdir)/src/libs/trxserver/libtrxserver.a \
	$(top_srcdir)/src/libs/trxdbcache/libtrxdbcache.a \
	$(top_srcdir)/src/libs/trxhistory/libtrxhistory.a \
	$(top_srcdir)/src/libs/trxmemory/libtrxmemory.a \
	$(top_srcdir)/src/libs/trxregexp/libtrxregexp.a \
	$(top_srcdir)/src/libs/trxself/libtrxself.a \
	$(top_srcdir)/src/libs/trxnix/libtrxnix.a \
	$(top_srcdir)/src/libs/trxalgo/libtrxalgo.a \
	$(top_srcdir)/src/libs/trxsys/libtrxsys.a \
	$(top_srcdir)/src/libs/trxconf/libtrxconf.a \
	$(top_srcdir)/src/libs/trxmedia/libtrxmedia.a \
	$(top_srcdir)/src/libs/trxcommon/libtrxcommon.a \
	$(top_srcdir)/src/libs/trxcrypto/libtrxcrypto.a \
	$(top_srcdir)/src/libs/trxcomms/libtrxcomms.a \
	$(top_srcdir)/src/libs/trxcommshigh/libtrxcommshigh.a \
	$(top_srcdir)/src/libs/trxjson/libtrxjson.a \
	$(top_srcdir)/src/libs/trxhttp/libtrxhttp.a \
	$(top_srcdir)/src/libs/trxipcservice/libtrxipcservice.a \
	$(top_srcdir)/src/libs/trxexec/libtrxexec.a \
	$(top_srcdir)/src/libs/trxicmpping/libtrxicmpping.a \
	$(top_srcdir)/src/libs/trxdbupgrade/libtrxdbupgrade.a \
	$(top_srcdir)/src/libs/trxdbhigh/libtrxdbhigh.a \
	$(top_srcdir)/src/libs/trxdb/libtrxdb.a \
	$(top_srcdir)/src/libs/trxmodules/libtrxmodules.a \
	$(top_srcdir)/src/libs/trxtasks/libtrxtasks.a \
	$(top_srcdir)/src/libs/trxcompress/libtrxcompress.a \
	$(top_srcdir)/src/libs/trxserver/libtrxserver_server.a

if HAVE_IPMI
treegix_server_LDADD += ipmi/libipmi.a
endif

treegix_server_LDADD += @SERVER_LIBS@

treegix_server_LDFLAGS = @SERVER_LDFLAGS@

treegix_server_CFLAGS = \
	-DDEFAULT_SSL_CERT_LOCATION="\"@CURL_SSL_CERT_LOCATION@\"" \
	-DDEFAULT_SSL_KEY_LOCATION="\"@CURL_SSL_KEY_LOCATION@\"" \
	-DDEFAULT_CONFIG_FILE="\"@SERVER_CONFIG_FILE@\"" \
	-DDEFAULT_EXTERNAL_SCRIPTS_PATH="\"@EXTERNAL_SCRIPTS_PATH@\"" \
	-DDEFAULT_ALERT_SCRIPTS_PATH="\"@ALERT_SCRIPTS_PATH@\"" \
	-DDEFAULT_LOAD_MODULE_PATH="\"@LOAD_MODULE_PATH@\""

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)@SERVER_CONFIG_FILE@.d"
	$(MKDIR_P) "$(DESTDIR)@EXTERNAL_SCRIPTS_PATH@"
	$(MKDIR_P) "$(DESTDIR)@ALERT_SCRIPTS_PATH@"
	$(MKDIR_P) "$(DESTDIR)@LOAD_MODULE_PATH@"
	test -f "$(DESTDIR)@SERVER_CONFIG_FILE@" || cp "../../conf/treegix_server.conf" "$(DESTDIR)@SERVER_CONFIG_FILE@"
