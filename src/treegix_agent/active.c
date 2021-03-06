

#include "common.h"
#include "active.h"
#include "trxconf.h"

#include "cfg.h"
#include "log.h"
#include "sysinfo.h"
#include "logfiles/logfiles.h"
#ifdef _WINDOWS
#	include "eventlog.h"
#	include <delayimp.h>
#endif
#include "comms.h"
#include "threads.h"
#include "trxjson.h"
#include "alias.h"
#include "metrics.h"

extern unsigned char			program_type;
extern TRX_THREAD_LOCAL unsigned char	process_type;
extern TRX_THREAD_LOCAL int		server_num, process_num;

#if defined(TREEGIX_SERVICE)
#	include "service.h"
#elif defined(TREEGIX_DAEMON)
#	include "daemon.h"
#endif

#include "../libs/trxcrypto/tls.h"

static TRX_THREAD_LOCAL TRX_ACTIVE_BUFFER	buffer;
static TRX_THREAD_LOCAL trx_vector_ptr_t	active_metrics;
static TRX_THREAD_LOCAL trx_vector_ptr_t	regexps;
static TRX_THREAD_LOCAL char			*session_token;
static TRX_THREAD_LOCAL trx_uint64_t		last_valueid = 0;

#ifdef _WINDOWS
LONG WINAPI	DelayLoadDllExceptionFilter(PEXCEPTION_POINTERS excpointers)
{
	LONG		disposition = EXCEPTION_EXECUTE_HANDLER;
	PDelayLoadInfo	delayloadinfo = (PDelayLoadInfo)(excpointers->ExceptionRecord->ExceptionInformation[0]);

	switch (excpointers->ExceptionRecord->ExceptionCode)
	{
		case VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND):
			treegix_log(LOG_LEVEL_DEBUG, "function %s was not found in %s",
					delayloadinfo->dlp.szProcName, delayloadinfo->szDll);
			break;
		case VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND):
			if (delayloadinfo->dlp.fImportByName)
			{
				treegix_log(LOG_LEVEL_DEBUG, "function %s was not found in %s",
						delayloadinfo->dlp.szProcName, delayloadinfo->szDll);
			}
			else
			{
				treegix_log(LOG_LEVEL_DEBUG, "function ordinal %d was not found in %s",
						delayloadinfo->dlp.dwOrdinal, delayloadinfo->szDll);
			}
			break;
		default:
			disposition = EXCEPTION_CONTINUE_SEARCH;
			break;
	}

	return disposition;
}
#endif

static void	init_active_metrics(void)
{
	size_t	sz;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (NULL == buffer.data)
	{
		treegix_log(LOG_LEVEL_DEBUG, "buffer: first allocation for %d elements", CONFIG_BUFFER_SIZE);
		sz = CONFIG_BUFFER_SIZE * sizeof(TRX_ACTIVE_BUFFER_ELEMENT);
		buffer.data = (TRX_ACTIVE_BUFFER_ELEMENT *)trx_malloc(buffer.data, sz);
		memset(buffer.data, 0, sz);
		buffer.count = 0;
		buffer.pcount = 0;
		buffer.lastsent = (int)time(NULL);
		buffer.first_error = 0;
	}

	trx_vector_ptr_create(&active_metrics);
	trx_vector_ptr_create(&regexps);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

static void	free_active_metric(TRX_ACTIVE_METRIC *metric)
{
	int	i;

	trx_free(metric->key);
	trx_free(metric->key_orig);

	for (i = 0; i < metric->logfiles_num; i++)
		trx_free(metric->logfiles[i].filename);

	trx_free(metric->logfiles);
	trx_free(metric);
}

#ifdef _WINDOWS
static void	free_active_metrics(void)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_regexp_clean_expressions(&regexps);
	trx_vector_ptr_destroy(&regexps);

	trx_vector_ptr_clear_ext(&active_metrics, (trx_clean_func_t)free_active_metric);
	trx_vector_ptr_destroy(&active_metrics);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
#endif

static int	metric_ready_to_process(const TRX_ACTIVE_METRIC *metric)
{
	if (ITEM_STATE_NOTSUPPORTED == metric->state && 0 == metric->refresh_unsupported)
		return FAIL;

	return SUCCEED;
}

static int	get_min_nextcheck(void)
{
	int	i, min = -1;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < active_metrics.values_num; i++)
	{
		const TRX_ACTIVE_METRIC	*metric = (const TRX_ACTIVE_METRIC *)active_metrics.values[i];

		if (SUCCEED != metric_ready_to_process(metric))
			continue;

		if (metric->nextcheck < min || -1 == min)
			min = metric->nextcheck;
	}

	if (-1 == min)
		min = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __func__, min);

	return min;
}

