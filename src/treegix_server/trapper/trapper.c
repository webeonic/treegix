

#include "common.h"

#include "comms.h"
#include "log.h"
#include "trxjson.h"
#include "trxserver.h"
#include "dbcache.h"
#include "proxy.h"
#include "trxself.h"

#include "trapper.h"
#include "active.h"
#include "nodecommand.h"
#include "proxyconfig.h"
#include "proxydata.h"
#include "../alerter/alerter_protocol.h"
#include "trapper_preproc.h"

#include "daemon.h"
#include "../../libs/trxcrypto/tls.h"
#include "../../libs/trxserver/treegix_stats.h"
#include "trxipcservice.h"

#define TRX_MAX_SECTION_ENTRIES		4
#define TRX_MAX_ENTRY_ATTRIBUTES	3

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;
extern size_t		(*find_psk_in_cache)(const unsigned char *, unsigned char *, unsigned int *);

typedef struct
{
	trx_counter_value_t	online;
	trx_counter_value_t	offline;
}
trx_user_stats_t;

typedef union
{
	trx_counter_value_t	counter;	/* single global counter */
	trx_vector_ptr_t	counters;	/* array of per proxy counters */
}
trx_entry_info_t;

typedef struct
{
	const char	*name;
	trx_uint64_t	value;
}
trx_entry_attribute_t;

typedef struct
{
	trx_entry_info_t	*info;
	trx_counter_type_t	counter_type;
	trx_entry_attribute_t	attributes[TRX_MAX_ENTRY_ATTRIBUTES];
}
trx_section_entry_t;

typedef enum
{
	TRX_SECTION_ENTRY_THE_ONLY,
	TRX_SECTION_ENTRY_PER_PROXY
}
trx_entry_type_t;

typedef struct
{
	const char		*name;
	trx_entry_type_t	entry_type;
	trx_user_type_t		access_level;
	int			*res;
	trx_section_entry_t	entries[TRX_MAX_SECTION_ENTRIES];
}
trx_status_section_t;

/******************************************************************************
 *                                                                            *
 * Function: recv_agenthistory                                                *
 *                                                                            *
 * Purpose: processes the received values from active agents                  *
 *                                                                            *
 ******************************************************************************/
static void	recv_agenthistory(trx_socket_t *sock, struct trx_json_parse *jp, trx_timespec_t *ts)
{
	char	*info = NULL;
	int	ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (ret = process_agent_history_data(sock, jp, ts, &info)))
	{
		treegix_log(LOG_LEVEL_WARNING, "received invalid agent history data from \"%s\": %s", sock->peer, info);
	}
	else if (!TRX_IS_RUNNING())
	{
		info = trx_strdup(info, "Treegix server shutdown in progress");
		treegix_log(LOG_LEVEL_WARNING, "cannot receive agent history data from \"%s\": %s", sock->peer, info);
		ret = FAIL;
	}

	trx_send_response(sock, ret, info, CONFIG_TIMEOUT);

	trx_free(info);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: recv_senderhistory                                               *
 *                                                                            *
 * Purpose: processes the received values from senders                        *
 *                                                                            *
 ******************************************************************************/
static void	recv_senderhistory(trx_socket_t *sock, struct trx_json_parse *jp, trx_timespec_t *ts)
{
	char	*info = NULL;
	int	ret;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (ret = process_sender_history_data(sock, jp, ts, &info)))
	{
		treegix_log(LOG_LEVEL_WARNING, "received invalid sender data from \"%s\": %s", sock->peer, info);
	}
	else if (!TRX_IS_RUNNING())
	{
		info = trx_strdup(info, "Treegix server shutdown in progress");
		treegix_log(LOG_LEVEL_WARNING, "cannot process sender data from \"%s\": %s", sock->peer, info);
		ret = FAIL;
	}

	trx_send_response(sock, ret, info, CONFIG_TIMEOUT);

	trx_free(info);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: recv_proxy_heartbeat                                             *
 *                                                                            *
 * Purpose: process heartbeat sent by proxy servers                           *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	recv_proxy_heartbeat(trx_socket_t *sock, struct trx_json_parse *jp)
{
	char		*error = NULL;
	int		ret, flags = TRX_TCP_PROTOCOL;
	DC_PROXY	proxy;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != (ret = get_active_proxy_from_request(jp, &proxy, &error)))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot parse heartbeat from active proxy at \"%s\": %s",
				sock->peer, error);
		goto out;
	}

	if (SUCCEED != (ret = trx_proxy_check_permissions(&proxy, sock, &error)))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
		goto out;
	}

	trx_update_proxy_data(&proxy, trx_get_proxy_protocol_version(jp), time(NULL),
			(0 != (sock->protocol & TRX_TCP_COMPRESS) ? 1 : 0));

	if (0 != proxy.auto_compress)
		flags |= TRX_TCP_COMPRESS;
