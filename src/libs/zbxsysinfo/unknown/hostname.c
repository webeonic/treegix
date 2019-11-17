

#include "sysinfo.h"

TRX_METRIC	parameter_hostname =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
	{"system.hostname",     0,              SYSTEM_HOSTNAME,        NULL};

int	SYSTEM_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Not supported because the system is unknown."));
	return SYSINFO_RET_FAIL;
}
