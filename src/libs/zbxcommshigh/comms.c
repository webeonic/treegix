

#include "common.h"
#include "comms.h"
#include "trxjson.h"
#include "log.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_send_response                                                *
 *                                                                            *
 * Purpose: send json SUCCEED or FAIL to socket along with an info message    *
 *                                                                            *
 * Parameters: sock     - [IN] socket descriptor                              *
 *             result   - [IN] SUCCEED or FAIL                                *
 *             info     - [IN] info message (optional)                        *
 *             version  - [IN] the version data (optional)                    *
 *             protocol - [IN] the transport protocol                         *
 *             timeout - [IN] timeout for this operation                      *
 *                                                                            *
 * Return value: SUCCEED - data successfully transmitted                      *
 *               NETWORK_ERROR - network related error occurred               *
 *                                                                            *
 * Author: Alexander Vladishev, Alexei Vladishev                              *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
int	trx_send_response_ext(trx_socket_t *sock, int result, const char *info, const char *version, int protocol,
		int timeout)
{
	struct trx_json	json;
	const char	*resp;
	int		ret = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	resp = SUCCEED == result ? TRX_PROTO_VALUE_SUCCESS : TRX_PROTO_VALUE_FAILED;

	trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, resp, TRX_JSON_TYPE_STRING);

	if (NULL != info && '\0' != *info)
		trx_json_addstring(&json, TRX_PROTO_TAG_INFO, info, TRX_JSON_TYPE_STRING);

	if (NULL != version)
		trx_json_addstring(&json, TRX_PROTO_TAG_VERSION, version, TRX_JSON_TYPE_STRING);

	treegix_log(LOG_LEVEL_DEBUG, "%s() '%s'", __func__, json.buffer);

	if (FAIL == (ret = trx_tcp_send_ext(sock, json.buffer, strlen(json.buffer), protocol, timeout)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "Error sending result back: %s", trx_socket_strerror());
		ret = NETWORK_ERROR;
	}

	trx_json_free(&json);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_recv_response                                                *
 *                                                                            *
 * Purpose: read a response message (in JSON format) from socket, optionally  *
 *          extract "info" value.                                             *
 *                                                                            *
 * Parameters: sock    - [IN] socket descriptor                               *
 *             timeout - [IN] timeout for this operation                      *
 *             error   - [OUT] pointer to error message                       *
 *                                                                            *
 * Return value: SUCCEED - "response":"success" successfully retrieved        *
 *               FAIL    - otherwise                                          *
 * Comments:                                                                  *
 *     Allocates memory.                                                      *
 *                                                                            *
 *     If an error occurs, the function allocates dynamic memory for an error *
 *     message and writes its address into location pointed to by "error"     *
 *     parameter.                                                             *
 *                                                                            *
 *     When the "info" value is present in the response message then function *
 *     copies the "info" value into the "error" buffer as additional          *
 *     information                                                            *
 *                                                                            *
 *     IMPORTANT: it is a responsibility of the caller to release the         *
 *                "error" memory !                                            *
 *                                                                            *
 ******************************************************************************/
int	trx_recv_response(trx_socket_t *sock, int timeout, char **error)
{
	struct trx_json_parse	jp;
	char			value[16];
	int			ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != trx_tcp_recv_to(sock, timeout))
	{
		/* since we have successfully sent data earlier, we assume the other */
		/* side is just too busy processing our data if there is no response */
		*error = trx_strdup(*error, trx_socket_strerror());
		goto out;
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s() '%s'", __func__, sock->buffer);

	/* deal with empty string here because trx_json_open() does not produce an error message in this case */
	if ('\0' == *sock->buffer)
	{
		*error = trx_strdup(*error, "empty string received");
		goto out;
	}

	if (SUCCEED != trx_json_open(sock->buffer, &jp))
	{
		*error = trx_strdup(*error, trx_json_strerror());
		goto out;
	}

	if (SUCCEED != trx_json_value_by_name(&jp, TRX_PROTO_TAG_RESPONSE, value, sizeof(value)))
	{
		*error = trx_strdup(*error, "no \"" TRX_PROTO_TAG_RESPONSE "\" tag");
		goto out;
	}

	if (0 != strcmp(value, TRX_PROTO_VALUE_SUCCESS))
	{
		char	*info = NULL;
		size_t	info_alloc = 0;

		if (SUCCEED == trx_json_value_by_name_dyn(&jp, TRX_PROTO_TAG_INFO, &info, &info_alloc))
			*error = trx_strdup(*error, info);
		else
			*error = trx_dsprintf(*error, "negative response \"%s\"", value);
		trx_free(info);
		goto out;
	}

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}
