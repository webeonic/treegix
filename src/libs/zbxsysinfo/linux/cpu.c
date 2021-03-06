

#include "common.h"
#include "sysinfo.h"
#include "stats.h"
#include "log.h"

int	SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*type;
	int	name;
	long	ncpu;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	type = get_rparam(request, 0);

	if (NULL == type || '\0' == *type || 0 == strcmp(type, "online"))
		name = _SC_NPROCESSORS_ONLN;
	else if (0 == strcmp(type, "max"))
		name = _SC_NPROCESSORS_CONF;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (-1 == (ncpu = sysconf(name)))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain number of CPUs: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, ncpu);

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
	{
		cpu_num = TRX_CPUNUM_ALL;
	}
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
	else if (0 == strcmp(tmp, "iowait"))
		state = TRX_CPU_STATE_IOWAIT;
	else if (0 == strcmp(tmp, "interrupt"))
		state = TRX_CPU_STATE_INTERRUPT;
	else if (0 == strcmp(tmp, "softirq"))
		state = TRX_CPU_STATE_SOFTIRQ;
	else if (0 == strcmp(tmp, "steal"))
		state = TRX_CPU_STATE_STEAL;
	else if (0 == strcmp(tmp, "guest"))
		state = TRX_CPU_STATE_GCPU;
	else if (0 == strcmp(tmp, "guest_nice"))
		state = TRX_CPU_STATE_GNICE;
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
	char	*tmp;
	int	mode, per_cpu = 1, cpu_num;
	double	load[TRX_AVG_COUNT], value;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 0);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
	{
		per_cpu = 0;
	}
	else if (0 != strcmp(tmp, "percpu"))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		mode = TRX_AVG1;
	else if (0 == strcmp(tmp, "avg5"))
		mode = TRX_AVG5;
	else if (0 == strcmp(tmp, "avg15"))
		mode = TRX_AVG15;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (mode >= getloadavg(load, 3))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain load average."));
		return SYSINFO_RET_FAIL;
	}

	value = load[mode];

	if (1 == per_cpu)
	{
		if (0 >= (cpu_num = sysconf(_SC_NPROCESSORS_ONLN)))
		{
			SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain number of CPUs: %s",
				trx_strerror(errno)));
			return SYSINFO_RET_FAIL;
		}
		value /= cpu_num;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}

int     SYSTEM_CPU_SWITCHES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int		ret = SYSINFO_RET_FAIL;
	char		line[MAX_STRING_LEN];
	trx_uint64_t	value = 0;
	FILE		*f;

	TRX_UNUSED(request);

	if (NULL == (f = fopen("/proc/stat", "r")))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open /proc/stat: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != fgets(line, sizeof(line), f))
	{
		if (0 != strncmp(line, "ctxt", 4))
			continue;

		if (1 != sscanf(line, "%*s " TRX_FS_UI64, &value))
			continue;

		SET_UI64_RESULT(result, value);
		ret = SYSINFO_RET_OK;
		break;
	}
	trx_fclose(f);

	if (SYSINFO_RET_FAIL == ret)
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot find a line with \"ctxt\" in /proc/stat."));

	return ret;
}

int     SYSTEM_CPU_INTR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int		ret = SYSINFO_RET_FAIL;
	char		line[MAX_STRING_LEN];
	trx_uint64_t	value = 0;
	FILE		*f;

	TRX_UNUSED(request);

	if (NULL == (f = fopen("/proc/stat", "r")))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open /proc/stat: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != fgets(line, sizeof(line), f))
	{
		if (0 != strncmp(line, "intr", 4))
			continue;

		if (1 != sscanf(line, "%*s " TRX_FS_UI64, &value))
			continue;

		SET_UI64_RESULT(result, value);
		ret = SYSINFO_RET_OK;
		break;
	}
	trx_fclose(f);

	if (SYSINFO_RET_FAIL == ret)
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot find a line with \"intr\" in /proc/stat."));

	return ret;
}
