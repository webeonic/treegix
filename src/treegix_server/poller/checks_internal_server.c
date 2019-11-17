

#include "common.h"
#include "valuecache.h"
#include "preproc.h"
#include "zbxlld.h"
#include "checks_internal.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_value_internal_ext                                       *
 *                                                                            *
 * Purpose: processes program type (server) specific internal checks          *
 *                                                                            *
 * Parameters: param1  - [IN] the first parameter                             *
 *             request - [IN] the request                                     *
 *             result  - [OUT] the result                                     *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *               NOTSUPPORTED - requested item is not supported               *
 *               FAIL - not a server specific internal check                  *
 *                                                                            *
 * Comments: This function is used to process server specific internal checks *
 *           before generic internal checks are processed.                    *
 *                                                                            *
 ******************************************************************************/
int	zbx_get_value_internal_ext(const char *param1, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	nparams, ret = NOTSUPPORTED;
	char	*param2, *param3;

	nparams = get_rparams_num(request);

	if (0 == strcmp(param1, "triggers"))			/* treegix["triggers"] */
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_UI64_RESULT(result, DCget_trigger_count());
	}
	else if (0 == strcmp(param1, "history") ||		/* treegix["history"] */
			0 == strcmp(param1, "history_log") ||	/* treegix["history_log"] */
			0 == strcmp(param1, "history_str") ||	/* treegix["history_str"] */
			0 == strcmp(param1, "history_text") ||	/* treegix["history_text"] */
			0 == strcmp(param1, "history_uint"))	/* treegix["history_uint"] */
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_UI64_RESULT(result, DBget_row_count(param1));
	}
	else if (0 == strcmp(param1, "trends") ||			/* treegix["trends"] */
			0 == strcmp(param1, "trends_uint"))	/* treegix["trends_uint"] */
	{
		if (1 != nparams)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		SET_UI64_RESULT(result, DBget_row_count(param1));
	}
	else if (0 == strcmp(param1, "proxy"))			/* treegix["proxy",<hostname>,"lastaccess"] */
	{
		int	lastaccess;
		char	*error = NULL;

		/* this item is always processed by server */

		if (3 != nparams)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		param2 = get_rparam(request, 2);
		if ('\0' == *param2 || 0 != strcmp(param2, "lastaccess"))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
			goto out;
		}

		if (FAIL == DBget_proxy_lastaccess(get_rparam(request, 1), &lastaccess, &error))
		{
			SET_MSG_RESULT(result, error);
			goto out;
		}

		SET_UI64_RESULT(result, lastaccess);
	}
	else if (0 == strcmp(param1, "vcache"))
	{
		zbx_vc_stats_t	stats;

		if (FAIL == zbx_vc_get_statistics(&stats))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Value cache is disabled."));
			goto out;
		}

		if (2 > nparams || nparams > 3)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		param2 = get_rparam(request, 1);
		if (NULL == (param3 = get_rparam(request, 2)))
			param3 = "";

		if (0 == strcmp(param2, "buffer"))
		{
			if (0 == strcmp(param3, "free"))
				SET_UI64_RESULT(result, stats.free_size);
			else if (0 == strcmp(param3, "pfree"))
				SET_DBL_RESULT(result, (double)stats.free_size / stats.total_size * 100);
			else if (0 == strcmp(param3, "total"))
				SET_UI64_RESULT(result, stats.total_size);
			else if (0 == strcmp(param3, "used"))
				SET_UI64_RESULT(result, stats.total_size - stats.free_size);
			else if (0 == strcmp(param3, "pused"))
				SET_DBL_RESULT(result, (double)(stats.total_size - stats.free_size) /
						stats.total_size * 100);
			else
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
				goto out;
			}
		}
		else if (0 == strcmp(param2, "cache"))
		{
			if (0 == strcmp(param3, "hits"))
				SET_UI64_RESULT(result, stats.hits);
			else if (0 == strcmp(param3, "requests"))
				SET_UI64_RESULT(result, stats.hits + stats.misses);
			else if (0 == strcmp(param3, "misses"))
				SET_UI64_RESULT(result, stats.misses);
			else if (0 == strcmp(param3, "mode"))
				SET_UI64_RESULT(result, stats.mode);
			else
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
				goto out;
			}
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
			goto out;
		}
	}
	else if (0 == strcmp(param1, "lld_queue"))
	{
		zbx_uint64_t	value;
		char		*error = NULL;

		if (1 != nparams)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
			goto out;
		}

		if (FAIL == zbx_lld_get_queue_size(&value, &error))
		{
			SET_MSG_RESULT(result, error);
			goto out;
		}

		SET_UI64_RESULT(result, value);
	}
	else
	{
		ret = FAIL;
		goto out;
	}

	ret = SUCCEED;
out:
	return ret;
}