out:
	if (FAIL == ret && 0 != (sock->protocol & TRX_TCP_COMPRESS))
		flags |= TRX_TCP_COMPRESS;

	trx_send_response_ext(sock, ret, error, NULL, flags, CONFIG_TIMEOUT);

	trx_free(error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

#define TRX_GET_QUEUE_UNKNOWN		-1
#define TRX_GET_QUEUE_OVERVIEW		0
#define TRX_GET_QUEUE_PROXY		1
#define TRX_GET_QUEUE_DETAILS		2

/* queue stats split by delay times */
typedef struct
{
	trx_uint64_t	id;
	int		delay5;
	int		delay10;
	int		delay30;
	int		delay60;
	int		delay300;
	int		delay600;
}
trx_queue_stats_t;

/******************************************************************************
 *                                                                            *
 * Function: queue_stats_update                                               *
 *                                                                            *
 * Purpose: update queue stats with a new item delay                          *
 *                                                                            *
 * Parameters: stats   - [IN] the queue stats                                 *
 *             delay   - [IN] the delay time of an delayed item               *
 *                                                                            *
 ******************************************************************************/
static void	queue_stats_update(trx_queue_stats_t *stats, int delay)
{
	if (10 >= delay)
		stats->delay5++;
	else if (30 >= delay)
		stats->delay10++;
	else if (60 >= delay)
		stats->delay30++;
	else if (300 >= delay)
		stats->delay60++;
	else if (600 >= delay)
		stats->delay300++;
	else
		stats->delay600++;
}

/******************************************************************************
 *                                                                            *
 * Function: queue_stats_export                                               *
 *                                                                            *
 * Purpose: export queue stats to JSON format                                 *
 *                                                                            *
 * Parameters: queue_stats - [IN] a hashset containing item stats             *
 *             id_name     - [IN] the name of stats id field                  *
 *             json        - [OUT] the output JSON                            *
 *                                                                            *
 ******************************************************************************/
static void	queue_stats_export(trx_hashset_t *queue_stats, const char *id_name, struct trx_json *json)
{
	trx_hashset_iter_t	iter;
	trx_queue_stats_t	*stats;

	trx_json_addarray(json, TRX_PROTO_TAG_DATA);

	trx_hashset_iter_reset(queue_stats, &iter);

	while (NULL != (stats = (trx_queue_stats_t *)trx_hashset_iter_next(&iter)))
	{
		trx_json_addobject(json, NULL);
		trx_json_adduint64(json, id_name, stats->id);
		trx_json_adduint64(json, "delay5", stats->delay5);
		trx_json_adduint64(json, "delay10", stats->delay10);
		trx_json_adduint64(json, "delay30", stats->delay30);
		trx_json_adduint64(json, "delay60", stats->delay60);
		trx_json_adduint64(json, "delay300", stats->delay300);
		trx_json_adduint64(json, "delay600", stats->delay600);
		trx_json_close(json);
	}

	trx_json_close(json);
}

/* queue item comparison function used to sort queue by nextcheck */
static int	queue_compare_by_nextcheck_asc(trx_queue_item_t **d1, trx_queue_item_t **d2)
{
	trx_queue_item_t	*i1 = *d1, *i2 = *d2;

	return i1->nextcheck - i2->nextcheck;
}

/******************************************************************************
 *                                                                            *
 * Function: recv_getqueue                                                    *
 *                                                                            *
 * Purpose: process queue request                                             *
 *                                                                            *
 * Parameters:  sock  - [IN] the request socket                               *
 *              jp    - [IN] the request data                                 *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static int	recv_getqueue(trx_socket_t *sock, struct trx_json_parse *jp)
{
	int			ret = FAIL, request_type = TRX_GET_QUEUE_UNKNOWN, now, i, limit;
	char			type[MAX_STRING_LEN], sessionid[MAX_STRING_LEN], limit_str[MAX_STRING_LEN];
	trx_user_t		user;
	trx_vector_ptr_t	queue;
	struct trx_json		json;
	trx_hashset_t		queue_stats;
	trx_queue_stats_t	*stats;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (FAIL == trx_json_value_by_name(jp, TRX_PROTO_TAG_SID, sessionid, sizeof(sessionid)) ||
			SUCCEED != DBget_user_by_active_session(sessionid, &user) || USER_TYPE_SUPER_ADMIN > user.type)
	{
		trx_send_response(sock, ret, "Permission denied.", CONFIG_TIMEOUT);
		goto out;
	}

	if (FAIL != trx_json_value_by_name(jp, TRX_PROTO_TAG_TYPE, type, sizeof(type)))
	{
		if (0 == strcmp(type, TRX_PROTO_VALUE_GET_QUEUE_OVERVIEW))
		{
			request_type = TRX_GET_QUEUE_OVERVIEW;
		}
		else if (0 == strcmp(type, TRX_PROTO_VALUE_GET_QUEUE_PROXY))
		{
			request_type = TRX_GET_QUEUE_PROXY;
		}
		else if (0 == strcmp(type, TRX_PROTO_VALUE_GET_QUEUE_DETAILS))
		{
			request_type = TRX_GET_QUEUE_DETAILS;

			if (FAIL == trx_json_value_by_name(jp, TRX_PROTO_TAG_LIMIT, limit_str, sizeof(limit_str)) ||
					FAIL == is_uint31(limit_str, &limit))
			{
				trx_send_response(sock, ret, "Unsupported limit value.", CONFIG_TIMEOUT);
				goto out;
			}
		}
	}

	if (TRX_GET_QUEUE_UNKNOWN == request_type)
	{
		trx_send_response(sock, ret, "Unsupported request type.", CONFIG_TIMEOUT);
		goto out;
	}

	now = time(NULL);
	trx_vector_ptr_create(&queue);
	DCget_item_queue(&queue, TRX_QUEUE_FROM_DEFAULT, TRX_QUEUE_TO_INFINITY);

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	switch (request_type)
	{
		case TRX_GET_QUEUE_OVERVIEW:
			trx_hashset_create(&queue_stats, 32, TRX_DEFAULT_UINT64_HASH_FUNC,
					TRX_DEFAULT_UINT64_COMPARE_FUNC);

			/* gather queue stats by item type */
			for (i = 0; i < queue.values_num; i++)
			{
				trx_queue_item_t	*item = (trx_queue_item_t *)queue.values[i];
				trx_uint64_t		id = item->type;

				if (NULL == (stats = (trx_queue_stats_t *)trx_hashset_search(&queue_stats, &id)))
				{
					trx_queue_stats_t	data = {.id = id};

					stats = (trx_queue_stats_t *)trx_hashset_insert(&queue_stats, &data, sizeof(data));
				}
				queue_stats_update(stats, now - item->nextcheck);
			}

			trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS,
					TRX_JSON_TYPE_STRING);
			queue_stats_export(&queue_stats, "itemtype", &json);
			trx_hashset_destroy(&queue_stats);

			break;
		case TRX_GET_QUEUE_PROXY:
			trx_hashset_create(&queue_stats, 32, TRX_DEFAULT_UINT64_HASH_FUNC,
					TRX_DEFAULT_UINT64_COMPARE_FUNC);

			/* gather queue stats by proxy hostid */
			for (i = 0; i < queue.values_num; i++)
			{
				trx_queue_item_t	*item = (trx_queue_item_t *)queue.values[i];
				trx_uint64_t		id = item->proxy_hostid;

				if (NULL == (stats = (trx_queue_stats_t *)trx_hashset_search(&queue_stats, &id)))
				{
					trx_queue_stats_t	data = {.id = id};

					stats = (trx_queue_stats_t *)trx_hashset_insert(&queue_stats, &data, sizeof(data));
				}
				queue_stats_update(stats, now - item->nextcheck);
			}

			trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS,
					TRX_JSON_TYPE_STRING);
			queue_stats_export(&queue_stats, "proxyid", &json);
			trx_hashset_destroy(&queue_stats);

			break;
		case TRX_GET_QUEUE_DETAILS:
			trx_vector_ptr_sort(&queue, (trx_compare_func_t)queue_compare_by_nextcheck_asc);
			trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS,
					TRX_JSON_TYPE_STRING);
			trx_json_addarray(&json, TRX_PROTO_TAG_DATA);

			for (i = 0; i < queue.values_num && i < limit; i++)
			{
				trx_queue_item_t	*item = (trx_queue_item_t *)queue.values[i];

				trx_json_addobject(&json, NULL);
				trx_json_adduint64(&json, "itemid", item->itemid);
				trx_json_adduint64(&json, "nextcheck", item->nextcheck);
				trx_json_close(&json);
			}

			trx_json_close(&json);
			trx_json_adduint64(&json, "total", queue.values_num);

			break;
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s() json.buffer:'%s'", __func__, json.buffer);

	(void)trx_tcp_send(sock, json.buffer);

	DCfree_item_queue(&queue);
	trx_vector_ptr_destroy(&queue);

	trx_json_free(&json);

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: recv_alert_send                                                  *
 *                                                                            *
 * Purpose: process alert send request that is used to test media types       *
 *                                                                            *
 * Parameters:  sock  - [IN] the request socket                               *
 *              jp    - [IN] the request data                                 *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static void	recv_alert_send(trx_socket_t *sock, const struct trx_json_parse *jp)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			ret = FAIL, errcode;
	char			tmp[TRX_MAX_UINT64_LEN + 1], sessionid[MAX_STRING_LEN], *sendto = NULL, *subject = NULL,
				*message = NULL, *error = NULL, *params = NULL, *value = NULL;
	trx_uint64_t		mediatypeid;
	size_t			string_alloc;
	struct trx_json		json;
	struct trx_json_parse	jp_data, jp_params;
	unsigned char		*data = NULL,smtp_security, smtp_verify_peer, smtp_verify_host,
				smtp_authentication, content_type, *response = NULL;
	trx_uint32_t		size;
	trx_user_t		user;
	unsigned short		smtp_port;


	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_SID, sessionid, sizeof(sessionid)) ||
			SUCCEED != DBget_user_by_active_session(sessionid, &user) || USER_TYPE_SUPER_ADMIN > user.type)
	{
		error = trx_strdup(NULL, "Permission denied.");
		goto fail;
	}

	if (SUCCEED != trx_json_brackets_by_name(jp, TRX_PROTO_TAG_DATA, &jp_data))
	{
		error = trx_dsprintf(NULL, "Cannot parse request tag: %s.", TRX_PROTO_TAG_DATA);
		goto fail;
	}

	if (SUCCEED != trx_json_value_by_name(&jp_data, TRX_PROTO_TAG_MEDIATYPEID, tmp, sizeof(tmp)) ||
			SUCCEED != is_uint64(tmp, &mediatypeid))
	{
		error = trx_dsprintf(NULL, "Cannot parse request tag: %s.", TRX_PROTO_TAG_MEDIATYPEID);
		goto fail;
	}

	string_alloc = 0;
	if (SUCCEED == trx_json_value_by_name_dyn(&jp_data, TRX_PROTO_TAG_SENDTO, &sendto, &string_alloc))
		string_alloc = 0;
	if (SUCCEED == trx_json_value_by_name_dyn(&jp_data, TRX_PROTO_TAG_SUBJECT, &subject, &string_alloc))
		string_alloc = 0;
	if (SUCCEED == trx_json_value_by_name_dyn(&jp_data, TRX_PROTO_TAG_MESSAGE, &message, &string_alloc))
		string_alloc = 0;

	if (SUCCEED == trx_json_brackets_by_name(&jp_data, TRX_PROTO_TAG_PARAMETERS, &jp_params))
	{
		size_t	string_offset = 0;

		trx_strncpy_alloc(&params, &string_alloc, &string_offset, jp_params.start,
				jp_params.end - jp_params.start + 1);
	}

	result = DBselect("select type,smtp_server,smtp_helo,smtp_email,exec_path,gsm_modem,username,"
				"passwd,smtp_port,smtp_security,smtp_verify_peer,smtp_verify_host,smtp_authentication,"
				"exec_params,maxsessions,maxattempts,attempt_interval,content_type,script,timeout"
			" from media_type"
			" where mediatypeid=" TRX_FS_UI64, mediatypeid);

	if (NULL == (row = DBfetch(result)))
	{
		DBfree_result(result);
		error = trx_dsprintf(NULL, "Cannot find the specified media type.");
		goto fail;
	}

	if (FAIL == is_ushort(row[8], &smtp_port))
	{
		DBfree_result(result);
		error = trx_dsprintf(NULL, "Invalid port value.");
		goto fail;
	}

	TRX_STR2UCHAR(smtp_security, row[9]);
	TRX_STR2UCHAR(smtp_verify_peer, row[10]);
	TRX_STR2UCHAR(smtp_verify_host, row[11]);
	TRX_STR2UCHAR(smtp_authentication, row[12]);
	TRX_STR2UCHAR(content_type, row[17]);

	size = trx_alerter_serialize_alert_send(&data, mediatypeid, atoi(row[0]), row[1], row[2], row[3], row[4],
			row[5], row[6], row[7], smtp_port, smtp_security, smtp_verify_peer, smtp_verify_host,
			smtp_authentication, row[13], atoi(row[14]), atoi(row[15]), row[16], content_type, row[18],
			row[19], sendto, subject, message, params);

	DBfree_result(result);

	if (SUCCEED != trx_ipc_async_exchange(TRX_IPC_SERVICE_ALERTER, TRX_IPC_ALERTER_ALERT, SEC_PER_MIN, data, size,
			&response, &error))
	{
		goto fail;
	}

	trx_alerter_deserialize_result(response, &value, &errcode, &error);
	trx_free(response);

	if (SUCCEED != errcode)
		goto fail;

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);
	trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS, TRX_JSON_TYPE_STRING);
	if (NULL != value)
		trx_json_addstring(&json, TRX_PROTO_TAG_RESULT, value, TRX_JSON_TYPE_STRING);

	(void)trx_tcp_send(sock, json.buffer);

	trx_json_free(&json);

	ret = SUCCEED;
