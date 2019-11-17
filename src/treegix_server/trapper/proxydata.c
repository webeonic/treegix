

#include "common.h"
#include "db.h"
#include "log.h"
#include "proxy.h"

#include "proxydata.h"
#include "../../libs/zbxcrypto/tls_tcp_active.h"
#include "zbxtasks.h"
#include "mutexs.h"
#include "daemon.h"

extern unsigned char	program_type;
static zbx_mutex_t	proxy_lock = TRX_MUTEX_NULL;

#define	LOCK_PROXY_HISTORY	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE)) zbx_mutex_lock(proxy_lock)
#define	UNLOCK_PROXY_HISTORY	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE)) zbx_mutex_unlock(proxy_lock)

int	zbx_send_proxy_data_response(const DC_PROXY *proxy, zbx_socket_t *sock, const char *info)
{
	struct zbx_json		json;
	zbx_vector_ptr_t	tasks;
	int			ret, flags = TRX_TCP_PROTOCOL;

	zbx_vector_ptr_create(&tasks);

	zbx_tm_get_remote_tasks(&tasks, proxy->hostid);

	zbx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	zbx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS, TRX_JSON_TYPE_STRING);

	if (NULL != info && '\0' != *info)
		zbx_json_addstring(&json, TRX_PROTO_TAG_INFO, info, TRX_JSON_TYPE_STRING);

	if (0 != tasks.values_num)
		zbx_tm_json_serialize_tasks(&json, &tasks);

	if (0 != proxy->auto_compress)
		flags |= TRX_TCP_COMPRESS;

	if (SUCCEED == (ret = zbx_tcp_send_ext(sock, json.buffer, strlen(json.buffer), flags, 0)))
	{
		if (0 != tasks.values_num)
			zbx_tm_update_task_status(&tasks, TRX_TM_STATUS_INPROGRESS);
	}

	zbx_json_free(&json);

	zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
	zbx_vector_ptr_destroy(&tasks);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_recv_proxy_data                                              *
 *                                                                            *
 * Purpose: receive 'proxy data' request from proxy                           *
 *                                                                            *
 * Parameters: sock - [IN] the connection socket                              *
 *             jp   - [IN] the received JSON data                             *
 *             ts   - [IN] the connection timestamp                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_recv_proxy_data(zbx_socket_t *sock, struct zbx_json_parse *jp, zbx_timespec_t *ts)
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

	if (SUCCEED != (status = zbx_proxy_check_permissions(&proxy, sock, &error)))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
		goto out;
	}

	zbx_update_proxy_data(&proxy, zbx_get_proxy_protocol_version(jp), time(NULL),
			(0 != (sock->protocol & TRX_TCP_COMPRESS) ? 1 : 0));

	if (SUCCEED != zbx_check_protocol_version(&proxy))
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
		error = zbx_strdup(error, "Treegix server shutdown in progress");
		treegix_log(LOG_LEVEL_WARNING, "cannot process proxy data from active proxy at \"%s\": %s",
				sock->peer, error);
		ret = status = FAIL;
		goto out;
	}
	else
		zbx_send_proxy_data_response(&proxy, sock, error);

