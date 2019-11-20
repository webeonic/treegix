

#include "common.h"
#include "db.h"
#include "log.h"
#include "proxy.h"

#include "proxydata.h"
#include "../../libs/trxcrypto/tls_tcp_active.h"
#include "trxtasks.h"
#include "mutexs.h"
#include "daemon.h"

extern unsigned char	program_type;
static trx_mutex_t	proxy_lock = TRX_MUTEX_NULL;

#define	LOCK_PROXY_HISTORY	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE)) trx_mutex_lock(proxy_lock)
#define	UNLOCK_PROXY_HISTORY	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE)) trx_mutex_unlock(proxy_lock)

int	trx_send_proxy_data_response(const DC_PROXY *proxy, trx_socket_t *sock, const char *info)
{
	struct trx_json		json;
	trx_vector_ptr_t	tasks;
	int			ret, flags = TRX_TCP_PROTOCOL;

	trx_vector_ptr_create(&tasks);

	trx_tm_get_remote_tasks(&tasks, proxy->hostid);

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS, TRX_JSON_TYPE_STRING);

	if (NULL != info && '\0' != *info)
		trx_json_addstring(&json, TRX_PROTO_TAG_INFO, info, TRX_JSON_TYPE_STRING);

	if (0 != tasks.values_num)
		trx_tm_json_serialize_tasks(&json, &tasks);

	if (0 != proxy->auto_compress)
		flags |= TRX_TCP_COMPRESS;

	if (SUCCEED == (ret = trx_tcp_send_ext(sock, json.buffer, strlen(json.buffer), flags, 0)))
	{
		if (0 != tasks.values_num)
			trx_tm_update_task_status(&tasks, TRX_TM_STATUS_INPROGRESS);
	}

	trx_json_free(&json);

	trx_vector_ptr_clear_ext(&tasks, (trx_clean_func_t)trx_tm_task_free);
	trx_vector_ptr_destroy(&tasks);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_recv_proxy_data                                              *
 *                                                                            *
 * Purpose: receive 'proxy data' request from proxy                           *
 *                                                                            *
 * Parameters: sock - [IN] the connection socket                              *
 *             jp   - [IN] the received JSON data                             *
 *             ts   - [IN] the connection timestamp                           *
 *                                                                            *
 ******************************************************************************/
void	trx_recv_proxy_data(trx_socket_t *sock, struct trx_json_parse *jp, trx_timespec_t *ts)
{
	int		ret = FAIL, status;
	char		*error = NULL;
	DC_PROXY	proxy;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (status = get_active_proxy_from_request(jp, &proxy, &error)))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot parse proxy data from active proxy at \"%s\": %s",
				sock->peer, error);
		goto out;
	}

	if (SUCCEED != (status = trx_proxy_check_permissions(&proxy, sock, &error)))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
		goto out;
	}

	trx_update_proxy_data(&proxy, trx_get_proxy_protocol_version(jp), time(NULL),
			(0 != (sock->protocol & TRX_TCP_COMPRESS) ? 1 : 0));

	if (SUCCEED != trx_check_protocol_version(&proxy))
	{
		goto out;
	}

	if (SUCCEED != (ret = process_proxy_data(&proxy, jp, ts, &error)))
	{
		treegix_log(LOG_LEVEL_WARNING, "received invalid proxy data from proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, error);
		status = FAIL;
		goto out;
	}

	if (!TRX_IS_RUNNING())
	{
		error = trx_strdup(error, "Treegix server shutdown in progress");
		treegix_log(LOG_LEVEL_WARNING, "cannot process proxy data from active proxy at \"%s\": %s",
				sock->peer, error);
		ret = status = FAIL;
		goto out;
	}
	else
		trx_send_proxy_data_response(&proxy, sock, error);

out:
	if (FAIL == ret)
	{
		int	flags = TRX_TCP_PROTOCOL;

		if (0 != (sock->protocol & TRX_TCP_COMPRESS))
			flags |= TRX_TCP_COMPRESS;

		trx_send_response_ext(sock, status, error, NULL, flags, CONFIG_TIMEOUT);
	}

	trx_free(error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));
}

