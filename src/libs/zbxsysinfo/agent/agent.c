

#include "common.h"
#include "sysinfo.h"

extern char	*CONFIG_HOSTNAME;

static int	AGENT_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result);
static int	AGENT_PING(AGENT_REQUEST *request, AGENT_RESULT *result);
static int	AGENT_VERSION(AGENT_REQUEST *request, AGENT_RESULT *result);

TRX_METRIC	parameters_agent[] =
/*	KEY			FLAG		FUNCTION	TEST PARAMETERS */
{
	{"agent.hostname",	0,		AGENT_HOSTNAME,	NULL},
	{"agent.ping",		0,		AGENT_PING,	NULL},
	{"agent.version",	0,		AGENT_VERSION,	NULL},
	{NULL}
};

static int	AGENT_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	TRX_UNUSED(request);

	SET_STR_RESULT(result, zbx_strdup(NULL, CONFIG_HOSTNAME));

	return SYSINFO_RET_OK;
}

static int	AGENT_PING(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	TRX_UNUSED(request);

	SET_UI64_RESULT(result, 1);

	return SYSINFO_RET_OK;
}

static int	AGENT_VERSION(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	TRX_UNUSED(request);

	SET_STR_RESULT(result, zbx_strdup(NULL, TREEGIX_VERSION));

	return SYSINFO_RET_OK;
}