out:
	if (FAIL == ret)
	{
		int	flags = TRX_TCP_PROTOCOL;

		if (0 != (sock->protocol & TRX_TCP_COMPRESS))
			flags |= TRX_TCP_COMPRESS;

		zbx_send_response_ext(sock, status, error, NULL, flags, CONFIG_TIMEOUT);
	}

	zbx_free(error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));
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
static int	send_data_to_server(zbx_socket_t *sock, const char *data, char **error)
{
	if (SUCCEED != zbx_tcp_send_ext(sock, data, strlen(data), TRX_TCP_PROTOCOL | TRX_TCP_COMPRESS, CONFIG_TIMEOUT))
	{
		*error = zbx_strdup(*error, zbx_socket_strerror());
		return FAIL;
	}

	if (SUCCEED != zbx_recv_response(sock, CONFIG_TIMEOUT, error))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_send_proxy_data                                              *
 *                                                                            *
 * Purpose: sends 'proxy data' request to server                              *
 *                                                                            *
 * Parameters: sock - [IN] the connection socket                              *
 *             ts   - [IN] the connection timestamp                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_send_proxy_data(zbx_socket_t *sock, zbx_timespec_t *ts)
{
	struct zbx_json		j;
	zbx_uint64_t		areg_lastid = 0, history_lastid = 0, discovery_lastid = 0;
	char			*error = NULL;
	int			availability_ts, more_history, more_discovery, more_areg;
	zbx_vector_ptr_t	tasks;
	struct zbx_json_parse	jp, jp_tasks;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != check_access_passive_proxy(sock, TRX_DO_NOT_SEND_RESPONSE, "proxy data request"))
	{
		/* do not send any reply to server in this case as the server expects proxy data */
		goto out;
	}

	LOCK_PROXY_HISTORY;
	zbx_json_init(&j, TRX_JSON_STAT_BUF_LEN);

	zbx_json_addstring(&j, TRX_PROTO_TAG_SESSION, zbx_dc_get_session_token(), TRX_JSON_TYPE_STRING);
	get_host_availability_data(&j, &availability_ts);
	proxy_get_hist_data(&j, &history_lastid, &more_history);
	proxy_get_dhis_data(&j, &discovery_lastid, &more_discovery);
	proxy_get_areg_data(&j, &areg_lastid, &more_areg);

	zbx_vector_ptr_create(&tasks);
	zbx_tm_get_remote_tasks(&tasks, 0);

	if (0 != tasks.values_num)
		zbx_tm_json_serialize_tasks(&j, &tasks);

	if (TRX_PROXY_DATA_MORE == more_history || TRX_PROXY_DATA_MORE == more_discovery ||
			TRX_PROXY_DATA_MORE == more_areg)
	{
		zbx_json_adduint64(&j, TRX_PROTO_TAG_MORE, TRX_PROXY_DATA_MORE);
	}

	zbx_json_addstring(&j, TRX_PROTO_TAG_VERSION, TREEGIX_VERSION, TRX_JSON_TYPE_STRING);
	zbx_json_adduint64(&j, TRX_PROTO_TAG_CLOCK, ts->sec);
	zbx_json_adduint64(&j, TRX_PROTO_TAG_NS, ts->ns);

	if (SUCCEED == send_data_to_server(sock, j.buffer, &error))
	{
		zbx_set_availability_diff_ts(availability_ts);

		DBbegin();

		if (0 != history_lastid)
			proxy_set_hist_lastid(history_lastid);

		if (0 != discovery_lastid)
			proxy_set_dhis_lastid(discovery_lastid);

		if (0 != areg_lastid)
			proxy_set_areg_lastid(areg_lastid);

		if (0 != tasks.values_num)
		{
			zbx_tm_update_task_status(&tasks, TRX_TM_STATUS_DONE);
			zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
		}

		if (SUCCEED == zbx_json_open(sock->buffer, &jp))
		{
			if (SUCCEED == zbx_json_brackets_by_name(&jp, TRX_PROTO_TAG_TASKS, &jp_tasks))
			{
				zbx_tm_json_deserialize_tasks(&jp_tasks, &tasks);
				zbx_tm_save_tasks(&tasks);
			}
		}

		DBcommit();
	}
	else
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot send proxy data to server at \"%s\": %s", sock->peer, error);
		zbx_free(error);
	}

	zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
	zbx_vector_ptr_destroy(&tasks);

	zbx_json_free(&j);
	UNLOCK_PROXY_HISTORY;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_send_task_data                                               *
 *                                                                            *
 * Purpose: sends 'proxy data' request to server                              *
 *                                                                            *
 * Parameters: sock - [IN] the connection socket                              *
 *             ts   - [IN] the connection timestamp                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_send_task_data(zbx_socket_t *sock, zbx_timespec_t *ts)
{
	struct zbx_json		j;
	char			*error = NULL;
	zbx_vector_ptr_t	tasks;
	struct zbx_json_parse	jp, jp_tasks;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != check_access_passive_proxy(sock, TRX_DO_NOT_SEND_RESPONSE, "proxy data request"))
	{
		/* do not send any reply to server in this case as the server expects proxy data */
		goto out;
	}

	zbx_json_init(&j, TRX_JSON_STAT_BUF_LEN);

	zbx_vector_ptr_create(&tasks);
	zbx_tm_get_remote_tasks(&tasks, 0);

	if (0 != tasks.values_num)
		zbx_tm_json_serialize_tasks(&j, &tasks);

	zbx_json_addstring(&j, TRX_PROTO_TAG_VERSION, TREEGIX_VERSION, TRX_JSON_TYPE_STRING);
	zbx_json_adduint64(&j, TRX_PROTO_TAG_CLOCK, ts->sec);
	zbx_json_adduint64(&j, TRX_PROTO_TAG_NS, ts->ns);

	if (SUCCEED == send_data_to_server(sock, j.buffer, &error))
	{
		DBbegin();

		if (0 != tasks.values_num)
		{
			zbx_tm_update_task_status(&tasks, TRX_TM_STATUS_DONE);
			zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
		}

		if (SUCCEED == zbx_json_open(sock->buffer, &jp))
		{
			if (SUCCEED == zbx_json_brackets_by_name(&jp, TRX_PROTO_TAG_TASKS, &jp_tasks))
			{
				zbx_tm_json_deserialize_tasks(&jp_tasks, &tasks);
				zbx_tm_save_tasks(&tasks);
			}
		}

		DBcommit();
	}
	else
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot send task data to server at \"%s\": %s", sock->peer, error);
		zbx_free(error);
	}

	zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
	zbx_vector_ptr_destroy(&tasks);

	zbx_json_free(&j);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

int	init_proxy_history_lock(char **error)
{
	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE))
		return zbx_mutex_create(&proxy_lock, TRX_MUTEX_PROXY_HISTORY, error);

	return SUCCEED;
}

void	free_proxy_history_lock(void)
{
	if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE))
		zbx_mutex_destroy(&proxy_lock);
}