/******************************************************************************
 *                                                                            *
 * Function: send_data_to_server                                              *
 *                                                                            *
 * Purpose: sends data from proxy to server                                   *
 *                                                                            *
 * Parameters: sock  - [IN] the connection socket                             *
 *             data  - [IN] the data to send                                  *
 *             error - [OUT] the error message                                *
 *                                                                            *
 ******************************************************************************/
static int	send_data_to_server(trx_socket_t *sock, const char *data, char **error)
{
	if (SUCCEED != trx_tcp_send_ext(sock, data, strlen(data), TRX_TCP_PROTOCOL | TRX_TCP_COMPRESS, CONFIG_TIMEOUT))
	{
		*error = trx_strdup(*error, trx_socket_strerror());
		return FAIL;
	}

	if (SUCCEED != trx_recv_response(sock, CONFIG_TIMEOUT, error))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_send_proxy_data                                              *
 *                                                                            *
 * Purpose: sends 'proxy data' request to server                              *
 *                                                                            *
 * Parameters: sock - [IN] the connection socket                              *
 *             ts   - [IN] the connection timestamp                           *
 *                                                                            *
 ******************************************************************************/
void	trx_send_proxy_data(trx_socket_t *sock, trx_timespec_t *ts)
{
	struct trx_json		j;
	trx_uint64_t		areg_lastid = 0, history_lastid = 0, discovery_lastid = 0;
	char			*error = NULL;
	int			availability_ts, more_history, more_discovery, more_areg;
	trx_vector_ptr_t	tasks;
	struct trx_json_parse	jp, jp_tasks;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != check_access_passive_proxy(sock, TRX_DO_NOT_SEND_RESPONSE, "proxy data request"))
	{
		/* do not send any reply to server in this case as the server expects proxy data */
		goto out;
	}

	LOCK_PROXY_HISTORY;
	trx_json_init(&j, TRX_JSON_STAT_BUF_LEN);

	trx_json_addstring(&j, TRX_PROTO_TAG_SESSION, trx_dc_get_session_token(), TRX_JSON_TYPE_STRING);
	get_host_availability_data(&j, &availability_ts);
	proxy_get_hist_data(&j, &history_lastid, &more_history);
	proxy_get_dhis_data(&j, &discovery_lastid, &more_discovery);
	proxy_get_areg_data(&j, &areg_lastid, &more_areg);

	trx_vector_ptr_create(&tasks);
	trx_tm_get_remote_tasks(&tasks, 0);

	if (0 != tasks.values_num)
		trx_tm_json_serialize_tasks(&j, &tasks);

	if (TRX_PROXY_DATA_MORE == more_history || TRX_PROXY_DATA_MORE == more_discovery ||
			TRX_PROXY_DATA_MORE == more_areg)
	{
		trx_json_adduint64(&j, TRX_PROTO_TAG_MORE, TRX_PROXY_DATA_MORE);
	}

	trx_json_addstring(&j, TRX_PROTO_TAG_VERSION, TREEGIX_VERSION, TRX_JSON_TYPE_STRING);
	trx_json_adduint64(&j, TRX_PROTO_TAG_CLOCK, ts->sec);
	trx_json_adduint64(&j, TRX_PROTO_TAG_NS, ts->ns);

	if (SUCCEED == send_data_to_server(sock, j.buffer, &error))
	{
		trx_set_availability_diff_ts(availability_ts);

		DBbegin();

		if (0 != history_lastid)
			proxy_set_hist_lastid(history_lastid);

		if (0 != discovery_lastid)
			proxy_set_dhis_lastid(discovery_lastid);

		if (0 != areg_lastid)
			proxy_set_areg_lastid(areg_lastid);

		if (0 != tasks.values_num)
		{
			trx_tm_update_task_status(&tasks, TRX_TM_STATUS_DONE);
			trx_vector_ptr_clear_ext(&tasks, (trx_clean_func_t)trx_tm_task_free);
		}

		if (SUCCEED == trx_json_open(sock->buffer, &jp))
		{
			if (SUCCEED == trx_json_brackets_by_name(&jp, TRX_PROTO_TAG_TASKS, &jp_tasks))
			{
				trx_tm_json_deserialize_tasks(&jp_tasks, &tasks);
				trx_tm_save_tasks(&tasks);
			}
		}

		DBcommit();
	}
	else
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot send proxy data to server at \"%s\": %s", sock->peer, error);
		trx_free(error);
	}

	trx_vector_ptr_clear_ext(&tasks, (trx_clean_func_t)trx_tm_task_free);
	trx_vector_ptr_destroy(&tasks);

	trx_json_free(&j);
	UNLOCK_PROXY_HISTORY;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}