fail:
	if (SUCCEED != ret)
		trx_send_response(sock, FAIL, error, CONFIG_TIMEOUT);

	trx_free(params);
	trx_free(message);
	trx_free(subject);
	trx_free(sendto);
	trx_free(data);
	trx_free(value);
	trx_free(error);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));
}

static int	DBget_template_count(trx_uint64_t *count)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = FAIL;

	if (NULL == (result = DBselect("select count(*) from hosts where status=%d", HOST_STATUS_TEMPLATE)))
		goto out;

	if (NULL == (row = DBfetch(result)) || SUCCEED != is_uint64(row[0], count))
		goto out;

	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

static int	DBget_user_count(trx_uint64_t *count_online, trx_uint64_t *count_offline)
{
	DB_RESULT	result;
	DB_ROW		row;
	trx_uint64_t	users_offline, users_online = 0;
	int		now, ret = FAIL;

	if (NULL == (result = DBselect("select count(*) from users")))
		goto out;

	if (NULL == (row = DBfetch(result)) || SUCCEED != is_uint64(row[0], &users_offline))
		goto out;

	DBfree_result(result);
	now = time(NULL);

	if (NULL == (result = DBselect("select max(lastaccess) from sessions where status=%d group by userid,status",
			TRX_SESSION_ACTIVE)))
	{
		goto out;
	}

	while (NULL != (row = DBfetch(result)))
	{
		if (atoi(row[0]) + TRX_USER_ONLINE_TIME < now)
			continue;

		users_online++;

		if (0 == users_offline)	/* new user can be created and log in between two selects */
			continue;

		users_offline--;
	}

	*count_online = users_online;
	*count_offline = users_offline;
	ret = SUCCEED;
out:
	DBfree_result(result);

	return ret;
}

