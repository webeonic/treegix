

#include "common.h"
#include "sysinfo.h"
#include "log.h"

#define TRX_PERFSTAT_PAGE_SHIFT	12	/* 4 KB */

int	SYSTEM_SWAP_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef HAVE_LIBPERFSTAT
	perfstat_memory_total_t	mem;
	char			*swapdev, *mode;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	swapdev = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (1 != perfstat_memory_total(NULL, &mem, sizeof(perfstat_memory_total_t), 1))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "free"))
		SET_UI64_RESULT(result, mem.pgsp_free << TRX_PERFSTAT_PAGE_SHIFT);
	else if (0 == strcmp(mode, "total"))
		SET_UI64_RESULT(result, mem.pgsp_total << TRX_PERFSTAT_PAGE_SHIFT);
	else if (0 == strcmp(mode, "used"))
		SET_UI64_RESULT(result, (mem.pgsp_total - mem.pgsp_free) << TRX_PERFSTAT_PAGE_SHIFT);
	else if (0 == strcmp(mode, "pfree"))
		SET_DBL_RESULT(result, mem.pgsp_total ? 100.0 * (mem.pgsp_free / (double)mem.pgsp_total) : 0.0);
	else if (0 == strcmp(mode, "pused"))
		SET_DBL_RESULT(result, mem.pgsp_total ? 100.0 - 100.0 * (mem.pgsp_free / (double)mem.pgsp_total) : 0.0);
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, trx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}
