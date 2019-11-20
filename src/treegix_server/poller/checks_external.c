

#include "common.h"
#include "log.h"
#include "trxexec.h"

#include "checks_external.h"

extern char	*CONFIG_EXTERNALSCRIPTS;

/******************************************************************************
 *                                                                            *
 * Function: get_value_external                                               *
 *                                                                            *
 * Purpose: retrieve data from script executed on Treegix server               *
 *                                                                            *
 * Parameters: item - item we are interested in                               *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *                         and result_str (as string)                         *
 *               NOTSUPPORTED - requested item is not supported               *
 *                                                                            *
 * Author: Mike Nestor, rewritten by Alexander Vladishev                      *
 *                                                                            *
 ******************************************************************************/
int	get_value_external(DC_ITEM *item, AGENT_RESULT *result)
{
	char		error[ITEM_ERROR_LEN_MAX], *cmd = NULL, *buf = NULL;
	size_t		cmd_alloc = TRX_KIBIBYTE, cmd_offset = 0;
	int		i, ret = NOTSUPPORTED;
	AGENT_REQUEST	request;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __func__, item->key);

	init_request(&request);

	if (SUCCEED != parse_item_key(item->key, &request))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid item key format."));
		goto out;
	}

	cmd = (char *)trx_malloc(cmd, cmd_alloc);
	trx_snprintf_alloc(&cmd, &cmd_alloc, &cmd_offset, "%s/%s", CONFIG_EXTERNALSCRIPTS, get_rkey(&request));

	if (-1 == access(cmd, X_OK))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "%s: %s", cmd, trx_strerror(errno)));
		goto out;
	}

	for (i = 0; i < get_rparams_num(&request); i++)
	{
		const char	*param;
		char		*param_esc;

		param = get_rparam(&request, i);

		param_esc = trx_dyn_escape_shell_single_quote(param);
		trx_snprintf_alloc(&cmd, &cmd_alloc, &cmd_offset, " '%s'", param_esc);
		trx_free(param_esc);
	}

	if (SUCCEED == trx_execute(cmd, &buf, error, sizeof(error), CONFIG_TIMEOUT, TRX_EXIT_CODE_CHECKS_DISABLED))
	{
		trx_rtrim(buf, TRX_WHITESPACE);

		set_result_type(result, ITEM_VALUE_TYPE_TEXT, buf);
		trx_free(buf);

		ret = SUCCEED;
	}
	else
		SET_MSG_RESULT(result, trx_strdup(NULL, error));
out:
	trx_free(cmd);

	free_request(&request);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}
