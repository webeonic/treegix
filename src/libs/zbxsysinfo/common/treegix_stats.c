

#include "common.h"
#include "comms.h"
#include "zbxjson.h"

#include "treegix_stats.h"

/******************************************************************************
 *                                                                            *
 * Function: check_response                                                   *
 *                                                                            *
 * Purpose: Check whether JSON response is "success" or "failed"              *
 *                                                                            *
 * Parameters: response - [IN] the request                                    *
 *             result   - [OUT] check result                                  *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static int	check_response(const char *response, AGENT_RESULT *result)
{
	struct zbx_json_parse	jp;
	char			buffer[MAX_STRING_LEN];

	if (SUCCEED != zbx_json_open(response, &jp))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Value should be a JSON object."));
		return FAIL;
	}

	if (SUCCEED != zbx_json_value_by_name(&jp, TRX_PROTO_TAG_RESPONSE, buffer, sizeof(buffer)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find tag: %s.", TRX_PROTO_TAG_RESPONSE));
		return FAIL;
	}

	if (0 != strcmp(buffer, TRX_PROTO_VALUE_SUCCESS))
	{
		if (SUCCEED != zbx_json_value_by_name(&jp, TRX_PROTO_TAG_INFO, buffer, sizeof(buffer)))
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find tag: %s.", TRX_PROTO_TAG_INFO));
		else
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain internal statistics: %s", buffer));

		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: get_remote_treegix_stats                                          *
 *                                                                            *
 * Purpose: send Treegix stats request and receive the result data             *
 *                                                                            *
 * Parameters: json   - [IN] the request                                      *
 *             ip     - [IN] external Treegix instance hostname                *
 *             port   - [IN] external Treegix instance port                    *
 *             result - [OUT] check result                                    *
 *                                                                            *
 ******************************************************************************/
static void	get_remote_treegix_stats(const struct zbx_json *json, const char *ip, unsigned short port,
		AGENT_RESULT *result)
{
	zbx_socket_t	s;

	if (SUCCEED == zbx_tcp_connect(&s, CONFIG_SOURCE_IP, ip, port, CONFIG_TIMEOUT, TRX_TCP_SEC_UNENCRYPTED,
			NULL, NULL))
	{
		if (SUCCEED == zbx_tcp_send(&s, json->buffer))
		{
			if (SUCCEED == zbx_tcp_recv(&s) && NULL != s.buffer)
			{
				if ('\0' == *s.buffer)
				{
					SET_MSG_RESULT(result, zbx_strdup(NULL,
							"Cannot obtain internal statistics: received empty response."));
				}
				else if (SUCCEED == check_response(s.buffer, result))
					set_result_type(result, ITEM_VALUE_TYPE_TEXT, s.buffer);
			}
			else
			{
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain internal statistics: %s",
						zbx_socket_strerror()));
			}
		}
		else
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain internal statistics: %s",
					zbx_socket_strerror()));
		}

		zbx_tcp_close(&s);
	}
	else
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain internal statistics: %s",
				zbx_socket_strerror()));
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_remote_treegix_stats                                      *
 *                                                                            *
 * Purpose: create Treegix stats request                                       *
 *                                                                            *
 * Parameters: ip     - [IN] external Treegix instance hostname                *
 *             port   - [IN] external Treegix instance port                    *
 *             result - [OUT] check result                                    *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	zbx_get_remote_treegix_stats(const char *ip, unsigned short port, AGENT_RESULT *result)
{
	struct zbx_json	json;

	zbx_json_init(&json, TRX_JSON_STAT_BUF_LEN);
	zbx_json_addstring(&json, TRX_PROTO_TAG_REQUEST, TRX_PROTO_VALUE_TREEGIX_STATS, TRX_JSON_TYPE_STRING);

	get_remote_treegix_stats(&json, ip, port, result);

	zbx_json_free(&json);

	return 0 == ISSET_MSG(result) ? SUCCEED : FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_remote_treegix_stats_queue                                *
 *                                                                            *
 * Purpose: create Treegix stats queue request                                 *
 *                                                                            *
 * Parameters: ip     - [IN] external Treegix instance hostname                *
 *             port   - [IN] external Treegix instance port                    *
 *             from   - [IN] lower limit for delay                            *
 *             to     - [IN] upper limit for delay                            *
 *             result - [OUT] check result                                    *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	zbx_get_remote_treegix_stats_queue(const char *ip, unsigned short port, const char *from, const char *to,
		AGENT_RESULT *result)
{
	struct zbx_json	json;

	zbx_json_init(&json, TRX_JSON_STAT_BUF_LEN);
	zbx_json_addstring(&json, TRX_PROTO_TAG_REQUEST, TRX_PROTO_VALUE_TREEGIX_STATS, TRX_JSON_TYPE_STRING);
	zbx_json_addstring(&json, TRX_PROTO_TAG_TYPE, TRX_PROTO_VALUE_TREEGIX_STATS_QUEUE, TRX_JSON_TYPE_STRING);

	zbx_json_addobject(&json, TRX_PROTO_TAG_PARAMS);

	if (NULL != from && '\0' != *from)
		zbx_json_addstring(&json, TRX_PROTO_TAG_FROM, from, TRX_JSON_TYPE_STRING);
	if (NULL != to && '\0' != *to)
		zbx_json_addstring(&json, TRX_PROTO_TAG_TO, to, TRX_JSON_TYPE_STRING);

	zbx_json_close(&json);

	get_remote_treegix_stats(&json, ip, port, result);

	zbx_json_free(&json);

	return 0 == ISSET_MSG(result) ? SUCCEED : FAIL;
}

int	TREEGIX_STATS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	const char	*ip_str, *port_str, *tmp;
	unsigned short	port_number;

	if (5 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (ip_str = get_rparam(request, 0)) || '\0' == *ip_str)
		ip_str = "127.0.0.1";

	if (NULL == (port_str = get_rparam(request, 1)) || '\0' == *port_str)
	{
		port_number = TRX_DEFAULT_SERVER_PORT;
	}
	else if (SUCCEED != is_ushort(port_str, &port_number))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (3 > request->nparam)
	{
		if (SUCCEED != zbx_get_remote_treegix_stats(ip_str, port_number, result))
			return SYSINFO_RET_FAIL;
	}
	else if (0 == strcmp((tmp = get_rparam(request, 2)), TRX_PROTO_VALUE_TREEGIX_STATS_QUEUE))
	{
		if (SUCCEED != zbx_get_remote_treegix_stats_queue(ip_str, port_number, get_rparam(request, 3),
				get_rparam(request, 4), result))
		{
			return SYSINFO_RET_FAIL;
		}
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