static void	add_check(const char *key, const char *key_orig, int refresh, trx_uint64_t lastlogsize, int mtime)
{
	TRX_ACTIVE_METRIC	*metric;
	int			i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s' refresh:%d lastlogsize:" TRX_FS_UI64 " mtime:%d",
			__func__, key, refresh, lastlogsize, mtime);

	for (i = 0; i < active_metrics.values_num; i++)
	{
		metric = (TRX_ACTIVE_METRIC *)active_metrics.values[i];

		if (0 != strcmp(metric->key_orig, key_orig))
			continue;

		if (0 != strcmp(metric->key, key))
		{
			int	j;

			trx_free(metric->key);
			metric->key = trx_strdup(NULL, key);
			metric->lastlogsize = lastlogsize;
			metric->mtime = mtime;
			metric->big_rec = 0;
			metric->use_ino = 0;
			metric->error_count = 0;

			for (j = 0; j < metric->logfiles_num; j++)
				trx_free(metric->logfiles[j].filename);

			trx_free(metric->logfiles);
			metric->logfiles_num = 0;
			metric->start_time = 0.0;
			metric->processed_bytes = 0;
		}

		/* replace metric */
		if (metric->refresh != refresh)
		{
			metric->nextcheck = 0;
			metric->refresh = refresh;
		}

		if (ITEM_STATE_NOTSUPPORTED == metric->state)
		{
			/* Currently receiving list of active checks works as a signal to refresh unsupported */
			/* items. Hopefully in the future this will be controlled by server (TRXNEXT-2633). */
			metric->refresh_unsupported = 1;
			metric->start_time = 0.0;
			metric->processed_bytes = 0;
		}

		goto out;
	}

	metric = (TRX_ACTIVE_METRIC *)trx_malloc(NULL, sizeof(TRX_ACTIVE_METRIC));

	/* add new metric */
	metric->key = trx_strdup(NULL, key);
	metric->key_orig = trx_strdup(NULL, key_orig);
	metric->refresh = refresh;
	metric->nextcheck = 0;
	metric->state = ITEM_STATE_NORMAL;
	metric->refresh_unsupported = 0;
	metric->lastlogsize = lastlogsize;
	metric->mtime = mtime;
	/* existing log[], log.count[] and eventlog[] data can be skipped */
	metric->skip_old_data = (0 != metric->lastlogsize ? 0 : 1);
	metric->big_rec = 0;
	metric->use_ino = 0;
	metric->error_count = 0;
	metric->logfiles_num = 0;
	metric->logfiles = NULL;
	metric->flags = TRX_METRIC_FLAG_NEW;

	if ('l' == metric->key[0] && 'o' == metric->key[1] && 'g' == metric->key[2])
	{
		if ('[' == metric->key[3])					/* log[ */
			metric->flags |= TRX_METRIC_FLAG_LOG_LOG;
		else if (0 == strncmp(metric->key + 3, "rt[", 3))		/* logrt[ */
			metric->flags |= TRX_METRIC_FLAG_LOG_LOGRT;
		else if (0 == strncmp(metric->key + 3, ".count[", 7))		/* log.count[ */
			metric->flags |= TRX_METRIC_FLAG_LOG_LOG | TRX_METRIC_FLAG_LOG_COUNT;
		else if (0 == strncmp(metric->key + 3, "rt.count[", 9))		/* logrt.count[ */
			metric->flags |= TRX_METRIC_FLAG_LOG_LOGRT | TRX_METRIC_FLAG_LOG_COUNT;
	}
	else if (0 == strncmp(metric->key, "eventlog[", 9))
		metric->flags |= TRX_METRIC_FLAG_LOG_EVENTLOG;

	metric->start_time = 0.0;
	metric->processed_bytes = 0;

	trx_vector_ptr_append(&active_metrics, metric);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: mode_parameter_is_skip                                           *
 *                                                                            *
 * Purpose: test log[] or log.count[] item key if <mode> parameter is set to  *
 *          'skip'                                                            *
 *                                                                            *
 * Return value: SUCCEED - <mode> parameter is set to 'skip'                  *
 *               FAIL - <mode> is not 'skip' or error                         *
 *                                                                            *
 ******************************************************************************/
