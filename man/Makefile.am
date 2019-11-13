## Process this file with automake to produce Makefile.in

if AGENT
agent_MANS = treegix_agentd.man
client_MANS = treegix_get.man treegix_sender.man
endif

if PROXY
proxy_MANS = treegix_proxy.man
endif

if SERVER
server_MANS = treegix_server.man
endif

man1_MANS = $(client_MANS)
man8_MANS = $(agent_MANS) $(proxy_MANS) $(server_MANS)

EXTRA_DIST = \
	treegix_agentd.man \
	treegix_agent2.man \
	treegix_get.man \
	treegix_sender.man \
	treegix_proxy.man \
	treegix_server.man
