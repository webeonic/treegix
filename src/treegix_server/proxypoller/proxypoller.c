

#include "common.h"
#include "daemon.h"
#include "comms.h"
#include "trxself.h"

#include "proxypoller.h"
#include "trxserver.h"
#include "dbcache.h"
#include "db.h"
#include "trxjson.h"
#include "log.h"
#include "proxy.h"
#include "../../libs/trxcrypto/tls.h"
#include "../trapper/proxydata.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static int	connect_to_proxy(const DC_PROXY *proxy, trx_socket_t *sock, int timeout)
{
	int		ret = FAIL;
	const char	*tls_arg1, *tls_arg2;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() address:%s port:%hu timeout:%d conn:%u", __func__, proxy->addr,
			proxy->port, timeout, (unsigned int)proxy->tls_connect);

	switch (proxy->tls_connect)
	{
		case TRX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case TRX_TCP_SEC_TLS_CERT:
			tls_arg1 = proxy->tls_issuer;
			tls_arg2 = proxy->tls_subject;
			break;
		case TRX_TCP_SEC_TLS_PSK:
			tls_arg1 = proxy->tls_psk_identity;
			tls_arg2 = proxy->tls_psk;
			break;
#else
		case TRX_TCP_SEC_TLS_CERT:
		case TRX_TCP_SEC_TLS_PSK:
			treegix_log(LOG_LEVEL_ERR, "TLS connection is configured to be used with passive proxy \"%s\""
					" but support for TLS was not compiled into %s.", proxy->host,
					get_program_type_string(program_type));
			ret = CONFIG_ERROR;
			goto out;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			goto out;
	}

	if (FAIL == (ret = trx_tcp_connect(sock, CONFIG_SOURCE_IP, proxy->addr, proxy->port, timeout,
			proxy->tls_connect, tls_arg1, tls_arg2)))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot connect to proxy \"%s\": %s", proxy->host, trx_socket_strerror());
		ret = NETWORK_ERROR;
	}
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int	send_data_to_proxy(const DC_PROXY *proxy, trx_socket_t *sock, const char *data, size_t size)
{
	int	ret, flags = TRX_TCP_PROTOCOL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() data:'%s'", __func__, data);

	if (0 != proxy->auto_compress)
		flags |= TRX_TCP_COMPRESS;

	if (FAIL == (ret = trx_tcp_send_ext(sock, data, size, flags, 0)))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot send data to proxy \"%s\": %s", proxy->host, trx_socket_strerror());

		ret = NETWORK_ERROR;
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static int	recv_data_from_proxy(const DC_PROXY *proxy, trx_socket_t *sock)
{
	int	ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == (ret = trx_tcp_recv(sock)))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot obtain data from proxy \"%s\": %s", proxy->host,
				trx_socket_strerror());
	}
	else
		treegix_log(LOG_LEVEL_DEBUG, "obtained data from proxy \"%s\": [%s]", proxy->host, sock->buffer);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static void	disconnect_proxy(trx_socket_t *sock)
{
	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_tcp_close(sock);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: get_data_from_proxy                                              *
 *                                                                            *
 * Purpose: get historical data from proxy                                    *
 *                                                                            *
 * Parameters: proxy   - [IN/OUT] proxy data                                  *
 *             request - [IN] requested data type                             *
 *             data    - [OUT] data received from proxy                       *
 *             ts      - [OUT] timestamp when the proxy connection was        *
 *                             established                                    *
 *             tasks   - [IN] proxy task response flag                        *
 *                                                                            *
 * Return value: SUCCESS - processed successfully                             *
 *               other code - an error occurred                               *
 *                                                                            *
 * Comments: The proxy->compress property is updated depending on the         *
 *           protocol flags sent by proxy.                                    *
 *                                                                            *
 ******************************************************************************/
static int	get_data_from_proxy(DC_PROXY *proxy, const char *request, char **data, trx_timespec_t *ts)
{
	trx_socket_t	s;
	struct trx_json	j;
	int		ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() request:'%s'", __func__, request);

	trx_json_init(&j, TRX_JSON_STAT_BUF_LEN);

	trx_json_addstring(&j, "request", request, TRX_JSON_TYPE_STRING);

	if (SUCCEED == (ret = connect_to_proxy(proxy, &s, CONFIG_TRAPPER_TIMEOUT)))
	{
		/* get connection timestamp if required */
		if (NULL != ts)
			trx_timespec(ts);

		if (SUCCEED == (ret = send_data_to_proxy(proxy, &s, j.buffer, j.buffer_size)))
		{
			if (SUCCEED == (ret = recv_data_from_proxy(proxy, &s)))
			{
				if (0 != (s.protocol & TRX_TCP_COMPRESS))
					proxy->auto_compress = 1;

				if (!TRX_IS_RUNNING())
				{
					int	flags = TRX_TCP_PROTOCOL;

					if (0 != (s.protocol & TRX_TCP_COMPRESS))
						flags |= TRX_TCP_COMPRESS;

					trx_send_response_ext(&s, FAIL, "Treegix server shutdown in progress", NULL,
							flags, CONFIG_TIMEOUT);

					treegix_log(LOG_LEVEL_WARNING, "cannot process proxy data from passive proxy at"
							" \"%s\": Treegix server shutdown in progress", s.peer);
					ret = FAIL;
				}
				else
				{
					ret = trx_send_proxy_data_response(proxy, &s, NULL);

					if (SUCCEED == ret)
						*data = trx_strdup(*data, s.buffer);
				}
			}
		}

		disconnect_proxy(&s);
	}

	trx_json_free(&j);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_send_configuration                                         *
 *                                                                            *
 * Purpose: sends configuration data to proxy                                 *
 *                                                                            *
 * Parameters: proxy - [IN/OUT] proxy data                                    *
 *                                                                            *
 * Return value: SUCCEED - processed successfully                             *
 *               other code - an error occurred                               *
 *                                                                            *
 * Comments: This function updates proxy version, compress and lastaccess     *
 *           properties.                                                      *
 *                                                                            *
 ******************************************************************************/
static int	proxy_send_configuration(DC_PROXY *proxy)
{
	char		*error = NULL;
	int		ret;
	trx_socket_t	s;
	struct trx_json	j;

	trx_json_init(&j, 512 * TRX_KIBIBYTE);

	trx_json_addstring(&j, TRX_PROTO_TAG_REQUEST, TRX_PROTO_VALUE_PROXY_CONFIG, TRX_JSON_TYPE_STRING);
	trx_json_addobject(&j, TRX_PROTO_TAG_DATA);

	if (SUCCEED != (ret = get_proxyconfig_data(proxy->hostid, &j, &error)))
	{
		treegix_log(LOG_LEVEL_ERR, "cannot collect configuration data for proxy \"%s\": %s",
				proxy->host, error);
		goto out;
	}

	if (SUCCEED != (ret = connect_to_proxy(proxy, &s, CONFIG_TRAPPER_TIMEOUT)))
		goto out;

	treegix_log(LOG_LEVEL_WARNING, "sending configuration data to proxy \"%s\" at \"%s\", datalen " TRX_FS_SIZE_T,
			proxy->host, s.peer, (trx_fs_size_t)j.buffer_size);

	if (SUCCEED == (ret = send_data_to_proxy(proxy, &s, j.buffer, j.buffer_size)))
	{
		if (SUCCEED != (ret = trx_recv_response(&s, 0, &error)))
		{
			treegix_log(LOG_LEVEL_WARNING, "cannot send configuration data to proxy"
					" \"%s\" at \"%s\": %s", proxy->host, s.peer, error);
		}
		else
		{
			struct trx_json_parse	jp;

			if (SUCCEED != trx_json_open(s.buffer, &jp))
			{
				treegix_log(LOG_LEVEL_WARNING, "invalid configuration data response received from proxy"
						" \"%s\" at \"%s\": %s", proxy->host, s.peer, trx_json_strerror());
			}
			else
			{
				proxy->version = trx_get_proxy_protocol_version(&jp);
				proxy->auto_compress = (0 != (s.protocol & TRX_TCP_COMPRESS) ? 1 : 0);
				proxy->lastaccess = time(NULL);
			}
		}
	}

	disconnect_proxy(&s);
out:
	trx_free(error);
	trx_json_free(&j);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_process_proxy_data                                         *
 *                                                                            *
 * Purpose: processes proxy data request                                      *
 *                                                                            *
 * Parameters: proxy  - [IN/OUT] proxy data                                   *
 *             answer - [IN] data received from proxy                         *
 *             ts     - [IN] timestamp when the proxy connection was          *
 *                           established                                      *
 *             more   - [OUT] available data flag                             *
 *                                                                            *
 * Return value: SUCCEED - data were received and processed successfully      *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Comments: The proxy->version property is updated with the version number   *
 *           sent by proxy.                                                   *
 *                                                                            *
 ******************************************************************************/
static int	proxy_process_proxy_data(DC_PROXY *proxy, const char *answer, trx_timespec_t *ts, int *more)
{
	struct trx_json_parse	jp;
	char			*error = NULL;
	int			ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	*more = TRX_PROXY_DATA_DONE;

	if ('\0' == *answer)
	{
		treegix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned no proxy data:"
				" check allowed connection types and access rights", proxy->host, proxy->addr);
		goto out;
	}

	if (SUCCEED != trx_json_open(answer, &jp))
	{
		treegix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid proxy data: %s",
				proxy->host, proxy->addr, trx_json_strerror());
		goto out;
	}

	proxy->version = trx_get_proxy_protocol_version(&jp);

	if (SUCCEED != trx_check_protocol_version(proxy))
	{
		goto out;
	}

	if (SUCCEED != (ret = process_proxy_data(proxy, &jp, ts, &error)))
	{
		treegix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid proxy data: %s",
				proxy->host, proxy->addr, error);
	}
	else
	{
		char	value[MAX_STRING_LEN];

		if (SUCCEED == trx_json_value_by_name(&jp, TRX_PROTO_TAG_MORE, value, sizeof(value)))
			*more = atoi(value);
	}
out:
	trx_free(error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_get_data                                                   *
 *                                                                            *
 * Purpose: gets data from proxy ('proxy data' request)                       *
 *                                                                            *
 * Parameters: proxy  - [IN] proxy data                                       *
 *             more   - [OUT] available data flag                             *
 *                                                                            *
 * Return value: SUCCEED - data were received and processed successfully      *
 *               other code - an error occurred                               *
 *                                                                            *
 * Comments: This function updates proxy version, compress and lastaccess     *
 *           properties.                                                      *
 *                                                                            *
 ******************************************************************************/
static int	proxy_get_data(DC_PROXY *proxy, int *more)
{
	char		*answer = NULL;
	int		ret;
	trx_timespec_t	ts;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (ret = get_data_from_proxy(proxy, TRX_PROTO_VALUE_PROXY_DATA, &answer, &ts)))
		goto out;

	/* handle pre 3.4 proxies that did not support proxy data request */
	if ('\0' == *answer)
	{
		proxy->version = TRX_COMPONENT_VERSION(3, 2);
		trx_free(answer);
		ret = FAIL;
		goto out;
	}

	proxy->lastaccess = time(NULL);
	ret = proxy_process_proxy_data(proxy, answer, &ts, more);
	trx_free(answer);
out:
	if (SUCCEED == ret)
		treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s more:%d", __func__, trx_result_string(ret), *more);
	else
		treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_get_tasks                                                  *
 *                                                                            *
 * Purpose: gets data from proxy ('proxy data' request)                       *
 *                                                                            *
 * Parameters: proxy - [IN/OUT] the proxy data                                *
 *                                                                            *
 * Return value: SUCCEED - data were received and processed successfully      *
 *               other code - an error occurred                               *
 *                                                                            *
 * Comments: This function updates proxy version, compress and lastaccess     *
 *           properties.                                                      *
 *                                                                            *
 ******************************************************************************/
static int	proxy_get_tasks(DC_PROXY *proxy)
{
	char		*answer = NULL;
	int		ret = FAIL, more;
	trx_timespec_t	ts;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (TRX_COMPONENT_VERSION(3, 2) >= proxy->version)
		goto out;

	if (SUCCEED != (ret = get_data_from_proxy(proxy, TRX_PROTO_VALUE_PROXY_TASKS, &answer, &ts)))
		goto out;

	proxy->lastaccess = time(NULL);

	ret = proxy_process_proxy_data(proxy, answer, &ts, &more);

	trx_free(answer);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_proxy                                                    *
 *                                                                            *
 * Purpose: retrieve values of metrics from monitored hosts                   *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static int	process_proxy(void)
{
	DC_PROXY	proxy, proxy_old;
	int		num, i;
	time_t		now;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (0 == (num = DCconfig_get_proxypoller_hosts(&proxy, 1)))
		goto exit;

	now = time(NULL);

	for (i = 0; i < num; i++)
	{
		int		ret = FAIL;
		unsigned char	update_nextcheck = 0;

		memcpy(&proxy_old, &proxy, sizeof(DC_PROXY));

		if (proxy.proxy_config_nextcheck <= now)
			update_nextcheck |= TRX_PROXY_CONFIG_NEXTCHECK;
		if (proxy.proxy_data_nextcheck <= now)
			update_nextcheck |= TRX_PROXY_DATA_NEXTCHECK;
		if (proxy.proxy_tasks_nextcheck <= now)
			update_nextcheck |= TRX_PROXY_TASKS_NEXTCHECK;

		/* Check if passive proxy has been misconfigured on the server side. If it has happened more */
		/* recently than last synchronisation of cache then there is no point to retry connecting to */
		/* proxy again. The next reconnection attempt will happen after cache synchronisation. */
		if (proxy.last_cfg_error_time < DCconfig_get_last_sync_time())
		{
			char	*port = NULL;

			proxy.addr = proxy.addr_orig;

			port = trx_strdup(port, proxy.port_orig);
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
					&port, MACRO_TYPE_COMMON, NULL, 0);
			if (FAIL == is_ushort(port, &proxy.port))
			{
				treegix_log(LOG_LEVEL_ERR, "invalid proxy \"%s\" port: \"%s\"", proxy.host, port);
				ret = CONFIG_ERROR;
				trx_free(port);
				goto error;
			}
			trx_free(port);

			if (proxy.proxy_config_nextcheck <= now)
			{
				if (SUCCEED != (ret = proxy_send_configuration(&proxy)))
					goto error;
			}

			if (proxy.proxy_data_nextcheck <= now)
			{
				int	more;

				do
				{
					if (SUCCEED != (ret = proxy_get_data(&proxy, &more)))
						goto error;
				}
				while (TRX_PROXY_DATA_MORE == more);
			}
			else if (proxy.proxy_tasks_nextcheck <= now)
			{
				if (SUCCEED != (ret = proxy_get_tasks(&proxy)))
					goto error;
			}
		}
error:
		if (proxy_old.version != proxy.version || proxy_old.auto_compress != proxy.auto_compress ||
				proxy_old.lastaccess != proxy.lastaccess)
		{
			trx_update_proxy_data(&proxy_old, proxy.version, proxy.lastaccess, proxy.auto_compress);
		}

		DCrequeue_proxy(proxy.hostid, update_nextcheck, ret);
	}
exit:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return num;
}

TRX_THREAD_ENTRY(proxypoller_thread, args)
{
	int	nextcheck, sleeptime = -1, processed = 0, old_processed = 0;
	double	sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t	last_stat_time;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_init_child();
#endif
	trx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);
	last_stat_time = time(NULL);

	DBconnect(TRX_DB_CONNECT_NORMAL);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();
		trx_update_env(sec);

		if (0 != sleeptime)
		{
			trx_setproctitle("%s #%d [exchanged data with %d proxies in " TRX_FS_DBL " sec,"
					" exchanging data]", get_process_type_string(process_type), process_num,
					old_processed, old_total_sec);
		}

		processed += process_proxy();
		total_sec += trx_time() - sec;

		nextcheck = DCconfig_get_proxypoller_nextcheck();
		sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				trx_setproctitle("%s #%d [exchanged data with %d proxies in " TRX_FS_DBL " sec,"
						" exchanging data]", get_process_type_string(process_type), process_num,
						processed, total_sec);
			}
			else
			{
				trx_setproctitle("%s #%d [exchanged data with %d proxies in " TRX_FS_DBL " sec,"
						" idle %d sec]", get_process_type_string(process_type), process_num,
						processed, total_sec, sleeptime);
				old_processed = processed;
				old_total_sec = total_sec;
			}
			processed = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		trx_sleep_loop(sleeptime);
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}