/* auxiliary variables for status_stats_export() */

static trx_entry_info_t	templates, hosts_monitored, hosts_not_monitored, items_active_normal, items_active_notsupported,
			items_disabled, triggers_enabled_ok, triggers_enabled_problem, triggers_disabled, users_online,
			users_offline, required_performance;
static int		templates_res, users_res;

static void	trx_status_counters_init(void)
{
	trx_vector_ptr_create(&hosts_monitored.counters);
	trx_vector_ptr_create(&hosts_not_monitored.counters);
	trx_vector_ptr_create(&items_active_normal.counters);
	trx_vector_ptr_create(&items_active_notsupported.counters);
	trx_vector_ptr_create(&items_disabled.counters);
	trx_vector_ptr_create(&required_performance.counters);
}

static void	trx_status_counters_free(void)
{
	trx_vector_ptr_clear_ext(&hosts_monitored.counters, trx_default_mem_free_func);
	trx_vector_ptr_clear_ext(&hosts_not_monitored.counters, trx_default_mem_free_func);
	trx_vector_ptr_clear_ext(&items_active_normal.counters, trx_default_mem_free_func);
	trx_vector_ptr_clear_ext(&items_active_notsupported.counters, trx_default_mem_free_func);
	trx_vector_ptr_clear_ext(&items_disabled.counters, trx_default_mem_free_func);
	trx_vector_ptr_clear_ext(&required_performance.counters, trx_default_mem_free_func);

	trx_vector_ptr_destroy(&hosts_monitored.counters);
	trx_vector_ptr_destroy(&hosts_not_monitored.counters);
	trx_vector_ptr_destroy(&items_active_normal.counters);
	trx_vector_ptr_destroy(&items_active_notsupported.counters);
	trx_vector_ptr_destroy(&items_disabled.counters);
	trx_vector_ptr_destroy(&required_performance.counters);
}

