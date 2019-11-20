

#include "common.h"
#include "sysinfo.h"
#include "threads.h"
#include "perfstat.h"
#include "log.h"

int	USER_PERF_COUNTER(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_FAIL;
	char	*counter, *error = NULL;
	double	value;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (1 != request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	if (NULL == (counter = get_rparam(request, 0)) || '\0' == *counter)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		goto out;
	}

	if (SUCCEED != get_perf_counter_value_by_name(counter, &value, &error))
	{
		SET_MSG_RESULT(result, error != NULL ? error :
				trx_strdup(NULL, "Cannot obtain performance information from collector."));
		goto out;
	}

	SET_DBL_RESULT(result, value);
	ret = SYSINFO_RET_OK;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int perf_counter_ex(const char *function, AGENT_REQUEST *request, AGENT_RESULT *result,
		trx_perf_counter_lang_t lang)
{
	char	counterpath[PDH_MAX_COUNTER_PATH], *tmp, *error = NULL;
	int	interval, ret = SYSINFO_RET_FAIL;
	double	value;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", function);

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		goto out;
	}

	tmp = get_rparam(request, 0);

	if (NULL == tmp || '\0' == *tmp)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		goto out;
	}

	strscpy(counterpath, tmp);

	if (NULL == (tmp = get_rparam(request, 1)) || '\0' == *tmp)
	{
		interval = 1;
	}
	else if (FAIL == is_uint31(tmp, &interval))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		goto out;
	}

	if (1 > interval || MAX_COLLECTOR_PERIOD < interval)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Interval out of range."));
		goto out;
	}

	if (FAIL == check_counter_path(counterpath, PERF_COUNTER_LANG_DEFAULT == lang))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid performance counter path."));
		goto out;
	}

	if (SUCCEED != get_perf_counter_value_by_path(counterpath, interval, lang, &value, &error))
	{
		SET_MSG_RESULT(result, error != NULL ? error :
				trx_strdup(NULL, "Cannot obtain performance information from collector."));
		goto out;
	}

	ret = SYSINFO_RET_OK;
	SET_DBL_RESULT(result, value);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", function, trx_result_string(ret));

	return ret;
}

int	PERF_COUNTER(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return perf_counter_ex(__func__, request, result, PERF_COUNTER_LANG_DEFAULT);
}

int	PERF_COUNTER_EN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return perf_counter_ex(__func__, request, result, PERF_COUNTER_LANG_EN);
}
