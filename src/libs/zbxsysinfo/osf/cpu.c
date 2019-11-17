

#include "common.h"
#include "sysinfo.h"
#include "../common/common.h"

int	SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*tmp;
	int	ret = SYSINFO_RET_FAIL;

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 0);

	/* only "all" (default) for parameter "cpu" is supported */
	if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "all"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 2);

	/* only "avg1" (default) for parameter "mode" is supported */
	if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "avg1"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "user"))
		ret = EXECUTE_DBL("iostat 1 2 | tail -n 1 | awk '{printf(\"%s\",$(NF-3))}'", result);
	else if (0 == strcmp(tmp, "nice"))
		ret = EXECUTE_DBL("iostat 1 2 | tail -n 1 | awk '{printf(\"%s\",$(NF-2))}'", result);
	else if (0 == strcmp(tmp, "system"))
		ret = EXECUTE_DBL("iostat 1 2 | tail -n 1 | awk '{printf(\"%s\",$(NF-1))}'", result);
	else if (0 == strcmp(tmp, "idle"))
		ret = EXECUTE_DBL("iostat 1 2 | tail -n 1 | awk '{printf(\"%s\",$(NF))}'", result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}

int	SYSTEM_CPU_LOAD(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*tmp;
	int	ret = SYSINFO_RET_FAIL;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 0);

	/* only "all" (default) for parameter "cpu" is supported */
	if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "all"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		ret = EXECUTE_DBL("uptime | awk '{printf(\"%s\", $(NF))}' | sed 's/[ ,]//g'", result);
	else if (0 == strcmp(tmp, "avg5"))
		ret = EXECUTE_DBL("uptime | awk '{printf(\"%s\", $(NF-1))}' | sed 's/[ ,]//g'", result);
	else if (0 == strcmp(tmp, "avg15"))
		ret = EXECUTE_DBL("uptime | awk '{printf(\"%s\", $(NF-2))}' | sed 's/[ ,]//g'", result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}
