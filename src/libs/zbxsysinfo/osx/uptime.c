

#include "common.h"
#include "sysinfo.h"
#include "log.h"

int	SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int		mib[] = {CTL_KERN, KERN_BOOTTIME};
	struct timeval	boottime;
	size_t		len = sizeof(boottime);

	if (0 != sysctl(mib, 2, &boottime, &len, NULL, 0))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, time(NULL) - boottime.tv_sec);

	return SYSINFO_RET_OK;
}
