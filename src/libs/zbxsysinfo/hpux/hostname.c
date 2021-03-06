

#include "sysinfo.h"
#include "log.h"

TRX_METRIC	parameter_hostname =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
	{"system.hostname",     0,              SYSTEM_HOSTNAME,        NULL};

int	SYSTEM_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*hostname;
	long 	hostbufsize = 0;

#ifdef _SC_HOST_NAME_MAX
	hostbufsize = sysconf(_SC_HOST_NAME_MAX) + 1;
#endif
	if (0 == hostbufsize)
		hostbufsize = 256;

	hostname = trx_malloc(NULL, hostbufsize);

	if (0 != gethostname(hostname, hostbufsize))
	{
		trx_free(hostname);
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_STR_RESULT(result, hostname);

	return SYSINFO_RET_OK;
}