static int	mode_parameter_is_skip(unsigned char flags, const char *itemkey)
{
	AGENT_REQUEST	request;
	const char	*skip;
	int		ret = FAIL, max_num_parameters;

	if (0 == (TRX_METRIC_FLAG_LOG_COUNT & flags))	/* log[] */
		max_num_parameters = 7;
	else						/* log.count[] */
		max_num_parameters = 6;

	init_request(&request);

	if (SUCCEED == parse_item_key(itemkey, &request) && 0 < get_rparams_num(&request) &&
			max_num_parameters >= get_rparams_num(&request) && NULL != (skip = get_rparam(&request, 4)) &&
			0 == strcmp(skip, "skip"))
	{
		ret = SUCCEED;
	}

	free_request(&request);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_list_of_checks                                             *
 *                                                                            *
 * Purpose: Parse list of active checks received from server                  *
 *                                                                            *
 * Parameters: str  - NULL terminated string received from server             *
 *             host - address of host                                         *
 *             port - port number on host                                     *
 *                                                                            *
 * Return value: returns SUCCEED on successful parsing,                       *
 *               FAIL on an incorrect format of string                        *
 *                                                                            *
 * Author: Eugene Grigorjev, Alexei Vladishev (new json protocol)             *
 *                                                                            *
 * Comments:                                                                  *
 *    String represented as "TRX_EOF" termination list                        *
 *    With '\n' delimiter between elements.                                   *
 *    Each element represented as:                                            *
 *           <key>:<refresh time>:<last log size>:<modification time>         *
 *                                                                            *
 ******************************************************************************/
static int	parse_list_of_checks(char *str, const char *host, unsigned short port)
{
	const char		*p;
	char			name[MAX_STRING_LEN], key_orig[MAX_STRING_LEN], expression[MAX_STRING_LEN],
				tmp[MAX_STRING_LEN], exp_delimiter;
	trx_uint64_t		lastlogsize;
	struct trx_json_parse	jp;
	struct trx_json_parse	jp_data, jp_row;
	TRX_ACTIVE_METRIC	*metric;
	trx_vector_str_t	received_metrics;
	int			delay, mtime, expression_type, case_sensitive, i, j, ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_str_create(&received_metrics);

	if (SUCCEED != trx_json_open(str, &jp))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", trx_json_strerror());
		goto out;
	}

	if (SUCCEED != trx_json_value_by_name(&jp, TRX_PROTO_TAG_RESPONSE, tmp, sizeof(tmp)))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", trx_json_strerror());
		goto out;
	}

	if (0 != strcmp(tmp, TRX_PROTO_VALUE_SUCCESS))
	{
		if (SUCCEED == trx_json_value_by_name(&jp, TRX_PROTO_TAG_INFO, tmp, sizeof(tmp)))
			treegix_log(LOG_LEVEL_WARNING, "no active checks on server [%s:%hu]: %s", host, port, tmp);
		else
			treegix_log(LOG_LEVEL_WARNING, "no active checks on server");

		goto out;
	}

	if (SUCCEED != trx_json_brackets_by_name(&jp, TRX_PROTO_TAG_DATA, &jp_data))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", trx_json_strerror());
		goto out;
	}

 	p = NULL;
	while (NULL != (p = trx_json_next(&jp_data, p)))
	{
/* {"data":[{"key":"system.cpu.num",...,...},{...},...]}
 *          ^------------------------------^
 */ 		if (SUCCEED != trx_json_brackets_open(p, &jp_row))
		{
			treegix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", trx_json_strerror());
			goto out;
		}

		if (SUCCEED != trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_KEY, name, sizeof(name)) || '\0' == *name)
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", TRX_PROTO_TAG_KEY);
			continue;
		}

		if (SUCCEED != trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_KEY_ORIG, key_orig, sizeof(key_orig))
				|| '\0' == *key_orig) {
			trx_strlcpy(key_orig, name, sizeof(key_orig));
		}

		if (SUCCEED != trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_DELAY, tmp, sizeof(tmp)) || '\0' == *tmp)
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", TRX_PROTO_TAG_DELAY);
			continue;
		}

		delay = atoi(tmp);

		if (SUCCEED != trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_LASTLOGSIZE, tmp, sizeof(tmp)) ||
				SUCCEED != is_uint64(tmp, &lastlogsize))
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", TRX_PROTO_TAG_LASTLOGSIZE);
			continue;
		}

		if (SUCCEED != trx_json_value_by_name(&jp_row, TRX_PROTO_TAG_MTIME, tmp, sizeof(tmp)) || '\0' == *tmp)
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", TRX_PROTO_TAG_MTIME);
			mtime = 0;
		}
		else
			mtime = atoi(tmp);

		add_check(trx_alias_get(name), key_orig, delay, lastlogsize, mtime);

		/* remember what was received */
		trx_vector_str_append(&received_metrics, trx_strdup(NULL, key_orig));
	}

	/* remove what wasn't received */
	for (i = 0; i < active_metrics.values_num; i++)
	{
		int	found = 0;

		metric = (TRX_ACTIVE_METRIC *)active_metrics.values[i];

		/* 'Do-not-delete' exception for log[] and log.count[] items with <mode> parameter set to 'skip'. */
		/* We need to keep their state, namely 'skip_old_data', in case the items become NOTSUPPORTED as */
		/* server might not send them in a new active check list. */

		if (0 != (TRX_METRIC_FLAG_LOG_LOG & metric->flags) && ITEM_STATE_NOTSUPPORTED == metric->state &&
				0 == metric->skip_old_data && SUCCEED == mode_parameter_is_skip(metric->flags,
				metric->key))
		{
			continue;
		}

		for (j = 0; j < received_metrics.values_num; j++)
		{
			if (0 == strcmp(metric->key_orig, received_metrics.values[j]))
			{
				found = 1;
				break;
			}
		}

		if (0 == found)
		{
			trx_vector_ptr_remove_noorder(&active_metrics, i);
			free_active_metric(metric);
			i--;	/* consider the same index on the next run */
		}
	}

	trx_regexp_clean_expressions(&regexps);

	if (SUCCEED == trx_json_brackets_by_name(&jp, TRX_PROTO_TAG_REGEXP, &jp_data))
	{
	 	p = NULL;
		while (NULL != (p = trx_json_next(&jp_data, p)))
		{
/* {"regexp":[{"name":"regexp1",...,...},{...},...]}
 *            ^------------------------^
 */			if (SUCCEED != trx_json_brackets_open(p, &jp_row))
			{
				treegix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", trx_json_strerror());
				goto out;
			}

			if (SUCCEED != trx_json_value_by_name(&jp_row, "name", name, sizeof(name)))
			{
				treegix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "name");
				continue;
			}

			if (SUCCEED != trx_json_value_by_name(&jp_row, "expression", expression, sizeof(expression)) ||
					'\0' == *expression)
			{
				treegix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "expression");
				continue;
			}

			if (SUCCEED != trx_json_value_by_name(&jp_row, "expression_type", tmp, sizeof(tmp)) ||
					'\0' == *tmp)
			{
				treegix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "expression_type");
				continue;
			}

			expression_type = atoi(tmp);

			if (SUCCEED != trx_json_value_by_name(&jp_row, "exp_delimiter", tmp, sizeof(tmp)))
			{
				treegix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "exp_delimiter");
				continue;
			}

			exp_delimiter = tmp[0];

			if (SUCCEED != trx_json_value_by_name(&jp_row, "case_sensitive", tmp,
					sizeof(tmp)) || '\0' == *tmp)
			{
				treegix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "case_sensitive");
				continue;
			}

			case_sensitive = atoi(tmp);

			add_regexp_ex(&regexps, name, expression, expression_type, exp_delimiter, case_sensitive);
		}
	}

	ret = SUCCEED;
out:
	trx_vector_str_clear_ext(&received_metrics, trx_str_free);
	trx_vector_str_destroy(&received_metrics);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/*********************************************************************************
 *                                                                               *
 * Function: process_config_item                                                 *
 *                                                                               *
 * Purpose: process configuration item and set it value to respective parameter  *
 *                                                                               *
 * Parameters: json   - pointer to JSON structure where to put resulting value   *
 *             config - pointer to configuration parameter                       *
 *             length - length of configuration parameter                        *
 *             proto  - configuration parameter prototype                        *
 *                                                                               *
 ********************************************************************************/
static void process_config_item(struct trx_json *json, char *config, size_t length, const char *proto)
{
	char		**value;
	AGENT_RESULT	result;
	const char	*config_name;
	const char	*config_type;

	if (CONFIG_HOST_METADATA_ITEM == config)
	{
		config_name = "HostMetadataItem";
		config_type = "metadata";
	}
	else /* CONFIG_HOST_INTERFACE_ITEM */
	{
		config_name = "HostInterfaceItem";
		config_type = "interface";
	}

	init_result(&result);

	if (SUCCEED == process(config, PROCESS_LOCAL_COMMAND | PROCESS_WITH_ALIAS, &result) &&
			NULL != (value = GET_STR_RESULT(&result)) && NULL != *value)
	{
		if (SUCCEED != trx_is_utf8(*value))
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot get host %s using \"%s\" item specified by"
					" \"%s\" configuration parameter: returned value is not"
					" an UTF-8 string",config_type, config, config_name);
		}
		else
		{
			if (length < trx_strlen_utf8(*value))
			{
				size_t	bytes;

				treegix_log(LOG_LEVEL_WARNING, "the returned value of \"%s\" item specified by"
						" \"%s\" configuration parameter is too long,"
						" using first %d characters", config, config_name, (int)length);

				bytes = trx_strlen_utf8_nchars(*value, length);
				(*value)[bytes] = '\0';
			}
			trx_json_addstring(json, proto, *value, TRX_JSON_TYPE_STRING);
		}
	}
	else
		treegix_log(LOG_LEVEL_WARNING, "cannot get host %s using \"%s\" item specified by"
				" \"%s\" configuration parameter",config_type, config,config_name);

	free_result(&result);
}

