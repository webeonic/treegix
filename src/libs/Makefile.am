## Process this file with automake to produce Makefile.in

DIST_SUBDIRS = \
	trxdbcache \
	trxdbhigh \
	trxdb \
	trxdbupgrade \
	trxmemory \
	trxalgo \
	trxmedia \
	trxcrypto \
	trxcommon \
	trxcomms \
	trxcommshigh \
	trxlog \
	trxnix \
	trxconf \
	trxsysinfo \
	trxsys \
	trxjson \
	trxserver \
	trxicmpping \
	trxexec \
	trxself \
	trxmodules \
	trxregexp \
	trxtasks \
	trxhttp \
	trxipcservice \
	trxhistory \
	trxcompress \
	trxembed \
	trxprometheus

if SERVER
SERVER_SUBDIRS = \
	trxcommshigh \
	trxdb \
	trxdbupgrade \
	trxdbcache \
	trxdbhigh \
	trxhttp \
	trxmemory \
	trxserver \
	trxicmpping \
	trxmedia \
	trxself \
	trxtasks \
	trxhistory \
	trxcompress \
	trxembed \
	trxprometheus
else
if PROXY
PROXY_SUBDIRS = \
	trxcommshigh \
	trxdb \
	trxdbupgrade \
	trxdbcache \
	trxdbhigh \
	trxhttp \
	trxmemory \
	trxserver \
	trxicmpping \
	trxself \
	trxtasks \
	trxhistory \
	trxcompress \
	trxembed \
	trxprometheus
endif
endif

SUBDIRS = \
	trxcrypto \
	trxcommon \
	trxlog \
	trxalgo \
	trxnix \
	trxconf \
	trxhttp \
	trxsysinfo \
	trxsys \
	trxcomms \
	trxjson \
	trxexec \
	trxmodules \
	trxregexp \
	trxipcservice \
	trxcompress \
	$(PROXY_SUBDIRS) \
	$(SERVER_SUBDIRS) \
	$(AGENT_SUBDIRS)

EXTRA_DIST = trxwin32
