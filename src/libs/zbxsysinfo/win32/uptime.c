

#include "common.h"

#include "perfmon.h"
#include "sysinfo.h"

int	SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		counter_path[64];
	AGENT_REQUEST	request_tmp;
	int		ret;

	trx_snprintf(counter_path, sizeof(counter_path), "\\%u\\%u",
			(unsigned int)get_builtin_counter_index(PCI_SYSTEM),
			(unsigned int)get_builtin_counter_index(PCI_SYSTEM_UP_TIME));

	request_tmp.nparam = 1;
	request_tmp.params = trx_malloc(NULL, request_tmp.nparam * sizeof(char *));
	request_tmp.params[0] = counter_path;

	ret = PERF_COUNTER(&request_tmp, result);

	trx_free(request_tmp.params);

	if (SYSINFO_RET_FAIL == ret)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain system information."));
		return SYSINFO_RET_FAIL;
	}

	/* result must be integer to correctly interpret it in frontend (uptime) */
	if (!GET_UI64_RESULT(result))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid result. Unsigned integer is expected."));
		return SYSINFO_RET_FAIL;
	}

	UNSET_RESULT_EXCLUDING(result, AR_UINT64);

	return SYSINFO_RET_OK;
}
