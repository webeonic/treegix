

#include "sysinfo.h"
#include "log.h"

#ifdef HAVE_SYS_UTSNAME_H
#	include <sys/utsname.h>
#endif

TRX_METRIC	parameter_hostname =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
	{"system.hostname",     0,              SYSTEM_HOSTNAME,        NULL};

int	SYSTEM_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct utsname	name;

	if (-1 == uname(&name))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_STR_RESULT(result, zbx_strdup(NULL, name.nodename));

	return SYSINFO_RET_OK;
}
