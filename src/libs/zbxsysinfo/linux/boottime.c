

#include "common.h"
#include "sysinfo.h"
#include "log.h"

int	SYSTEM_BOOTTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	FILE		*f;
	char		buf[MAX_STRING_LEN];
	int		ret = SYSINFO_RET_FAIL;
	unsigned long	value;

	TRX_UNUSED(request);

	if (NULL == (f = fopen("/proc/stat", "r")))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open /proc/stat: %s", trx_strerror(errno)));
		return ret;
	}

	/* find boot time entry "btime [boot time]" */
	while (NULL != fgets(buf, MAX_STRING_LEN, f))
	{
		if (1 == sscanf(buf, "btime %lu", &value))
		{
			SET_UI64_RESULT(result, value);

			ret = SYSINFO_RET_OK;

			break;
		}
	}
	trx_fclose(f);

	if (SYSINFO_RET_FAIL == ret)
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot find a line with \"btime\" in /proc/stat."));

	return ret;
}
