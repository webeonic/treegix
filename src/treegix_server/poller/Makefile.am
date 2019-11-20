## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libtrxpoller.a libtrxpoller_server.a libtrxpoller_proxy.a

libtrxpoller_a_SOURCES = \
	checks_agent.c checks_agent.h \
	checks_internal.c checks_internal.h \
	checks_simple.c checks_simple.h \
	checks_simple_vmware.c checks_simple_vmware.h \
	checks_snmp.c checks_snmp.h \
	checks_db.c checks_db.h \
	checks_aggregate.c checks_aggregate.h \
	checks_external.c checks_external.h \
	checks_ssh.c checks_ssh.h \
	checks_telnet.c checks_telnet.h \
	checks_java.c checks_java.h \
	checks_calculated.c checks_calculated.h \
	checks_http.c checks_http.h \
	poller.c poller.h
	
libtrxpoller_server_a_SOURCES = \
	checks_internal_server.c checks_internal.h

libtrxpoller_proxy_a_SOURCES = \
	checks_internal_proxy.c checks_internal.h

libtrxpoller_a_CFLAGS = -I@top_srcdir@/src/libs/trxsysinfo/simple -I@top_srcdir@/src/libs/trxdbcache @SNMP_CFLAGS@ @SSH2_CFLAGS@

libtrxpoller_server_a_CFLAGS = -I@top_srcdir@/src/libs/trxdbcache
