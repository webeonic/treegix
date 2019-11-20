

#include "common.h"
#include "proxy.h"
#include "checks_internal.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_get_value_internal_ext                                       *
 *                                                                            *
 * Purpose: processes program type (proxy) specific internal checks           *
 *                                                                            *
 * Parameters: param1  - [IN] the first parameter                             *
 *             request - [IN] the request                                     *
 *             result  - [OUT] the result                                     *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *               NOTSUPPORTED - requested item is not supported               *
 *               FAIL - not a proxy specific internal check                   *
 *                                                                            *
 * Comments: This function is used to process proxy specific internal checks  *
 *           before generic internal checks are processed.                    *
 *                                                                            *
 ******************************************************************************/
int	trx_get_value_internal_ext(const char *param1, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	if (0 == strcmp(param1, "proxy_history"))
	{
		if (1 != get_rparams_num(request))
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			return NOTSUPPORTED;
		}

		SET_UI64_RESULT(result, proxy_get_history_count());
	}
	else
		return FAIL;

	return SUCCEED;
}