const trx_status_section_t	status_sections[] = {
/*	{SECTION NAME,			NUMBER OF SECTION ENTRIES	SECTION ACCESS LEVEL	SECTION READYNESS, */
/*		{                                                                                                  */
/*			{ENTRY INFORMATION,		COUNTER TYPE,                                              */
/*				{                                                                                  */
/*					{ATTR. NAME,	ATTRIBUTE VALUE},                                          */
/*					... (up to TRX_MAX_ENTRY_ATTRIBUTES)                                       */
/*				}                                                                                  */
/*			},                                                                                         */
/*			... (up to TRX_MAX_SECTION_ENTRIES)                                                        */
/*		}                                                                                                  */
/*	},                                                                                                         */
/*	...                                                                                                        */
	{"template stats",		TRX_SECTION_ENTRY_THE_ONLY,	USER_TYPE_TREEGIX_USER,	&templates_res,
		{
			{&templates,			TRX_COUNTER_TYPE_UI64,
				{
					{NULL}
				}
			},
			{NULL}
		}
	},
	{"host stats",			TRX_SECTION_ENTRY_PER_PROXY,	USER_TYPE_TREEGIX_USER,	NULL,
		{
			{&hosts_monitored,		TRX_COUNTER_TYPE_UI64,
				{
					{"status",	HOST_STATUS_MONITORED},
					{NULL}
				}
			},
			{&hosts_not_monitored,		TRX_COUNTER_TYPE_UI64,
				{
					{"status",	HOST_STATUS_NOT_MONITORED},
					{NULL}
				}
			},
			{NULL}
		}
	},
	{"item stats",			TRX_SECTION_ENTRY_PER_PROXY,	USER_TYPE_TREEGIX_USER,	NULL,
		{
			{&items_active_normal,		TRX_COUNTER_TYPE_UI64,
				{
					{"status",	ITEM_STATUS_ACTIVE},
					{"state",	ITEM_STATE_NORMAL},
					{NULL}
				}
			},
			{&items_active_notsupported,	TRX_COUNTER_TYPE_UI64,
				{
					{"status",	ITEM_STATUS_ACTIVE},
					{"state",	ITEM_STATE_NOTSUPPORTED},
					{NULL}
				}
			},
			{&items_disabled,		TRX_COUNTER_TYPE_UI64,
				{
					{"status",	ITEM_STATUS_DISABLED},
					{NULL}
				}
			},
			{NULL}
		}
	},
	{"trigger stats",		TRX_SECTION_ENTRY_THE_ONLY,	USER_TYPE_TREEGIX_USER,	NULL,
		{
			{&triggers_enabled_ok,		TRX_COUNTER_TYPE_UI64,
				{
					{"status",	TRIGGER_STATUS_ENABLED},
					{"value",	TRIGGER_VALUE_OK},
					{NULL}
				}
			},
			{&triggers_enabled_problem,	TRX_COUNTER_TYPE_UI64,
				{
					{"status",	TRIGGER_STATUS_ENABLED},
					{"value",	TRIGGER_VALUE_PROBLEM},
					{NULL}
				}
			},
			{&triggers_disabled,		TRX_COUNTER_TYPE_UI64,
				{
					{"status",	TRIGGER_STATUS_DISABLED},
					{NULL}
				}
			},
			{NULL}
		}
	},
	{"user stats",			TRX_SECTION_ENTRY_THE_ONLY,	USER_TYPE_TREEGIX_USER,	&users_res,
		{
			{&users_online,			TRX_COUNTER_TYPE_UI64,
				{
					{"status",	TRX_SESSION_ACTIVE},
					{NULL}
				}
			},
			{&users_offline,		TRX_COUNTER_TYPE_UI64,
				{
					{"status",	TRX_SESSION_PASSIVE},
					{NULL}
				}
			},
			{NULL}
		}
	},
	{"required performance",	TRX_SECTION_ENTRY_PER_PROXY,	USER_TYPE_SUPER_ADMIN,	NULL,
		{
			{&required_performance,		TRX_COUNTER_TYPE_DBL,
				{
					{NULL}
				}
			},
			{NULL}
		}
	},
	{NULL}
};

