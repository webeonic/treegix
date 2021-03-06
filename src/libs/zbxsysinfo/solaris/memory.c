

#include "common.h"
#include "sysinfo.h"

static int	VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
	SET_UI64_RESULT(result, (trx_uint64_t)sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE));

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_FREE(AGENT_RESULT *result)
{
	SET_UI64_RESULT(result, (trx_uint64_t)sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGESIZE));

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_USED(AGENT_RESULT *result)
{
	trx_uint64_t	used;

	used = sysconf(_SC_PHYS_PAGES) - sysconf(_SC_AVPHYS_PAGES);

	SET_UI64_RESULT(result, used * sysconf(_SC_PAGESIZE));

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_PUSED(AGENT_RESULT *result)
{
	trx_uint64_t	used, total;

	if (0 == (total = sysconf(_SC_PHYS_PAGES)))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	used = total - sysconf(_SC_AVPHYS_PAGES);

	SET_DBL_RESULT(result, used / (double)total * 100);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
	SET_UI64_RESULT(result, (trx_uint64_t)sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGESIZE));

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
	trx_uint64_t	total;

	if (0 == (total = sysconf(_SC_PHYS_PAGES)))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, sysconf(_SC_AVPHYS_PAGES) / (double)total * 100);

	return SYSINFO_RET_OK;
}

int     VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*mode;
	int	ret;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	mode = get_rparam(request, 0);

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		ret = VM_MEMORY_TOTAL(result);
	else if (0 == strcmp(mode, "free"))
		ret = VM_MEMORY_FREE(result);
	else if (0 == strcmp(mode, "used"))
		ret = VM_MEMORY_USED(result);
	else if (0 == strcmp(mode, "pused"))
		ret = VM_MEMORY_PUSED(result);
	else if (0 == strcmp(mode, "available"))
		ret = VM_MEMORY_AVAILABLE(result);
	else if (0 == strcmp(mode, "pavailable"))
		ret = VM_MEMORY_PAVAILABLE(result);
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}
