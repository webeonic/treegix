

#include "common.h"
#include "system.h"

#ifdef _WINDOWS
#	include "perfmon.h"
#	pragma comment(lib, "user32.lib")
#endif

/******************************************************************************
 *                                                                            *
 * Function: SYSTEM_LOCALTIME                                                 *
 *                                                                            *
 * Comments: Thread-safe                                                      *
 *                                                                            *
 ******************************************************************************/
int	SYSTEM_LOCALTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*type, buf[32];
	long		milliseconds;
	struct tm	tm;
	trx_timezone_t	tz;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	type = get_rparam(request, 0);

	if (NULL == type || '\0' == *type || 0 == strcmp(type, "utc"))
	{
		SET_UI64_RESULT(result, time(NULL));
	}
	else if (0 == strcmp(type, "local"))
	{
		trx_get_time(&tm, &milliseconds, &tz);

		trx_snprintf(buf, sizeof(buf), "%04d-%02d-%02d,%02d:%02d:%02d.%03ld,%1c%02d:%02d",
				1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec, milliseconds,
				tz.tz_sign, tz.tz_hour, tz.tz_min);

		SET_STR_RESULT(result, strdup(buf));
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	SYSTEM_USERS_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef _WINDOWS
	char		counter_path[64];
	AGENT_REQUEST	request_tmp;
	int		ret;

	TRX_UNUSED(request);

	trx_snprintf(counter_path, sizeof(counter_path), "\\%u\\%u",
			(unsigned int)get_builtin_counter_index(PCI_TERMINAL_SERVICES),
			(unsigned int)get_builtin_counter_index(PCI_TOTAL_SESSIONS));

	request_tmp.nparam = 1;
	request_tmp.params = trx_malloc(NULL, request_tmp.nparam * sizeof(char *));
	request_tmp.params[0] = counter_path;

	ret = PERF_COUNTER(&request_tmp, result);

	trx_free(request_tmp.params);

	return ret;
#else
	TRX_UNUSED(request);

	return EXECUTE_INT("who | wc -l", result);
#endif
}