static void	status_entry_export(struct trx_json *json, const trx_section_entry_t *entry,
		trx_counter_value_t counter_value, const trx_uint64_t *proxyid)
{
	const trx_entry_attribute_t	*attribute;
	char				*tmp = NULL;

	trx_json_addobject(json, NULL);

	if (NULL != entry->attributes[0].name || NULL != proxyid)
	{
		trx_json_addobject(json, "attributes");

		if (NULL != proxyid)
			trx_json_adduint64(json, "proxyid", *proxyid);

		for (attribute = entry->attributes; NULL != attribute->name; attribute++)
			trx_json_adduint64(json, attribute->name, attribute->value);

		trx_json_close(json);
	}

	switch (entry->counter_type)
	{
		case TRX_COUNTER_TYPE_UI64:
			trx_json_adduint64(json, "count", counter_value.ui64);
			break;
		case TRX_COUNTER_TYPE_DBL:
			tmp = trx_dsprintf(tmp, TRX_FS_DBL, counter_value.dbl);
			trx_json_addstring(json, "count", tmp, TRX_JSON_TYPE_STRING);
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	trx_json_close(json);

	trx_free(tmp);
}

static void	status_stats_export(struct trx_json *json, trx_user_type_t access_level)
{
	const trx_status_section_t	*section;
	const trx_section_entry_t	*entry;
	int				i;

	trx_status_counters_init();

	/* get status information */

	templates_res = DBget_template_count(&templates.counter.ui64);
	users_res = DBget_user_count(&users_online.counter.ui64, &users_offline.counter.ui64);
	DCget_status(&hosts_monitored.counters, &hosts_not_monitored.counters, &items_active_normal.counters,
			&items_active_notsupported.counters, &items_disabled.counters,
			&triggers_enabled_ok.counter.ui64, &triggers_enabled_problem.counter.ui64,
			&triggers_disabled.counter.ui64, &required_performance.counters);

	/* add status information to JSON */
	for (section = status_sections; NULL != section->name; section++)
	{
		if (access_level < section->access_level)	/* skip sections user has no rights to access */
			continue;

		if (NULL != section->res && SUCCEED != *section->res)	/* skip section we have no information for */
			continue;

		trx_json_addarray(json, section->name);

		for (entry = section->entries; NULL != entry->info; entry++)
		{
			switch (section->entry_type)
			{
				case TRX_SECTION_ENTRY_THE_ONLY:
					status_entry_export(json, entry, entry->info->counter, NULL);
					break;
				case TRX_SECTION_ENTRY_PER_PROXY:
					for (i = 0; i < entry->info->counters.values_num; i++)
					{
						const trx_proxy_counter_t	*proxy_counter;

						proxy_counter = (trx_proxy_counter_t *)entry->info->counters.values[i];
						status_entry_export(json, entry, proxy_counter->counter_value,
								&proxy_counter->proxyid);
					}
					break;
				default:
					THIS_SHOULD_NEVER_HAPPEN;
			}
		}

		trx_json_close(json);
	}

	trx_status_counters_free();
}

/******************************************************************************
 *                                                                            *
 * Function: recv_getstatus                                                   *
 *                                                                            *
 * Purpose: process status request                                            *
 *                                                                            *
 * Parameters:  sock  - [IN] the request socket                               *
 *              jp    - [IN] the request data                                 *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static int	recv_getstatus(trx_socket_t *sock, struct trx_json_parse *jp)
{
#define TRX_GET_STATUS_UNKNOWN	-1
#define TRX_GET_STATUS_PING	0
#define TRX_GET_STATUS_FULL	1

	trx_user_t	user;
	int		ret = FAIL, request_type = TRX_GET_STATUS_UNKNOWN;
	char		type[MAX_STRING_LEN], sessionid[MAX_STRING_LEN];
	struct trx_json	json;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != trx_json_value_by_name(jp, TRX_PROTO_TAG_SID, sessionid, sizeof(sessionid)) ||
			SUCCEED != DBget_user_by_active_session(sessionid, &user))
	{
		trx_send_response(sock, ret, "Permission denied.", CONFIG_TIMEOUT);
		goto out;
	}

	if (SUCCEED == trx_json_value_by_name(jp, TRX_PROTO_TAG_TYPE, type, sizeof(type)))
	{
		if (0 == strcmp(type, TRX_PROTO_VALUE_GET_STATUS_PING))
		{
			request_type = TRX_GET_STATUS_PING;
		}
		else if (0 == strcmp(type, TRX_PROTO_VALUE_GET_STATUS_FULL))
		{
			request_type = TRX_GET_STATUS_FULL;
		}
	}

	if (TRX_GET_STATUS_UNKNOWN == request_type)
	{
		trx_send_response(sock, ret, "Unsupported request type.", CONFIG_TIMEOUT);
		goto out;
	}

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	switch (request_type)
	{
		case TRX_GET_STATUS_PING:
			trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS, TRX_JSON_TYPE_STRING);
			trx_json_addobject(&json, TRX_PROTO_TAG_DATA);
			trx_json_close(&json);
			break;
		case TRX_GET_STATUS_FULL:
			trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS, TRX_JSON_TYPE_STRING);
			trx_json_addobject(&json, TRX_PROTO_TAG_DATA);
			status_stats_export(&json, user.type);
			trx_json_close(&json);
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s() json.buffer:'%s'", __func__, json.buffer);

	(void)trx_tcp_send(sock, json.buffer);

	trx_json_free(&json);

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;

#undef TRX_GET_STATUS_UNKNOWN
#undef TRX_GET_STATUS_PING
#undef TRX_GET_STATUS_FULL
}

/******************************************************************************
 *                                                                            *
 * Function: send_internal_stats_json                                         *
 *                                                                            *
 * Purpose: process Treegix stats request                                      *
 *                                                                            *
 * Parameters: sock  - [IN] the request socket                                *
 *             jp    - [IN] the request data                                  *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
static int	send_internal_stats_json(trx_socket_t *sock, const struct trx_json_parse *jp)
{
	struct trx_json	json;
	char		type[MAX_STRING_LEN], error[MAX_STRING_LEN];
	int		ret = FAIL;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (NULL == CONFIG_STATS_ALLOWED_IP ||
			SUCCEED != trx_tcp_check_allowed_peers(sock, CONFIG_STATS_ALLOWED_IP))
	{
		treegix_log(LOG_LEVEL_WARNING, "failed to accept an incoming stats request: %s",
				NULL == CONFIG_STATS_ALLOWED_IP ? "StatsAllowedIP not set" : trx_socket_strerror());
		strscpy(error, "Permission denied.");
		goto out;
	}

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);

	if (SUCCEED == trx_json_value_by_name(jp, TRX_PROTO_TAG_TYPE, type, sizeof(type)) &&
			0 == strcmp(type, TRX_PROTO_VALUE_TREEGIX_STATS_QUEUE))
	{
		char			from_str[TRX_MAX_UINT64_LEN + 1], to_str[TRX_MAX_UINT64_LEN + 1];
		int			from = TRX_QUEUE_FROM_DEFAULT, to = TRX_QUEUE_TO_INFINITY;
		struct trx_json_parse	jp_data;

		if (SUCCEED != trx_json_brackets_by_name(jp, TRX_PROTO_TAG_PARAMS, &jp_data))
		{
			trx_snprintf(error, sizeof(error), "cannot find tag: %s", TRX_PROTO_TAG_PARAMS);
			goto param_error;
		}

		if (SUCCEED == trx_json_value_by_name(&jp_data, TRX_PROTO_TAG_FROM, from_str, sizeof(from_str))
				&& FAIL == is_time_suffix(from_str, &from, TRX_LENGTH_UNLIMITED))
		{
			strscpy(error, "invalid 'from' parameter");
			goto param_error;
		}

		if (SUCCEED == trx_json_value_by_name(&jp_data, TRX_PROTO_TAG_TO, to_str, sizeof(to_str)) &&
				FAIL == is_time_suffix(to_str, &to, TRX_LENGTH_UNLIMITED))
		{
			strscpy(error, "invalid 'to' parameter");
			goto param_error;
		}

		if (TRX_QUEUE_TO_INFINITY != to && from > to)
		{
			strscpy(error, "parameters represent an invalid interval");
			goto param_error;
		}

		trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS, TRX_JSON_TYPE_STRING);
		trx_json_adduint64(&json, TRX_PROTO_VALUE_TREEGIX_STATS_QUEUE, DCget_item_queue(NULL, from, to));
	}
	else
	{
		trx_json_addstring(&json, TRX_PROTO_TAG_RESPONSE, TRX_PROTO_VALUE_SUCCESS, TRX_JSON_TYPE_STRING);
		trx_json_addobject(&json, TRX_PROTO_TAG_DATA);

		trx_get_treegix_stats(&json);

		trx_json_close(&json);
	}

	(void)trx_tcp_send(sock, json.buffer);
	ret = SUCCEED;
param_error:
	trx_json_free(&json);
out:
	if (SUCCEED != ret)
		trx_send_response(sock, ret, error, CONFIG_TIMEOUT);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, trx_result_string(ret));

	return ret;
}

static void	active_passive_misconfig(trx_socket_t *sock)
{
	char	*msg = NULL;

	msg = trx_dsprintf(msg, "misconfiguration error: the proxy is running in the active mode but server at \"%s\""
			" sends requests to it as to proxy in passive mode", sock->peer);

	treegix_log(LOG_LEVEL_WARNING, "%s", msg);
	trx_send_proxy_response(sock, FAIL, msg, CONFIG_TIMEOUT);
	trx_free(msg);
}

static int	process_trap(trx_socket_t *sock, char *s, trx_timespec_t *ts)
{
	int	ret = SUCCEED;

	trx_rtrim(s, " \r\n");

	treegix_log(LOG_LEVEL_DEBUG, "trapper got '%s'", s);

	if ('{' == *s)	/* JSON protocol */
	{
		struct trx_json_parse	jp;
		char			value[MAX_STRING_LEN];

		if (SUCCEED != trx_json_open(s, &jp))
		{
			trx_send_response(sock, FAIL, trx_json_strerror(), CONFIG_TIMEOUT);
			treegix_log(LOG_LEVEL_WARNING, "received invalid JSON object from %s: %s",
					sock->peer, trx_json_strerror());
			return FAIL;
		}

		if (SUCCEED == trx_json_value_by_name(&jp, TRX_PROTO_TAG_REQUEST, value, sizeof(value)))
		{
			if (0 == strcmp(value, TRX_PROTO_VALUE_PROXY_CONFIG))
			{
				if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
				{
					send_proxyconfig(sock, &jp);
				}
				else if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE))
				{
					treegix_log(LOG_LEVEL_WARNING, "received configuration data from server"
							" at \"%s\", datalen " TRX_FS_SIZE_T,
							sock->peer, (trx_fs_size_t)(jp.end - jp.start + 1));
					recv_proxyconfig(sock, &jp);
				}
				else if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_ACTIVE))
				{
					/* This is a misconfiguration: the proxy is configured in active mode */
					/* but server sends requests to it as to a proxy in passive mode. To  */
					/* prevent logging of this problem for every request we report it     */
					/* only when the server sends configuration to the proxy and ignore   */
					/* it for other requests.                                             */
					active_passive_misconfig(sock);
				}
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_AGENT_DATA))
			{
				recv_agenthistory(sock, &jp, ts);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_SENDER_DATA))
			{
				recv_senderhistory(sock, &jp, ts);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_PROXY_TASKS))
			{
				if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE))
					trx_send_task_data(sock, ts);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_PROXY_DATA))
			{
				if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
					trx_recv_proxy_data(sock, &jp, ts);
				else if (0 != (program_type & TRX_PROGRAM_TYPE_PROXY_PASSIVE))
					trx_send_proxy_data(sock, ts);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_PROXY_HEARTBEAT))
			{
				if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
					recv_proxy_heartbeat(sock, &jp);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_GET_ACTIVE_CHECKS))
			{
				ret = send_list_of_active_checks_json(sock, &jp);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_COMMAND))
			{
				if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
					ret = node_process_command(sock, s, &jp);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_GET_QUEUE))
			{
				if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
					ret = recv_getqueue(sock, &jp);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_GET_STATUS))
			{
				if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
					ret = recv_getstatus(sock, &jp);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_TREEGIX_STATS))
			{
				ret = send_internal_stats_json(sock, &jp);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_TREEGIX_ALERT_SEND))
			{
				if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
					recv_alert_send(sock, &jp);
			}
			else if (0 == strcmp(value, TRX_PROTO_VALUE_PREPROCESSING_TEST))
			{
				if (0 != (program_type & TRX_PROGRAM_TYPE_SERVER))
					ret = trx_trapper_preproc_test(sock, &jp);
			}
			else
				treegix_log(LOG_LEVEL_WARNING, "unknown request received from \"%s\": [%s]", sock->peer, value);
		}
	}
	else if (0 == strncmp(s, "TRX_GET_ACTIVE_CHECKS", 21))	/* request for list of active checks */
	{
		ret = send_list_of_active_checks(sock, s);
	}
	else
	{
		char			value_dec[MAX_BUFFER_LEN], lastlogsize[TRX_MAX_UINT64_LEN], timestamp[11],
					source[HISTORY_LOG_SOURCE_LEN_MAX], severity[11],
					host[HOST_HOST_LEN * TRX_MAX_BYTES_IN_UTF8_CHAR + 1],
					key[ITEM_KEY_LEN * TRX_MAX_BYTES_IN_UTF8_CHAR + 1];
		trx_agent_value_t	av;
		trx_host_key_t		hk = {host, key};
		DC_ITEM			item;
		int			errcode;

		memset(&av, 0, sizeof(trx_agent_value_t));

		if ('<' == *s)	/* XML protocol */
		{
			comms_parse_response(s, host, sizeof(host), key, sizeof(key), value_dec,
					sizeof(value_dec), lastlogsize, sizeof(lastlogsize), timestamp,
					sizeof(timestamp), source, sizeof(source), severity, sizeof(severity));

			av.value = value_dec;
			if (SUCCEED != is_uint64(lastlogsize, &av.lastlogsize))
				av.lastlogsize = 0;
			av.timestamp = atoi(timestamp);
			av.source = source;
			av.severity = atoi(severity);
		}
		else
		{
			char	*pl, *pr;

			pl = s;
			if (NULL == (pr = strchr(pl, ':')))
				return FAIL;

			*pr = '\0';
			trx_strlcpy(host, pl, sizeof(host));
			*pr = ':';

			pl = pr + 1;
			if (NULL == (pr = strchr(pl, ':')))
				return FAIL;

			*pr = '\0';
			trx_strlcpy(key, pl, sizeof(key));
			*pr = ':';

			av.value = pr + 1;
			av.severity = 0;
		}

		trx_timespec(&av.ts);

		if (0 == strcmp(av.value, TRX_NOTSUPPORTED))
			av.state = ITEM_STATE_NOTSUPPORTED;

		DCconfig_get_items_by_keys(&item, &hk, &errcode, 1);
		process_history_data(&item, &av, &errcode, 1);
		DCconfig_clean_items(&item, &errcode, 1);

		trx_alarm_on(CONFIG_TIMEOUT);
		if (SUCCEED != trx_tcp_send_raw(sock, "OK"))
			treegix_log(LOG_LEVEL_WARNING, "Error sending result back");
		trx_alarm_off();
	}

	return ret;
}