/******************************************************************************
 *                                                                            *
 * Function: trx_send_task_data                                               *
 *                                                                            *
 * Purpose: sends 'proxy data' request to server                              *
 *                                                                            *
 * Parameters: sock - [IN] the connection socket                              *
 *             ts   - [IN] the connection timestamp                           *
 *                                                                            *
 ******************************************************************************/
void	trx_send_task_data(trx_socket_t *sock, trx_timespec_t *ts)
{
	struct trx_json		j;
	char			*error = NULL;
	trx_vector_ptr_t	tasks;
	struct trx_json_parse	jp, jp_tasks;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != check_access_passive_proxy(sock, TRX_DO_NOT_SEND_RESPONSE, "proxy data request"))
	{
		/* do not send any reply to server in this case as the server expects proxy data */
		goto out;
	}

	trx_json_init(&j, TRX_JSON_STAT_BUF_LEN);

	trx_vector_ptr_create(&tasks);
	trx_tm_get_remote_tasks(&tasks, 0);

	if (0 != tasks.values_num)
		trx_tm_json_serialize_tasks(&j, &tasks);

	trx_json_addstring(&j, TRX_PROTO_TAG_VERSION, TREEGIX_VERSION, TRX_JSON_TYPE_STRING);
	trx_json_adduint64(&j, TRX_PROTO_TAG_CLOCK, ts->sec);
	trx_json_adduint64(&j, TRX_PROTO_TAG_NS, ts->ns);

	if (SUCCEED == send_data_to_server(sock, j.buffer, &error))
	{
		DBbegin();

		if (0 != tasks.values_num)
		{
			trx_tm_update_task_status(&tasks, TRX_TM_STATUS_DONE);
			trx_vector_ptr_clear_ext(&tasks, (trx_clean_func_t)trx_tm_task_free);
		}

		if (SUCCEED == trx_json_open(sock->buffer, &jp))
		{
			if (SUCCEED == trx_json_brackets_by_name(&jp, TRX_PROTO_TAG_TASKS, &jp_tasks))
			{
				trx_tm_json_deserialize_tasks(&jp_tasks, &tasks);
				trx_tm_save_tasks(&tasks);
			}
		}

		DBcommit();
	}
	else
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot send task data to server at \"%s\": %s", sock->peer, error);
		trx_free(error);
	}

	trx_vector_ptr_clear_ext(&tasks, (trx_clean_func_t)trx_tm_task_free);
	trx_vector_ptr_destroy(&tasks);

	trx_json_free(&j);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

int	init_proxy_history_lock(char **error)
{
	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE))
		return trx_mutex_create(&proxy_lock, TRX_MUTEX_PROXY_HISTORY, error);

	return SUCCEED;
}

void	free_proxy_history_lock(void)
{
	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE))
		trx_mutex_destroy(&proxy_lock);
}

