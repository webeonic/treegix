

#include "common.h"
#include "sysinfo.h"
#include "log.h"

int	SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME
	int		mib[2], now;
	size_t		len;
	struct timeval	uptime;

	mib[0] = CTL_KERN;
	mib[1] = KERN_BOOTTIME;

	len = sizeof(struct timeval);

	if (0 != sysctl(mib, 2, &uptime, &len, NULL, 0))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	now = time(NULL);

	SET_UI64_RESULT(result, now - uptime.tv_sec);

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, trx_strdup(NULL, "Agent was compiled without support for uptime information."));
	return SYSINFO_RET_FAIL;
#endif
}