/******************************************************************************
 *                                                                            *
 * Function: refresh_active_checks                                            *
 *                                                                            *
 * Purpose: Retrieve from Treegix server list of active checks                 *
 *                                                                            *
 * Parameters: host - IP or Hostname of Treegix server                         *
 *             port - port of Treegix server                                   *
 *                                                                            *
 * Return value: returns SUCCEED on successful parsing,                       *
 *               FAIL on other cases                                          *
 *                                                                            *
 * Author: Eugene Grigorjev, Alexei Vladishev (new json protocol)             *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static int	refresh_active_checks(const char *host, unsigned short port)
{
	static TRX_THREAD_LOCAL int	last_ret = SUCCEED;
	int				ret;
	char				*tls_arg1, *tls_arg2;
	trx_socket_t			s;
	struct trx_json			json;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' port:%hu", __func__, host, port);

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	trx_json_addstring(&json, TRX_PROTO_TAG_REQUEST, TRX_PROTO_VALUE_GET_ACTIVE_CHECKS, TRX_JSON_TYPE_STRING);
	trx_json_addstring(&json, TRX_PROTO_TAG_HOST, CONFIG_HOSTNAME, TRX_JSON_TYPE_STRING);

	if (NULL != CONFIG_HOST_METADATA)
	{
		trx_json_addstring(&json, TRX_PROTO_TAG_HOST_METADATA, CONFIG_HOST_METADATA, TRX_JSON_TYPE_STRING);
	}
	else if (NULL != CONFIG_HOST_METADATA_ITEM)
	{
		process_config_item(&json,CONFIG_HOST_METADATA_ITEM, HOST_METADATA_LEN, TRX_PROTO_TAG_HOST_METADATA);
	}

	if (NULL != CONFIG_HOST_INTERFACE)
	{
		trx_json_addstring(&json, TRX_PROTO_TAG_INTERFACE, CONFIG_HOST_INTERFACE, TRX_JSON_TYPE_STRING);
	}
	else if (NULL != CONFIG_HOST_INTERFACE_ITEM)
	{
		process_config_item(&json,CONFIG_HOST_INTERFACE_ITEM, HOST_INTERFACE_LEN, TRX_PROTO_TAG_INTERFACE);
	}

	if (NULL != CONFIG_LISTEN_IP)
	{
		char	*p;

		if (NULL != (p = strchr(CONFIG_LISTEN_IP, ',')))
			*p = '\0';

		trx_json_addstring(&json, TRX_PROTO_TAG_IP, CONFIG_LISTEN_IP, TRX_JSON_TYPE_STRING);

		if (NULL != p)
			*p = ',';
	}

	if (TRX_DEFAULT_AGENT_PORT != CONFIG_LISTEN_PORT)
		trx_json_adduint64(&json, TRX_PROTO_TAG_PORT, CONFIG_LISTEN_PORT);

	switch (configured_tls_connect_mode)
	{
		case TRX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case TRX_TCP_SEC_TLS_CERT:
			tls_arg1 = CONFIG_TLS_SERVER_CERT_ISSUER;
			tls_arg2 = CONFIG_TLS_SERVER_CERT_SUBJECT;
			break;
		case TRX_TCP_SEC_TLS_PSK:
			tls_arg1 = CONFIG_TLS_PSK_IDENTITY;
			tls_arg2 = NULL;	/* trx_tls_connect() will find PSK */
			break;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			ret = FAIL;
			goto out;
	}

	if (SUCCEED == (ret = trx_tcp_connect(&s, CONFIG_SOURCE_IP, host, port, CONFIG_TIMEOUT,
			configured_tls_connect_mode, tls_arg1, tls_arg2)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "sending [%s]", json.buffer);

		if (SUCCEED == (ret = trx_tcp_send(&s, json.buffer)))
		{
			treegix_log(LOG_LEVEL_DEBUG, "before read");

			if (SUCCEED == (ret = trx_tcp_recv(&s)))
			{
				treegix_log(LOG_LEVEL_DEBUG, "got [%s]", s.buffer);

				if (SUCCEED != last_ret)
				{
					treegix_log(LOG_LEVEL_WARNING, "active check configuration update from [%s:%hu]"
							" is working again", host, port);
				}
				parse_list_of_checks(s.buffer, host, port);
			}
		}

		trx_tcp_close(&s);
	}
out:
	if (SUCCEED != ret && SUCCEED == last_ret)
	{
		treegix_log(LOG_LEVEL_WARNING,
				"active check configuration update from [%s:%hu] started to fail (%s)",
				host, port, trx_socket_strerror());
	}

	last_ret = ret;

	trx_json_free(&json);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: check_response                                                   *
 *                                                                            *
 * Purpose: Check whether JSON response is SUCCEED                            *
 *                                                                            *
 * Parameters: JSON response from Treegix trapper                              *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: treegix_sender has almost the same function!                      *
 *                                                                            *
 ******************************************************************************/
