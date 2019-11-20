

#include "common.h"
#include "trxjson.h"
#include "comms.h"

#include "treegix_sender.h"

const char	*progname = NULL;
const char	title_message[] = "";
const char	*usage_message[] = {NULL};

const char	*help_message[] = {NULL};

unsigned char	program_type	= TRX_PROGRAM_TYPE_SENDER;

int	treegix_sender_send_values(const char *address, unsigned short port, const char *source,
		const treegix_sender_value_t *values, int count, char **result)
{
	trx_socket_t	sock;
	int		ret, i;
	struct trx_json	json;

	if (1 > count)
	{
		if (NULL != result)
			*result = trx_strdup(NULL, "values array must have at least one item");

		return FAIL;
	}

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);
	trx_json_addstring(&json, TRX_PROTO_TAG_REQUEST, TRX_PROTO_VALUE_SENDER_DATA, TRX_JSON_TYPE_STRING);
	trx_json_addarray(&json, TRX_PROTO_TAG_DATA);

	for (i = 0; i < count; i++)
	{
		trx_json_addobject(&json, NULL);
		trx_json_addstring(&json, TRX_PROTO_TAG_HOST, values[i].host, TRX_JSON_TYPE_STRING);
		trx_json_addstring(&json, TRX_PROTO_TAG_KEY, values[i].key, TRX_JSON_TYPE_STRING);
		trx_json_addstring(&json, TRX_PROTO_TAG_VALUE, values[i].value, TRX_JSON_TYPE_STRING);
		trx_json_close(&json);
	}
	trx_json_close(&json);

	if (SUCCEED == (ret = trx_tcp_connect(&sock, source, address, port, GET_SENDER_TIMEOUT,
			TRX_TCP_SEC_UNENCRYPTED, NULL, NULL)))
	{
		if (SUCCEED == (ret = trx_tcp_send(&sock, json.buffer)))
		{
			if (SUCCEED == (ret = trx_tcp_recv(&sock)))
			{
				if (NULL != result)
					*result = trx_strdup(NULL, sock.buffer);
			}
		}

		trx_tcp_close(&sock);
	}

	if (FAIL == ret && NULL != result)
		*result = trx_strdup(NULL, trx_socket_strerror());

	trx_json_free(&json);

	return ret;
}

int	treegix_sender_parse_result(const char *result, int *response, treegix_sender_info_t *info)
{
	int			ret;
	struct trx_json_parse	jp;
	char			value[MAX_STRING_LEN];

	if (SUCCEED != (ret = trx_json_open(result, &jp)))
		goto out;

	if (SUCCEED != (ret = trx_json_value_by_name(&jp, TRX_PROTO_TAG_RESPONSE, value, sizeof(value))))
		goto out;

	*response = (0 == strcmp(value, TRX_PROTO_VALUE_SUCCESS)) ? 0 : -1;

	if (NULL == info)
		goto out;

	if (SUCCEED != trx_json_value_by_name(&jp, TRX_PROTO_TAG_INFO, value, sizeof(value)) ||
			3 != sscanf(value, "processed: %*d; failed: %d; total: %d; seconds spent: %lf",
				&info->failed, &info->total, &info->time_spent))
	{
		info->total = -1;
	}
out:
	return ret;
}

void	treegix_sender_free_result(void *ptr)
{
	if (NULL != ptr)
		free(ptr);
}
