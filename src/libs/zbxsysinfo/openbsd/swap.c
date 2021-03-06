

#include "common.h"
#include "sysinfo.h"
#include "log.h"

static int	get_swap_size(trx_uint64_t *total, trx_uint64_t *free, trx_uint64_t *used, double *pfree, double *pused,
		char **error)
{
	int		mib[2];
	size_t		len;
	struct uvmexp	v;

	mib[0] = CTL_VM;
	mib[1] = VM_UVMEXP;

	len = sizeof(v);

	if (0 != sysctl(mib, 2, &v, &len, NULL, 0))
	{
		*error = trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	/* int pagesize;	size of a page (PAGE_SIZE): must be power of 2 */
	/* int swpages;		number of PAGE_SIZE'ed swap pages */
	/* int swpginuse;	number of swap pages in use */

	if (total)
		*total = (trx_uint64_t)v.swpages * v.pagesize;
	if (free)
		*free = (trx_uint64_t)(v.swpages - v.swpginuse) * v.pagesize;
	if (used)
		*used = (trx_uint64_t)v.swpginuse * v.pagesize;
	if (pfree)
		*pfree = v.swpages ? (double)(100.0 * (v.swpages - v.swpginuse)) / v.swpages : 100;
	if (pused)
		*pused = v.swpages ? (double)(100.0 * v.swpginuse) / v.swpages : 0;

	return SYSINFO_RET_OK;
}

static int	SYSTEM_SWAP_TOTAL(AGENT_RESULT *result)
{
	trx_uint64_t	value;
	char		*error;

	if (SYSINFO_RET_OK != get_swap_size(&value, NULL, NULL, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	SYSTEM_SWAP_FREE(AGENT_RESULT *result)
{
	trx_uint64_t	value;
	char		*error;

	if (SYSINFO_RET_OK != get_swap_size(NULL, &value, NULL, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	SYSTEM_SWAP_USED(AGENT_RESULT *result)
{
	trx_uint64_t	value;
	char		*error;

	if (SYSINFO_RET_OK != get_swap_size(NULL, NULL, &value, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	SYSTEM_SWAP_PFREE(AGENT_RESULT *result)
{
	double	value;
	char	*error;

	if (SYSINFO_RET_OK != get_swap_size(NULL, NULL, NULL, &value, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	SYSTEM_SWAP_PUSED(AGENT_RESULT *result)
{
	double	value;
	char	*error;

	if (SYSINFO_RET_OK != get_swap_size(NULL, NULL, NULL, NULL, &value, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}

int	SYSTEM_SWAP_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*swapdev, *mode;
	int	ret;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	swapdev = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	/* default parameter */
	if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* default parameter */
	if (NULL == mode || *mode == '\0' || 0 == strcmp(mode, "free"))
		ret = SYSTEM_SWAP_FREE(result);
	else if (0 == strcmp(mode, "used"))
		ret = SYSTEM_SWAP_USED(result);
	else if (0 == strcmp(mode, "total"))
		ret = SYSTEM_SWAP_TOTAL(result);
	else if (0 == strcmp(mode, "pfree"))
		ret = SYSTEM_SWAP_PFREE(result);
	else if (0 == strcmp(mode, "pused"))
		ret = SYSTEM_SWAP_PUSED(result);
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}

static int	get_swap_io(trx_uint64_t *icount, trx_uint64_t *ipages, trx_uint64_t *ocount, trx_uint64_t *opages,
		char **error)
{
	int		mib[2];
	size_t		len;
	struct uvmexp	v;

	mib[0] = CTL_VM;
	mib[1] = VM_UVMEXP;

	len = sizeof(v);

	if (0 != sysctl(mib, 2, &v, &len, NULL, 0))
	{
		*error = trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	/* int swapins;		swapins */
	/* int swapouts;	swapouts */
	/* int pgswapin;	pages swapped in */
	/* int pgswapout;	pages swapped out */

#if OpenBSD < 201311		/* swapins and swapouts are not supported starting from OpenBSD 5.4 */
	if (NULL != icount)
		*icount = (trx_uint64_t)v.swapins;
	if (NULL != ocount)
		*ocount = (trx_uint64_t)v.swapouts;
#else
	if (NULL != icount || NULL != ocount)
	{
		*error = trx_dsprintf(NULL, "Not supported by the system starting from OpenBSD 5.4.");
		return SYSINFO_RET_FAIL;
	}
#endif
	if (NULL != ipages)
		*ipages = (trx_uint64_t)v.pgswapin;
	if (NULL != opages)
		*opages = (trx_uint64_t)v.pgswapout;

	return SYSINFO_RET_OK;
}

int	SYSTEM_SWAP_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int		ret;
	char		*swapdev, *mode, *error;
	trx_uint64_t	value;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	swapdev = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	/* the only supported parameter */
	if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* default parameter */
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "count"))
	{
		ret = get_swap_io(&value, NULL, NULL, NULL, &error);
	}
	else if (0 == strcmp(mode, "pages"))
	{
		ret = get_swap_io(NULL, &value, NULL, NULL, &error);
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SYSINFO_RET_OK == ret)
		SET_UI64_RESULT(result, value);
	else
		SET_MSG_RESULT(result, error);

	return ret;
}

int	SYSTEM_SWAP_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int		ret;
	char		*swapdev, *mode, *error;
	trx_uint64_t	value;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	swapdev = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	/* the only supported parameter */
	if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* default parameter */
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "count"))
	{
		ret = get_swap_io(NULL, NULL, &value, NULL, &error);
	}
	else if (0 == strcmp(mode, "pages"))
	{
		ret = get_swap_io(NULL, NULL, NULL, &value, &error);
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SYSINFO_RET_OK == ret)
		SET_UI64_RESULT(result, value);
	else
		SET_MSG_RESULT(result, error);


	return ret;
}