static int	check_response(char *response)
{
	struct trx_json_parse	jp;
	char			value[MAX_STRING_LEN];
	char			info[MAX_STRING_LEN];
	int			ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() response:'%s'", __func__, response);

	ret = trx_json_open(response, &jp);

	if (SUCCEED == ret)
		ret = trx_json_value_by_name(&jp, TRX_PROTO_TAG_RESPONSE, value, sizeof(value));

	if (SUCCEED == ret && 0 != strcmp(value, TRX_PROTO_VALUE_SUCCESS))
		ret = FAIL;

	if (SUCCEED == ret && SUCCEED == trx_json_value_by_name(&jp, TRX_PROTO_TAG_INFO, info, sizeof(info)))
		treegix_log(LOG_LEVEL_DEBUG, "info from server: '%s'", info);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: send_buffer                                                      *
 *                                                                            *
 * Purpose: Send value stored in the buffer to Treegix server                  *
 *                                                                            *
 * Parameters: host - IP or Hostname of Treegix server                         *
 *             port - port number                                             *
 *                                                                            *
 * Return value: returns SUCCEED on successful sending,                       *
 *               FAIL on other cases                                          *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static int	send_buffer(const char *host, unsigned short port)
{
	TRX_ACTIVE_BUFFER_ELEMENT	*el;
	int				ret = SUCCEED, i, now;
	char				*tls_arg1, *tls_arg2;
	trx_timespec_t			ts;
	const char			*err_send_step = "";
	trx_socket_t			s;
	struct trx_json 		json;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' port:%d entries:%d/%d",
			__func__, host, port, buffer.count, CONFIG_BUFFER_SIZE);

	if (0 == buffer.count)
		goto ret;

	now = (int)time(NULL);

	if (CONFIG_BUFFER_SIZE / 2 > buffer.pcount && CONFIG_BUFFER_SIZE > buffer.count &&
			CONFIG_BUFFER_SEND > now - buffer.lastsent)
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() now:%d lastsent:%d now-lastsent:%d BufferSend:%d; will not send now",
				__func__, now, buffer.lastsent, now - buffer.lastsent, CONFIG_BUFFER_SEND);
		goto ret;
	}

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);
	trx_json_addstring(&json, TRX_PROTO_TAG_REQUEST, TRX_PROTO_VALUE_AGENT_DATA, TRX_JSON_TYPE_STRING);
	trx_json_addstring(&json, TRX_PROTO_TAG_SESSION, session_token, TRX_JSON_TYPE_STRING);
	trx_json_addarray(&json, TRX_PROTO_TAG_DATA);

	for (i = 0; i < buffer.count; i++)
	{
		el = &buffer.data[i];

		trx_json_addobject(&json, NULL);
		trx_json_addstring(&json, TRX_PROTO_TAG_HOST, el->host, TRX_JSON_TYPE_STRING);
		trx_json_addstring(&json, TRX_PROTO_TAG_KEY, el->key, TRX_JSON_TYPE_STRING);

		if (NULL != el->value)
			trx_json_addstring(&json, TRX_PROTO_TAG_VALUE, el->value, TRX_JSON_TYPE_STRING);

		if (ITEM_STATE_NOTSUPPORTED == el->state)
		{
			trx_json_adduint64(&json, TRX_PROTO_TAG_STATE, ITEM_STATE_NOTSUPPORTED);
		}
		else
		{
			/* add item meta information only for items in normal state */
			if (0 != (TRX_METRIC_FLAG_LOG & el->flags))
				trx_json_adduint64(&json, TRX_PROTO_TAG_LASTLOGSIZE, el->lastlogsize);
			if (0 != (TRX_METRIC_FLAG_LOG_LOGRT & el->flags))
				trx_json_adduint64(&json, TRX_PROTO_TAG_MTIME, el->mtime);
		}

		if (0 != el->timestamp)
			trx_json_adduint64(&json, TRX_PROTO_TAG_LOGTIMESTAMP, el->timestamp);

		if (NULL != el->source)
			trx_json_addstring(&json, TRX_PROTO_TAG_LOGSOURCE, el->source, TRX_JSON_TYPE_STRING);

		if (0 != el->severity)
			trx_json_adduint64(&json, TRX_PROTO_TAG_LOGSEVERITY, el->severity);

		if (0 != el->logeventid)
			trx_json_adduint64(&json, TRX_PROTO_TAG_LOGEVENTID, el->logeventid);

		trx_json_adduint64(&json, TRX_PROTO_TAG_ID, el->id);

		trx_json_adduint64(&json, TRX_PROTO_TAG_CLOCK, el->ts.sec);
		trx_json_adduint64(&json, TRX_PROTO_TAG_NS, el->ts.ns);
		trx_json_close(&json);
	}

	trx_json_close(&json);

	switch (configured_tls_connect_mode)
	{
		case TRX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case TRX_TCP_SEC_TLS_CERT:
			tls_arg1 = CONFIG_TLS_SERVER_CERT_ISSUER;
			tls_arg2 = CONFIG_TLS_SERVER_CERT_SUBJECT;
			break;
		case TRX_TCP_SEC_TLS_PSK:
			tls_arg1 = CONFIG_TLS_PSK_IDENTITY;
			tls_arg2 = NULL;	/* trx_tls_connect() will find PSK */
			break;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			ret = FAIL;
			goto out;
	}

	if (SUCCEED == (ret = trx_tcp_connect(&s, CONFIG_SOURCE_IP, host, port, MIN(buffer.count * CONFIG_TIMEOUT, 60),
			configured_tls_connect_mode, tls_arg1, tls_arg2)))
	{
		trx_timespec(&ts);
		trx_json_adduint64(&json, TRX_PROTO_TAG_CLOCK, ts.sec);
		trx_json_adduint64(&json, TRX_PROTO_TAG_NS, ts.ns);

		treegix_log(LOG_LEVEL_DEBUG, "JSON before sending [%s]", json.buffer);

		if (SUCCEED == (ret = trx_tcp_send(&s, json.buffer)))
		{
			if (SUCCEED == (ret = trx_tcp_recv(&s)))
			{
				treegix_log(LOG_LEVEL_DEBUG, "JSON back [%s]", s.buffer);

				if (NULL == s.buffer || SUCCEED != check_response(s.buffer))
				{
					ret = FAIL;
					treegix_log(LOG_LEVEL_DEBUG, "NOT OK");
				}
				else
					treegix_log(LOG_LEVEL_DEBUG, "OK");
			}
			else
				err_send_step = "[recv] ";
		}
		else
			err_send_step = "[send] ";

		trx_tcp_close(&s);
	}
	else
		err_send_step = "[connect] ";
out:
	trx_json_free(&json);

	if (SUCCEED == ret)
	{
		/* free buffer */
		for (i = 0; i < buffer.count; i++)
		{
			el = &buffer.data[i];

			trx_free(el->host);
			trx_free(el->key);
			trx_free(el->value);
			trx_free(el->source);
		}
		buffer.count = 0;
		buffer.pcount = 0;
		buffer.lastsent = now;
		if (0 != buffer.first_error)
		{
			treegix_log(LOG_LEVEL_WARNING, "active check data upload to [%s:%hu] is working again",
					host, port);
			buffer.first_error = 0;
		}
	}
	else
	{
		if (0 == buffer.first_error)
		{
			treegix_log(LOG_LEVEL_WARNING, "active check data upload to [%s:%hu] started to fail (%s%s)",
					host, port, err_send_step, trx_socket_strerror());
			buffer.first_error = now;
		}
		treegix_log(LOG_LEVEL_DEBUG, "send value error: %s%s", err_send_step, trx_socket_strerror());
	}
