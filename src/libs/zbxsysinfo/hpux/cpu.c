

#include "common.h"
#include "sysinfo.h"
#include "stats.h"
#include "log.h"

int	SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char			*type;
	struct pst_dynamic	dyn;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	type = get_rparam(request, 0);

	/* only "online" (default) for parameter "type" is supported */
	if (NULL != type && '\0' != *type && 0 != strcmp(type, "online"))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (-1 == pstat_getdynamic(&dyn, sizeof(dyn), 1, 0))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, dyn.psd_proc_cnt);

	return SYSINFO_RET_OK;
}

int	SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*tmp;
	int	cpu_num, state, mode;

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 0);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		cpu_num = TRX_CPUNUM_ALL;
	else if (SUCCEED != is_uint31_1(tmp, &cpu_num))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "user"))
		state = TRX_CPU_STATE_USER;
	else if (0 == strcmp(tmp, "nice"))
		state = TRX_CPU_STATE_NICE;
	else if (0 == strcmp(tmp, "system"))
		state = TRX_CPU_STATE_SYSTEM;
	else if (0 == strcmp(tmp, "idle"))
		state = TRX_CPU_STATE_IDLE;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 2);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		mode = TRX_AVG1;
	else if (0 == strcmp(tmp, "avg5"))
		mode = TRX_AVG5;
	else if (0 == strcmp(tmp, "avg15"))
		mode = TRX_AVG15;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	return get_cpustat(result, cpu_num, state, mode);
}

int	SYSTEM_CPU_LOAD(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char			*tmp;
	struct pst_dynamic	dyn;
	double			value;
	int			per_cpu = 1;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 0);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		per_cpu = 0;
	else if (0 != strcmp(tmp, "percpu"))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (-1 == pstat_getdynamic(&dyn, sizeof(dyn), 1, 0))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		value = dyn.psd_avg_1_min;
	else if (0 == strcmp(tmp, "avg5"))
		value = dyn.psd_avg_5_min;
	else if (0 == strcmp(tmp, "avg15"))
		value = dyn.psd_avg_15_min;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (1 == per_cpu)
	{
		if (0 >= dyn.psd_proc_cnt)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain number of CPUs."));
			return SYSINFO_RET_FAIL;
		}
		value /= dyn.psd_proc_cnt;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}
