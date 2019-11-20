

#include "common.h"
#include "sysinfo.h"
#include "log.h"

int	KERNEL_MAXFILES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	mib[] = {CTL_KERN, KERN_MAXFILES}, maxfiles;
	size_t	len = sizeof(maxfiles);

	if (0 != sysctl(mib, 2, &maxfiles, &len, NULL, 0))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, maxfiles);

	return SYSINFO_RET_OK;
}

int	KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	mib[] = {CTL_KERN, KERN_MAXPROC}, maxproc;
	size_t	len = sizeof(maxproc);

	if (0 != sysctl(mib, 2, &maxproc, &len, NULL, 0))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, maxproc);

	return SYSINFO_RET_OK;
}