ret:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_value                                                    *
 *                                                                            *
 * Purpose: Buffer new value or send the whole buffer to the server           *
 *                                                                            *
 * Parameters: server      - IP or Hostname of Treegix server                  *
 *             port        - port of Treegix server                            *
 *             host        - name of host in Treegix database                  *
 *             key         - name of metric                                   *
 *             value       - key value or error message why an item became    *
 *                           NOTSUPPORTED                                     *
 *             state       - ITEM_STATE_NORMAL or ITEM_STATE_NOTSUPPORTED     *
 *             lastlogsize - size of read logfile                             *
 *             mtime       - time of last file modification                   *
 *             timestamp   - timestamp of read value                          *
 *             source      - name of logged data source                       *
 *             severity    - severity of logged data sources                  *
 *             logeventid  - the application-specific identifier for          *
 *                           the event; used for monitoring of Windows        *
 *                           event logs                                       *
 *             flags       - metric flags                                     *
 *                                                                            *
 * Return value: returns SUCCEED on successful parsing,                       *
 *               FAIL on other cases                                          *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: ATTENTION! This function's address and pointers to arguments     *
 *           are described in Treegix defined type "trx_process_value_func_t"  *
 *           and used when calling process_log(), process_logrt() and         *
 *           trx_read2(). If you ever change this process_value() arguments   *
 *           or return value do not forget to synchronize changes with the    *
 *           defined type "trx_process_value_func_t" and implementations of   *
 *           process_log(), process_logrt(), trx_read2() and their callers.   *
 *                                                                            *
 ******************************************************************************/
