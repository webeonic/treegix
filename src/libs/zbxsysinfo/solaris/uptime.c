

#include "common.h"
#include "sysinfo.h"

int	SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	if (SYSINFO_RET_OK == SYSTEM_BOOTTIME(request, result))
	{
		time_t	now;

		time(&now);

		result->ui64 = now - result->ui64;

		return SYSINFO_RET_OK;
	}

	return SYSINFO_RET_FAIL;
}
