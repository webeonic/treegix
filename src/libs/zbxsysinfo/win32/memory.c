

#include "common.h"
#include "sysinfo.h"
#include "symbols.h"

int     VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	PERFORMANCE_INFORMATION pfi;
	MEMORYSTATUSEX		ms_ex;
	MEMORYSTATUS		ms;
	char			*mode;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	mode = get_rparam(request, 0);

	if (NULL != mode && 0 == strcmp(mode, "cached"))
	{
		if (NULL == trx_GetPerformanceInfo)
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain system information."));
			return SYSINFO_RET_FAIL;
		}

		trx_GetPerformanceInfo(&pfi, sizeof(PERFORMANCE_INFORMATION));

		SET_UI64_RESULT(result, (trx_uint64_t)pfi.SystemCache * pfi.PageSize);

		return SYSINFO_RET_OK;
	}

	if (NULL != trx_GlobalMemoryStatusEx)
	{
		ms_ex.dwLength = sizeof(MEMORYSTATUSEX);

		trx_GlobalMemoryStatusEx(&ms_ex);

		if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
			SET_UI64_RESULT(result, ms_ex.ullTotalPhys);
		else if (0 == strcmp(mode, "free"))
			SET_UI64_RESULT(result, ms_ex.ullAvailPhys);
		else if (0 == strcmp(mode, "used"))
			SET_UI64_RESULT(result, ms_ex.ullTotalPhys - ms_ex.ullAvailPhys);
		else if (0 == strcmp(mode, "pused") && 0 != ms_ex.ullTotalPhys)
			SET_DBL_RESULT(result, (ms_ex.ullTotalPhys - ms_ex.ullAvailPhys) / (double)ms_ex.ullTotalPhys * 100);
		else if (0 == strcmp(mode, "available"))
			SET_UI64_RESULT(result, ms_ex.ullAvailPhys);
		else if (0 == strcmp(mode, "pavailable") && 0 != ms_ex.ullTotalPhys)
			SET_DBL_RESULT(result, ms_ex.ullAvailPhys / (double)ms_ex.ullTotalPhys * 100);
		else
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
			return SYSINFO_RET_FAIL;
		}
	}
	else
	{
		GlobalMemoryStatus(&ms);

		if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
			SET_UI64_RESULT(result, ms.dwTotalPhys);
		else if (0 == strcmp(mode, "free"))
			SET_UI64_RESULT(result, ms.dwAvailPhys);
		else if (0 == strcmp(mode, "used"))
			SET_UI64_RESULT(result, ms.dwTotalPhys - ms.dwAvailPhys);
		else if (0 == strcmp(mode, "pused") && 0 != ms.dwTotalPhys)
			SET_DBL_RESULT(result, (ms.dwTotalPhys - ms.dwAvailPhys) / (double)ms.dwTotalPhys * 100);
		else if (0 == strcmp(mode, "available"))
			SET_UI64_RESULT(result, ms.dwAvailPhys);
		else if (0 == strcmp(mode, "pavailable") && 0 != ms.dwTotalPhys)
			SET_DBL_RESULT(result, ms.dwAvailPhys / (double)ms.dwTotalPhys * 100);
		else
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
			return SYSINFO_RET_FAIL;
		}
	}

	return SYSINFO_RET_OK;
}