static int	process_value(const char *server, unsigned short port, const char *host, const char *key,
		const char *value, unsigned char state, trx_uint64_t *lastlogsize, const int *mtime,
		unsigned long *timestamp, const char *source, unsigned short *severity, unsigned long *logeventid,
		unsigned char flags)
{
	TRX_ACTIVE_BUFFER_ELEMENT	*el = NULL;
	int				i, ret = FAIL;
	size_t				sz;

	if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		if (NULL != lastlogsize)
		{
			treegix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s:%s' lastlogsize:" TRX_FS_UI64 " value:'%s'",
					__func__, host, key, *lastlogsize, TRX_NULL2STR(value));
		}
		else
		{
			/* log a dummy lastlogsize to keep the same record format for simpler parsing */
			treegix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s:%s' lastlogsize:null value:'%s'",
					__func__, host, key, TRX_NULL2STR(value));
		}
	}

	/* do not sent data from buffer if host/key are the same as previous unless buffer is full already */
	if (0 < buffer.count)
	{
		el = &buffer.data[buffer.count - 1];

		if ((0 != (flags & TRX_METRIC_FLAG_PERSISTENT) && CONFIG_BUFFER_SIZE / 2 <= buffer.pcount) ||
				CONFIG_BUFFER_SIZE <= buffer.count ||
				0 != strcmp(el->key, key) || 0 != strcmp(el->host, host))
		{
			send_buffer(server, port);
		}
	}

	if (0 != (TRX_METRIC_FLAG_PERSISTENT & flags) && CONFIG_BUFFER_SIZE / 2 <= buffer.pcount)
	{
		treegix_log(LOG_LEVEL_WARNING, "buffer is full, cannot store persistent value");
		goto out;
	}

	if (CONFIG_BUFFER_SIZE > buffer.count)
	{
		treegix_log(LOG_LEVEL_DEBUG, "buffer: new element %d", buffer.count);
		el = &buffer.data[buffer.count];
		buffer.count++;
	}
	else
	{
		if (0 == (TRX_METRIC_FLAG_PERSISTENT & flags))
		{
			for (i = 0; i < buffer.count; i++)
			{
				el = &buffer.data[i];
				if (0 == strcmp(el->host, host) && 0 == strcmp(el->key, key))
					break;
			}
		}

		if (0 != (TRX_METRIC_FLAG_PERSISTENT & flags) || i == buffer.count)
		{
			for (i = 0; i < buffer.count; i++)
			{
				el = &buffer.data[i];
				if (0 == (TRX_METRIC_FLAG_PERSISTENT & el->flags))
					break;
			}
		}

		if (NULL != el)
		{
			treegix_log(LOG_LEVEL_DEBUG, "remove element [%d] Key:'%s:%s'", i, el->host, el->key);

			trx_free(el->host);
			trx_free(el->key);
			trx_free(el->value);
			trx_free(el->source);
		}

		sz = (CONFIG_BUFFER_SIZE - i - 1) * sizeof(TRX_ACTIVE_BUFFER_ELEMENT);
		memmove(&buffer.data[i], &buffer.data[i + 1], sz);

		treegix_log(LOG_LEVEL_DEBUG, "buffer full: new element %d", buffer.count - 1);

		el = &buffer.data[CONFIG_BUFFER_SIZE - 1];
	}

	memset(el, 0, sizeof(TRX_ACTIVE_BUFFER_ELEMENT));
	el->host = trx_strdup(NULL, host);
	el->key = trx_strdup(NULL, key);
	if (NULL != value)
		el->value = trx_strdup(NULL, value);
	el->state = state;

	if (NULL != source)
		el->source = strdup(source);
	if (NULL != severity)
		el->severity = *severity;
	if (NULL != lastlogsize)
		el->lastlogsize = *lastlogsize;
	if (NULL != mtime)
		el->mtime = *mtime;
	if (NULL != timestamp)
		el->timestamp = *timestamp;
	if (NULL != logeventid)
		el->logeventid = (int)*logeventid;

	trx_timespec(&el->ts);
	el->flags = flags;
	el->id = ++last_valueid;

	if (0 != (TRX_METRIC_FLAG_PERSISTENT & flags))
		buffer.pcount++;

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int	need_meta_update(TRX_ACTIVE_METRIC *metric, trx_uint64_t lastlogsize_sent, int mtime_sent,
		unsigned char old_state, trx_uint64_t lastlogsize_last, int mtime_last)
{
	int	ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() key:%s", __func__, metric->key);

	if (0 != (TRX_METRIC_FLAG_LOG & metric->flags))
	{
		/* meta information update is needed if:                                              */
		/* - lastlogsize or mtime changed since we last sent within this check                */
		/* - nothing was sent during this check and state changed from notsupported to normal */
		/* - nothing was sent during this check and it's a new metric                         */
		if (lastlogsize_sent != metric->lastlogsize || mtime_sent != metric->mtime ||
				(lastlogsize_last == lastlogsize_sent && mtime_last == mtime_sent &&
						(old_state != metric->state ||
						0 != (TRX_METRIC_FLAG_NEW & metric->flags))))
		{
			/* needs meta information update */
			ret = SUCCEED;
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int	process_eventlog_check(char *server, unsigned short port, TRX_ACTIVE_METRIC *metric,
		trx_uint64_t *lastlogsize_sent, char **error)
{
	int 		ret = FAIL;

#ifdef _WINDOWS
	AGENT_REQUEST	request;
	const char	*filename, *pattern, *maxlines_persec, *key_severity, *key_source, *key_logeventid, *skip;
	int		rate;
	OSVERSIONINFO	versionInfo;

	init_request(&request);

	if (SUCCEED != parse_item_key(metric->key, &request))
	{
		*error = trx_strdup(*error, "Invalid item key format.");
		goto out;
	}

	if (0 == get_rparams_num(&request))
	{
		*error = trx_strdup(*error, "Invalid number of parameters.");
		goto out;
	}

	if (7 < get_rparams_num(&request))
	{
		*error = trx_strdup(*error, "Too many parameters.");
		goto out;
	}

	if (NULL == (filename = get_rparam(&request, 0)) || '\0' == *filename)
	{
		*error = trx_strdup(*error, "Invalid first parameter.");
		goto out;
	}

	if (NULL == (pattern = get_rparam(&request, 1)))
	{
		pattern = "";
	}
	else if ('@' == *pattern && SUCCEED != trx_global_regexp_exists(pattern + 1, &regexps))
	{
		*error = trx_dsprintf(*error, "Global regular expression \"%s\" does not exist.", pattern + 1);
		goto out;
	}

	if (NULL == (key_severity = get_rparam(&request, 2)))
	{
		key_severity = "";
	}
	else if ('@' == *key_severity && SUCCEED != trx_global_regexp_exists(key_severity + 1, &regexps))
	{
		*error = trx_dsprintf(*error, "Global regular expression \"%s\" does not exist.", key_severity + 1);
		goto out;
	}

	if (NULL == (key_source = get_rparam(&request, 3)))
	{
		key_source = "";
	}
	else if ('@' == *key_source && SUCCEED != trx_global_regexp_exists(key_source + 1, &regexps))
	{
		*error = trx_dsprintf(*error, "Global regular expression \"%s\" does not exist.", key_source + 1);
		goto out;
	}

	if (NULL == (key_logeventid = get_rparam(&request, 4)))
	{
		key_logeventid = "";
	}
	else if ('@' == *key_logeventid && SUCCEED != trx_global_regexp_exists(key_logeventid + 1, &regexps))
	{
		*error = trx_dsprintf(*error, "Global regular expression \"%s\" does not exist.", key_logeventid + 1);
		goto out;
	}

	if (NULL == (maxlines_persec = get_rparam(&request, 5)) || '\0' == *maxlines_persec)
	{
		rate = CONFIG_MAX_LINES_PER_SECOND;
	}
	else if (MIN_VALUE_LINES > (rate = atoi(maxlines_persec)) || MAX_VALUE_LINES < rate)
	{
		*error = trx_strdup(*error, "Invalid sixth parameter.");
		goto out;
	}

	if (NULL == (skip = get_rparam(&request, 6)) || '\0' == *skip || 0 == strcmp(skip, "all"))
	{
		metric->skip_old_data = 0;
	}
	else if (0 != strcmp(skip, "skip"))
	{
		*error = trx_strdup(*error, "Invalid seventh parameter.");
		goto out;
	}

	versionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&versionInfo);

	if (versionInfo.dwMajorVersion >= 6)	/* Windows Vista, 7 or Server 2008 */
	{
		__try
		{
			trx_uint64_t	lastlogsize = metric->lastlogsize;
			EVT_HANDLE	eventlog6_render_context = NULL;
			EVT_HANDLE	eventlog6_query = NULL;
			trx_uint64_t	eventlog6_firstid = 0;
			trx_uint64_t	eventlog6_lastid = 0;

			if (SUCCEED != initialize_eventlog6(filename, &lastlogsize, &eventlog6_firstid,
					&eventlog6_lastid, &eventlog6_render_context, &eventlog6_query, error))
			{
				finalize_eventlog6(&eventlog6_render_context, &eventlog6_query);
				goto out;
			}

			ret = process_eventslog6(server, port, filename, &eventlog6_render_context, &eventlog6_query,
					lastlogsize, eventlog6_firstid, eventlog6_lastid, &regexps, pattern,
					key_severity, key_source, key_logeventid, rate, process_value, metric,
					lastlogsize_sent, error);

			finalize_eventlog6(&eventlog6_render_context, &eventlog6_query);
		}
		__except (DelayLoadDllExceptionFilter(GetExceptionInformation()))
		{
			treegix_log(LOG_LEVEL_WARNING, "failed to process eventlog");
		}
	}
	else if (versionInfo.dwMajorVersion < 6)    /* Windows versions before Vista */
	{
		ret = process_eventslog(server, port, filename, &regexps, pattern, key_severity, key_source,
				key_logeventid, rate, process_value, metric, lastlogsize_sent, error);
	}
out:
	free_request(&request);
#else	/* not _WINDOWS */
	TRX_UNUSED(server);
	TRX_UNUSED(port);
	TRX_UNUSED(metric);
	TRX_UNUSED(lastlogsize_sent);
	TRX_UNUSED(error);
#endif	/* _WINDOWS */

	return ret;
}

static int	process_common_check(char *server, unsigned short port, TRX_ACTIVE_METRIC *metric, char **error)
{
	int		ret;
	AGENT_RESULT	result;
	char		**pvalue;

	init_result(&result);

	if (SUCCEED != (ret = process(metric->key, 0, &result)))
	{
		if (NULL != (pvalue = GET_MSG_RESULT(&result)))
			*error = trx_strdup(*error, *pvalue);
		goto out;
	}

	if (NULL != (pvalue = GET_TEXT_RESULT(&result)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "for key [%s] received value [%s]", metric->key, *pvalue);

		process_value(server, port, CONFIG_HOSTNAME, metric->key_orig, *pvalue, ITEM_STATE_NORMAL, NULL, NULL,
				NULL, NULL, NULL, NULL, metric->flags);
	}
out:
	free_result(&result);

	return ret;
}

static void	process_active_checks(char *server, unsigned short port)
{
	char	*error = NULL;
	int	i, now, ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() server:'%s' port:%hu", __func__, server, port);

	now = (int)time(NULL);

	for (i = 0; i < active_metrics.values_num; i++)
	{
		trx_uint64_t		lastlogsize_last, lastlogsize_sent;
		int			mtime_last, mtime_sent;
		TRX_ACTIVE_METRIC	*metric;

		metric = (TRX_ACTIVE_METRIC *)active_metrics.values[i];

		if (metric->nextcheck > now)
			continue;

		if (SUCCEED != metric_ready_to_process(metric))
			continue;

		/* for meta information update we need to know if something was sent at all during the check */
		lastlogsize_last = metric->lastlogsize;
		mtime_last = metric->mtime;

		lastlogsize_sent = metric->lastlogsize;
		mtime_sent = metric->mtime;

		/* before processing make sure refresh is not 0 to avoid overload */
		if (0 == metric->refresh)
		{
			ret = FAIL;
			error = trx_strdup(error, "Incorrect update interval.");
		}
		else if (0 != ((TRX_METRIC_FLAG_LOG_LOG | TRX_METRIC_FLAG_LOG_LOGRT) & metric->flags))
		{
			ret = process_log_check(server, port, &regexps, metric, process_value, &lastlogsize_sent,
					&mtime_sent, &error);
		}
		else if (0 != (TRX_METRIC_FLAG_LOG_EVENTLOG & metric->flags))
			ret = process_eventlog_check(server, port, metric, &lastlogsize_sent, &error);
		else
			ret = process_common_check(server, port, metric, &error);

		if (SUCCEED != ret)
		{
			const char	*perror;

			perror = (NULL != error ? error : TRX_NOTSUPPORTED_MSG);

			metric->state = ITEM_STATE_NOTSUPPORTED;
			metric->refresh_unsupported = 0;
			metric->error_count = 0;
			metric->start_time = 0.0;
			metric->processed_bytes = 0;

			treegix_log(LOG_LEVEL_WARNING, "active check \"%s\" is not supported: %s", metric->key, perror);

			process_value(server, port, CONFIG_HOSTNAME, metric->key_orig, perror, ITEM_STATE_NOTSUPPORTED,
					&metric->lastlogsize, &metric->mtime, NULL, NULL, NULL, NULL, metric->flags);

			trx_free(error);
		}
		else
		{
			if (0 == metric->error_count)
			{
				unsigned char	old_state;

				old_state = metric->state;

				if (ITEM_STATE_NOTSUPPORTED == metric->state)
				{
					/* item became supported */
					metric->state = ITEM_STATE_NORMAL;
					metric->refresh_unsupported = 0;
				}

				if (SUCCEED == need_meta_update(metric, lastlogsize_sent, mtime_sent, old_state,
						lastlogsize_last, mtime_last))
				{
					/* meta information update */
					process_value(server, port, CONFIG_HOSTNAME, metric->key_orig, NULL,
							metric->state, &metric->lastlogsize, &metric->mtime, NULL, NULL,
							NULL, NULL, metric->flags);
				}

				/* remove "new metric" flag */
				metric->flags &= ~TRX_METRIC_FLAG_NEW;
			}
		}

		send_buffer(server, port);
		metric->nextcheck = (int)time(NULL) + metric->refresh;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: update_schedule                                                  *
 *                                                                            *
 * Purpose: update active check and send buffer schedule by the specified     *
 *          time delta                                                        *
 *                                                                            *
 * Parameters: delta - [IN] the time delta in seconds                         *
 *                                                                            *
 * Comments: This function is used to update checking and sending schedules   *
 *           if the system time was rolled back.                              *
 *                                                                            *
 ******************************************************************************/
static void	update_schedule(int delta)
{
	int	i;

	for (i = 0; i < active_metrics.values_num; i++)
	{
		TRX_ACTIVE_METRIC	*metric = (TRX_ACTIVE_METRIC *)active_metrics.values[i];
		metric->nextcheck += delta;
	}

	buffer.lastsent += delta;
}

TRX_THREAD_ENTRY(active_checks_thread, args)
{
	TRX_THREAD_ACTIVECHK_ARGS activechk_args;

	time_t	nextcheck = 0, nextrefresh = 0, nextsend = 0, now, delta, lastcheck = 0;

	assert(args);
	assert(((trx_thread_args_t *)args)->args);

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	activechk_args.host = trx_strdup(NULL, ((TRX_THREAD_ACTIVECHK_ARGS *)((trx_thread_args_t *)args)->args)->host);
	activechk_args.port = ((TRX_THREAD_ACTIVECHK_ARGS *)((trx_thread_args_t *)args)->args)->port;

	trx_free(args);

	session_token = trx_create_token(0);

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_init_child();
#endif
	init_active_metrics();

	while (TRX_IS_RUNNING())
	{
		trx_update_env(trx_time());

		if ((now = time(NULL)) >= nextsend)
		{
			send_buffer(activechk_args.host, activechk_args.port);
			nextsend = time(NULL) + 1;
		}

		if (now >= nextrefresh)
		{
			trx_setproctitle("active checks #%d [getting list of active checks]", process_num);

			if (FAIL == refresh_active_checks(activechk_args.host, activechk_args.port))
			{
				nextrefresh = time(NULL) + 60;
			}
			else
			{
				nextrefresh = time(NULL) + CONFIG_REFRESH_ACTIVE_CHECKS;
			}
		}

		if (now >= nextcheck && CONFIG_BUFFER_SIZE / 2 > buffer.pcount)
		{
			trx_setproctitle("active checks #%d [processing active checks]", process_num);

			process_active_checks(activechk_args.host, activechk_args.port);

			if (CONFIG_BUFFER_SIZE / 2 <= buffer.pcount)	/* failed to complete processing active checks */
				continue;

			nextcheck = get_min_nextcheck();
			if (FAIL == nextcheck)
				nextcheck = time(NULL) + 60;
		}
		else
		{
			if (0 > (delta = now - lastcheck))
			{
				treegix_log(LOG_LEVEL_WARNING, "the system time has been pushed back,"
						" adjusting active check schedule");
				update_schedule((int)delta);
				nextcheck += delta;
				nextsend += delta;
				nextrefresh += delta;
			}

			trx_setproctitle("active checks #%d [idle 1 sec]", process_num);
			trx_sleep(1);
		}

		lastcheck = now;
	}

	trx_free(session_token);

#ifdef _WINDOWS
	trx_free(activechk_args.host);
	free_active_metrics();

	TRX_DO_EXIT();

	trx_thread_exit(EXIT_SUCCESS);
#else
	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
#endif
}
