

#include "common.h"
#include "comms.h"
#include "log.h"

#include "trxjson.h"

#include "checks_java.h"

static int	parse_response(AGENT_RESULT *results, int *errcodes, int num, char *response,
		char *error, int max_error_len)
{
	const char		*p;
	struct trx_json_parse	jp, jp_data, jp_row;
	char			*value = NULL;
	size_t			value_alloc = 0;
	int			i, ret = GATEWAY_ERROR;

	if (SUCCEED == trx_json_open(response, &jp))
	{
		if (SUCCEED != trx_json_value_by_name_dyn(&jp, TRX_PROTO_TAG_RESPONSE, &value, &value_alloc))
		{
			trx_snprintf(error, max_error_len, "No '%s' tag in received JSON", TRX_PROTO_TAG_RESPONSE);
			goto exit;
		}

		if (0 == strcmp(value, TRX_PROTO_VALUE_SUCCESS))
		{
			if (SUCCEED != trx_json_brackets_by_name(&jp, TRX_PROTO_TAG_DATA, &jp_data))
			{
				trx_strlcpy(error, "Cannot open data array in received JSON", max_error_len);
				goto exit;
			}

			p = NULL;

			for (i = 0; i < num; i++)
			{
				if (SUCCEED != errcodes[i])
					continue;

				if (NULL == (p = trx_json_next(&jp_data, p)))
				{
					trx_strlcpy(error, "Not all values included in received JSON", max_error_len);
					goto exit;
				}

				if (SUCCEED != trx_json_brackets_open(p, &jp_row))
				{
					trx_strlcpy(error, "Cannot open value object in received JSON", max_error_len);
					goto exit;
				}

				if (SUCCEED == trx_json_value_by_name_dyn(&jp_row, TRX_PROTO_TAG_VALUE, &value, &value_alloc))
				{
					set_result_type(&results[i], ITEM_VALUE_TYPE_TEXT, value);
					errcodes[i] = SUCCEED;
				}
				else if (SUCCEED == trx_json_value_by_name_dyn(&jp_row, TRX_PROTO_TAG_ERROR, &value, &value_alloc))
				{
					SET_MSG_RESULT(&results[i], trx_strdup(NULL, value));
					errcodes[i] = NOTSUPPORTED;
				}
				else
				{
					SET_MSG_RESULT(&results[i], trx_strdup(NULL, "Cannot get item value or error message"));
					errcodes[i] = AGENT_ERROR;
				}
			}

			ret = SUCCEED;
		}
		else if (0 == strcmp(value, TRX_PROTO_VALUE_FAILED))
		{
			if (SUCCEED == trx_json_value_by_name(&jp, TRX_PROTO_TAG_ERROR, error, max_error_len))
				ret = NETWORK_ERROR;
			else
				trx_strlcpy(error, "Cannot get error message describing reasons for failure", max_error_len);

			goto exit;
		}
		else
		{
			trx_snprintf(error, max_error_len, "Bad '%s' tag value '%s' in received JSON",
					TRX_PROTO_TAG_RESPONSE, value);
			goto exit;
		}
	}
	else
	{
		trx_strlcpy(error, "Cannot open received JSON", max_error_len);
		goto exit;
	}
exit:
	trx_free(value);

	return ret;
}

int	get_value_java(unsigned char request, const DC_ITEM *item, AGENT_RESULT *result)
{
	int	errcode = SUCCEED;

	get_values_java(request, item, result, &errcode, 1);

	return errcode;
}

void	get_values_java(unsigned char request, const DC_ITEM *items, AGENT_RESULT *results, int *errcodes, int num)
{
	trx_socket_t	s;
	struct trx_json	json;
	char		error[MAX_STRING_LEN];
	int		i, j, err = SUCCEED;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() jmx_endpoint:'%s' num:%d", __func__, items[0].jmx_endpoint, num);

	for (j = 0; j < num; j++)	/* locate first supported item to use as a reference */
	{
		if (SUCCEED == errcodes[j])
			break;
	}

	if (j == num)	/* all items already NOTSUPPORTED (with invalid key or port) */
		goto out;

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	if (NULL == CONFIG_JAVA_GATEWAY || '\0' == *CONFIG_JAVA_GATEWAY)
	{
		err = GATEWAY_ERROR;
		strscpy(error, "JavaGateway configuration parameter not set or empty");
		goto exit;
	}

	if (TRX_JAVA_GATEWAY_REQUEST_INTERNAL == request)
	{
		trx_json_addstring(&json, TRX_PROTO_TAG_REQUEST, TRX_PROTO_VALUE_JAVA_GATEWAY_INTERNAL,
				TRX_JSON_TYPE_STRING);
	}
	else if (TRX_JAVA_GATEWAY_REQUEST_JMX == request)
	{
		for (i = j + 1; i < num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			if (0 != strcmp(items[j].username, items[i].username) ||
					0 != strcmp(items[j].password, items[i].password) ||
					0 != strcmp(items[j].jmx_endpoint, items[i].jmx_endpoint))
			{
				err = GATEWAY_ERROR;
				strscpy(error, "Java poller received items with different connection parameters");
				goto exit;
			}
		}

		trx_json_addstring(&json, TRX_PROTO_TAG_REQUEST, TRX_PROTO_VALUE_JAVA_GATEWAY_JMX, TRX_JSON_TYPE_STRING);

		if ('\0' != *items[j].username)
		{
			trx_json_addstring(&json, TRX_PROTO_TAG_USERNAME, items[j].username, TRX_JSON_TYPE_STRING);
		}
		if ('\0' != *items[j].password)
		{
			trx_json_addstring(&json, TRX_PROTO_TAG_PASSWORD, items[j].password, TRX_JSON_TYPE_STRING);
		}
		if ('\0' != *items[j].jmx_endpoint)
		{
			trx_json_addstring(&json, TRX_PROTO_TAG_JMX_ENDPOINT, items[j].jmx_endpoint,
					TRX_JSON_TYPE_STRING);
		}
	}
	else
		assert(0);

	trx_json_addarray(&json, TRX_PROTO_TAG_KEYS);
	for (i = j; i < num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		trx_json_addstring(&json, NULL, items[i].key, TRX_JSON_TYPE_STRING);
	}
	trx_json_close(&json);

	if (SUCCEED == (err = trx_tcp_connect(&s, CONFIG_SOURCE_IP, CONFIG_JAVA_GATEWAY, CONFIG_JAVA_GATEWAY_PORT,
			CONFIG_TIMEOUT, TRX_TCP_SEC_UNENCRYPTED, NULL, NULL)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "JSON before sending [%s]", json.buffer);

		if (SUCCEED == (err = trx_tcp_send(&s, json.buffer)))
		{
			if (SUCCEED == (err = trx_tcp_recv(&s)))
			{
				treegix_log(LOG_LEVEL_DEBUG, "JSON back [%s]", s.buffer);

				err = parse_response(results, errcodes, num, s.buffer, error, sizeof(error));
			}
		}

		trx_tcp_close(&s);
	}

	trx_json_free(&json);

	if (FAIL == err)
	{
		strscpy(error, trx_socket_strerror());
		err = GATEWAY_ERROR;
	}
exit:
	if (NETWORK_ERROR == err || GATEWAY_ERROR == err)
	{
		treegix_log(LOG_LEVEL_DEBUG, "getting Java values failed: %s", error);

		for (i = j; i < num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			SET_MSG_RESULT(&results[i], trx_strdup(NULL, error));
			errcodes[i] = err;
		}
	}
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
