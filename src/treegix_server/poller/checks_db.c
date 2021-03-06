

#include "checks_db.h"

#ifdef HAVE_UNIXODBC

#include "log.h"
#include "../odbc/odbc.h"

/******************************************************************************
 *                                                                            *
 * Function: get_value_db                                                     *
 *                                                                            *
 * Purpose: retrieve data from database                                       *
 *                                                                            *
 * Parameters: item   - [IN] item we are interested in                        *
 *             result - [OUT] check result                                    *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *               NOTSUPPORTED - requested item is not supported               *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
int	get_value_db(DC_ITEM *item, AGENT_RESULT *result)
{
	AGENT_REQUEST		request;
	const char		*dsn;
	trx_odbc_data_source_t	*data_source;
	trx_odbc_query_result_t	*query_result;
	char			*error = NULL;
	int			(*query_result_to_text)(trx_odbc_query_result_t *query_result, char **text, char **error),
				ret = NOTSUPPORTED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() key_orig:'%s' query:'%s'", __func__, item->key_orig, item->params);

	init_request(&request);

	if (SUCCEED != parse_item_key(item->key, &request))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid item key format."));
		goto out;
	}

	if (0 == strcmp(request.key, "db.odbc.select"))
	{
		query_result_to_text = trx_odbc_query_result_to_string;
	}
	else if (0 == strcmp(request.key, "db.odbc.discovery"))
	{
		query_result_to_text = trx_odbc_query_result_to_lld_json;
	}
	else if (0 == strcmp(request.key, "db.odbc.get"))
	{
		query_result_to_text = trx_odbc_query_result_to_json;
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Unsupported item key for this item type."));
		goto out;
	}

	if (2 != request.nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	/* request.params[0] is ignored and is only needed to distinguish queries of same DSN */

	dsn = request.params[1];

	if (NULL == dsn || '\0' == *dsn)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		goto out;
	}

	if (NULL != (data_source = trx_odbc_connect(dsn, item->username, item->password, CONFIG_TIMEOUT, &error)))
	{
		if (NULL != (query_result = trx_odbc_select(data_source, item->params, &error)))
		{
			char	*text = NULL;

			if (SUCCEED == query_result_to_text(query_result, &text, &error))
			{
				SET_TEXT_RESULT(result, text);
				ret = SUCCEED;
			}

			trx_odbc_query_result_free(query_result);
		}

		trx_odbc_data_source_free(data_source);
	}

	if (SUCCEED != ret)
		SET_MSG_RESULT(result, error);
out:
	free_request(&request);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

#endif	/* HAVE_UNIXODBC */
