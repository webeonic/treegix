## Process this file with automake to produce Makefile.in

DIST_SUBDIRS = \
	libs \
	treegix_agent \
	treegix_get \
	treegix_sender \
	treegix_proxy \
	treegix_server \
	treegix_java

if AGENT
COMMON_SUBDIRS = libs
else
if SERVER
COMMON_SUBDIRS = libs
else
if PROXY
COMMON_SUBDIRS = libs
else
if AGENT2
COMMON_SUBDIRS = libs
endif
endif
endif
endif

if AGENT
AGENT_SUBDIRS = \
	treegix_agent \
	treegix_get \
	treegix_sender
else
if AGENT2
AGENT_SUBDIRS = \
	treegix_agent
endif
endif

if SERVER
SERVER_SUBDIRS = \
	treegix_server
endif

if PROXY
if SERVER
PROXY_SUBDIRS = \
	treegix_proxy
else

PROXY_SUBDIRS = \
	treegix_server/dbsyncer \
	treegix_server/dbconfig \
	treegix_server/discoverer \
	treegix_server/httppoller \
	treegix_server/pinger \
	treegix_server/poller \
	treegix_server/trapper \
	treegix_server/selfmon \
	treegix_server/snmptrapper \
	treegix_server/vmware \
	treegix_server/ipmi \
	treegix_server/odbc \
	treegix_server/scripts \
	treegix_server/preprocessor \
	treegix_proxy
	
endif
endif

if JAVA
JAVA_SUBDIRS = \
	treegix_java
endif

SUBDIRS = \
	$(COMMON_SUBDIRS) \
	$(AGENT_SUBDIRS) \
	$(SERVER_SUBDIRS) \
	$(PROXY_SUBDIRS) \
	$(JAVA_SUBDIRS)
	

## "dist-hook" run after the distribution directory is filled, but before the actual tar (or shar) file is created.
dist-hook:
	mkdir $(distdir)/modules
	mkdir $(distdir)/modules/dummy
	cp $(srcdir)/modules/dummy/*.c $(distdir)/modules/dummy/
	cp $(srcdir)/modules/dummy/Makefile $(distdir)/modules/dummy/
	cp $(srcdir)/modules/dummy/README $(distdir)/modules/dummy/