static void	process_trapper_child(trx_socket_t *sock, trx_timespec_t *ts)
{
	if (SUCCEED != trx_tcp_recv_to(sock, CONFIG_TRAPPER_TIMEOUT))
		return;

	process_trap(sock, sock->buffer, ts);
}

TRX_THREAD_ENTRY(trapper_thread, args)
{
	double		sec = 0.0;
	trx_socket_t	s;
	int		ret;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	memcpy(&s, (trx_socket_t *)((trx_thread_args_t *)args)->args, sizeof(trx_socket_t));

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	trx_tls_init_child();
	find_psk_in_cache = DCget_psk_by_identity;
#endif
	trx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);

	DBconnect(TRX_DB_CONNECT_NORMAL);

	while (TRX_IS_RUNNING())
	{
		trx_setproctitle("%s #%d [processed data in " TRX_FS_DBL " sec, waiting for connection]",
				get_process_type_string(process_type), process_num, sec);

		update_selfmon_counter(TRX_PROCESS_STATE_IDLE);

		/* Trapper has to accept all types of connections it can accept with the specified configuration. */
		/* Only after receiving data it is known who has sent them and one can decide to accept or discard */
		/* the data. */
		ret = trx_tcp_accept(&s, TRX_TCP_SEC_TLS_CERT | TRX_TCP_SEC_TLS_PSK | TRX_TCP_SEC_UNENCRYPTED);
		trx_update_env(trx_time());

		if (SUCCEED == ret)
		{
			trx_timespec_t	ts;

			/* get connection timestamp */
			trx_timespec(&ts);

			update_selfmon_counter(TRX_PROCESS_STATE_BUSY);

			trx_setproctitle("%s #%d [processing data]", get_process_type_string(process_type),
					process_num);

			sec = trx_time();
			process_trapper_child(&s, &ts);
			sec = trx_time() - sec;

			trx_tcp_unaccept(&s);
		}
		else if (EINTR != trx_socket_last_error())
		{
			treegix_log(LOG_LEVEL_WARNING, "failed to accept an incoming connection: %s",
					trx_socket_strerror());
		}
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
