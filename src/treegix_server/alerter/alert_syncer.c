

#include "common.h"
#include "daemon.h"
#include "trxself.h"
#include "log.h"
#include "db.h"
#include "dbcache.h"
#include "trxipcservice.h"
#include "trxjson.h"
#include "alert_manager.h"
#include "alert_syncer.h"
#include "alerter_protocol.h"

#define TRX_POLL_INTERVAL	1

#define TRX_ALERT_BATCH_SIZE		1000
#define TRX_MEDIATYPE_CACHE_TTL		SEC_PER_DAY

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

extern int	CONFIG_CONFSYNCER_FREQUENCY;

typedef struct
{
	trx_hashset_t		mediatypes;
	trx_ipc_socket_t	am;
}
trx_am_db_t;

/******************************************************************************
 *                                                                            *
 * Function: am_db_create_alert                                               *
 *                                                                            *
 * Purpose: creates new alert object                                          *
 *                                                                            *
 * Parameters: ...           - [IN] alert data                                *
 *                                                                            *
 * Return value: The alert object.                                            *
 *                                                                            *
 ******************************************************************************/
static trx_am_db_alert_t	*am_db_create_alert(trx_uint64_t alertid, trx_uint64_t mediatypeid, int source,
		int object, trx_uint64_t objectid, trx_uint64_t eventid, const char *sendto, const char *subject,
		const char *message, const char *params, int status, int retries)
{
	trx_am_db_alert_t	*alert;

	alert = (trx_am_db_alert_t *)trx_malloc(NULL, sizeof(trx_am_db_alert_t));
	alert->alertid = alertid;
	alert->mediatypeid = mediatypeid;
	alert->source = source;
	alert->object = object;
	alert->objectid = objectid;
	alert->eventid = eventid;

	alert->sendto = trx_strdup(NULL, sendto);
	alert->subject = trx_strdup(NULL, subject);
	alert->message = trx_strdup(NULL, message);
	alert->params = trx_strdup(NULL, params);

	alert->status = status;
	alert->retries = retries;

	return alert;
}

/******************************************************************************
 *                                                                            *
 * Function: am_db_init                                                       *
 *                                                                            *
 ******************************************************************************/
static int 	am_db_init(trx_am_db_t *amdb, char **error)
{
	trx_hashset_create(&amdb->mediatypes, 5, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	if (SUCCEED != trx_ipc_socket_open(&amdb->am, TRX_IPC_SERVICE_ALERTER, SEC_PER_MIN, error))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: am_db_clear                                                      *
 *                                                                            *
 ******************************************************************************/
static void	am_db_clear(trx_am_db_t *amdb)
{
	trx_hashset_iter_t	iter;
	trx_am_db_mediatype_t	*mediatype;

	trx_hashset_iter_reset(&amdb->mediatypes, &iter);
	while (NULL != (mediatype = (trx_am_db_mediatype_t *)trx_hashset_iter_next(&iter)))
		trx_am_db_mediatype_clear(mediatype);

	trx_hashset_destroy(&amdb->mediatypes);
}

/******************************************************************************
 *                                                                            *
 * Function: am_db_get_alerts                                                 *
 *                                                                            *
 * Purpose: reads the new alerts from database                                *
 *                                                                            *
 * Parameters: alerts - [OUT] the new alerts                                  *
 *                                                                            *
 * Comments: One the first call this function will return new and not sent    *
 *           alerts. After that only new alerts are returned.                 *
 *                                                                            *
 * Return value: SUCCEED - the alerts were read successfully                  *
 *               FAIL    - database connection error                          *
 *                                                                            *
 ******************************************************************************/
static int	am_db_get_alerts(trx_vector_ptr_t *alerts)
{
	static int		status_limit = 2;
	trx_uint64_t		status_filter[] = {ALERT_STATUS_NEW, ALERT_STATUS_NOT_SENT};
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_uint64_t		alertid, mediatypeid, objectid, eventid;
	int			status, attempts, source, object, ret = SUCCEED;
	trx_am_db_alert_t	*alert;
	trx_vector_uint64_t	alertids;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&alertids);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select a.alertid,a.mediatypeid,a.sendto,a.subject,a.message,a.status,a.retries,"
				"e.source,e.object,e.objectid,a.parameters,a.eventid"
			" from alerts a"
			" left join events e"
				" on a.eventid=e.eventid"
			" where alerttype=%d"
			" and",
			ALERT_TYPE_MESSAGE);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "a.status", status_filter, status_limit);
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by a.alertid");

	DBbegin();
	result = DBselect("%s", sql);
	sql_offset = 0;
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(alertid, row[0]);
		TRX_STR2UINT64(mediatypeid, row[1]);
		TRX_STR2UINT64(eventid, row[11]);
		status = atoi(row[5]);
		attempts = atoi(row[6]);

		if (SUCCEED == DBis_null(row[7]))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"update alerts set status=%d,retries=0,error='Related event was removed.';\n",
					ALERT_STATUS_FAILED);
			if (FAIL == (ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset)))
				break;
			continue;
		}

		source = atoi(row[7]);
		object = atoi(row[8]);
		TRX_STR2UINT64(objectid, row[9]);

		alert = am_db_create_alert(alertid, mediatypeid, source, object, objectid, eventid, row[2], row[3],
				row[4], row[10], status, attempts);

		trx_vector_ptr_append(alerts, alert);

		if (ALERT_STATUS_NEW == alert->status)
			trx_vector_uint64_append(&alertids, alert->alertid);
	}
	DBfree_result(result);

	if (SUCCEED == ret)
	{
		if (0 != alertids.values_num)
		{
			sql_offset = 0;
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update alerts set status=%d where",
					ALERT_STATUS_NOT_SENT);
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "alertid", alertids.values,
					alertids.values_num);

		}
		if (16 < sql_offset)
			ret = (TRX_DB_OK <= DBexecute("%s", sql) ? SUCCEED : FAIL);
	}
	if (SUCCEED == ret)
	{
		if (TRX_DB_OK != DBcommit())
			ret = FAIL;
	}
	else
		DBrollback();

	trx_vector_uint64_destroy(&alertids);
	trx_free(sql);

	if (SUCCEED != ret)
		trx_vector_ptr_clear_ext(alerts, (trx_clean_func_t)trx_am_db_alert_free);
	else
		status_limit = 1;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s alerts:%d", __func__, trx_result_string(ret), alerts->values_num);

	return ret;
}

#define TRX_UPDATE_STR(dst, src, ret)			\
	if (NULL == dst || 0 != strcmp(dst, src))	\
	{						\
		dst = trx_strdup(dst, src);		\
		ret = SUCCEED;				\
	}

#define TRX_UPDATE_VALUE(dst, src, ret)			\
	if (dst != src)					\
	{						\
		dst = src;				\
		ret = SUCCEED;				\
	}

/******************************************************************************
 *                                                                            *
 * Function: am_db_update_mediatype                                           *
 *                                                                            *
 * Purpose: updates media type object, creating one if necessary              *
 *                                                                            *
 * Return value: Updated mediatype or NULL, if the cached media was up to     *
 *               date.                                                        *
 *                                                                            *
 ******************************************************************************/
static trx_am_db_mediatype_t	*am_db_update_mediatype(trx_am_db_t *amdb, time_t now, trx_uint64_t mediatypeid,
		int type, const char *smtp_server, const char *smtp_helo, const char *smtp_email,
		const char *exec_path, const char *gsm_modem, const char *username, const char *passwd,
		unsigned short smtp_port, unsigned char smtp_security, unsigned char smtp_verify_peer,
		unsigned char smtp_verify_host, unsigned char smtp_authentication, const char *exec_params,
		int maxsessions, int maxattempts, const char *attempt_interval, unsigned char content_type,
		const char *script, const char *timeout, int process_tags)
{
	trx_am_db_mediatype_t	*mediatype;
	int			ret = FAIL;

	if (NULL == (mediatype = (trx_am_db_mediatype_t *)trx_hashset_search(&amdb->mediatypes, &mediatypeid)))
	{
		trx_am_db_mediatype_t	mediatype_local = {
				.mediatypeid = mediatypeid
		};

		mediatype = (trx_am_db_mediatype_t *)trx_hashset_insert(&amdb->mediatypes, &mediatype_local,
				sizeof(mediatype_local));
		ret = SUCCEED;
	}

	mediatype->last_access = now;
	TRX_UPDATE_VALUE(mediatype->type, type, ret);
	TRX_UPDATE_STR(mediatype->smtp_server, smtp_server, ret);
	TRX_UPDATE_STR(mediatype->smtp_helo, smtp_helo, ret);
	TRX_UPDATE_STR(mediatype->smtp_email, smtp_email, ret);
	TRX_UPDATE_STR(mediatype->exec_path, exec_path, ret);
	TRX_UPDATE_STR(mediatype->exec_params, exec_params, ret);
	TRX_UPDATE_STR(mediatype->gsm_modem, gsm_modem, ret);
	TRX_UPDATE_STR(mediatype->username, username, ret);
	TRX_UPDATE_STR(mediatype->passwd, passwd, ret);
	TRX_UPDATE_STR(mediatype->script, script, ret);
	TRX_UPDATE_STR(mediatype->timeout, timeout, ret);
	TRX_UPDATE_STR(mediatype->attempt_interval, attempt_interval, ret);

	TRX_UPDATE_VALUE(mediatype->smtp_port, smtp_port, ret);
	TRX_UPDATE_VALUE(mediatype->smtp_security, smtp_security, ret);
	TRX_UPDATE_VALUE(mediatype->smtp_verify_peer, smtp_verify_peer, ret);
	TRX_UPDATE_VALUE(mediatype->smtp_verify_host, smtp_verify_host, ret);
	TRX_UPDATE_VALUE(mediatype->smtp_authentication, smtp_authentication, ret);

	TRX_UPDATE_VALUE(mediatype->maxsessions, maxsessions, ret);
	TRX_UPDATE_VALUE(mediatype->maxattempts, maxattempts, ret);
	TRX_UPDATE_VALUE(mediatype->content_type, content_type, ret);

	TRX_UPDATE_VALUE(mediatype->process_tags, process_tags, ret);

	return SUCCEED == ret ? mediatype : NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: am_db_update_mediatypes                                          *
 *                                                                            *
 * Purpose: updates alert manager media types                                 *
 *                                                                            *
 * Parameters: amdb            - [IN] the alert manager cache                 *
 *             mediatypeids    - [IN] the media type identifiers              *
 *             medatypeids_num - [IN] the number of media type identifiers    *
 *             mediatypes      - [OUT] the updated mediatypes                 *
 *                                                                            *
 ******************************************************************************/
static void	am_db_update_mediatypes(trx_am_db_t *amdb, const trx_uint64_t *mediatypeids, int mediatypeids_num,
		trx_vector_ptr_t *mediatypes)
{
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	int			type, maxsessions, maxattempts;
	trx_uint64_t		mediatypeid;
	unsigned short		smtp_port;
	unsigned char		smtp_security, smtp_verify_peer, smtp_verify_host, smtp_authentication, content_type;
	trx_am_db_mediatype_t	*mediatype;
	time_t			now;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select mediatypeid,type,smtp_server,smtp_helo,smtp_email,exec_path,gsm_modem,username,"
				"passwd,smtp_port,smtp_security,smtp_verify_peer,smtp_verify_host,smtp_authentication,"
				"exec_params,maxsessions,maxattempts,attempt_interval,content_type,script,timeout,"
				"process_tags"
			" from media_type"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "mediatypeid", mediatypeids, mediatypeids_num);

	result = DBselect("%s", sql);
	trx_free(sql);

	now = time(NULL);
	while (NULL != (row = DBfetch(result)))
	{
		if (FAIL == is_ushort(row[9], &smtp_port))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		TRX_STR2UINT64(mediatypeid, row[0]);
		type = atoi(row[1]);
		TRX_STR2UCHAR(smtp_security, row[10]);
		TRX_STR2UCHAR(smtp_verify_peer, row[11]);
		TRX_STR2UCHAR(smtp_verify_host, row[12]);
		TRX_STR2UCHAR(smtp_authentication, row[13]);
		maxsessions = atoi(row[15]);
		maxattempts = atoi(row[16]);
		TRX_STR2UCHAR(content_type, row[18]);

		mediatype = am_db_update_mediatype(amdb, now, mediatypeid, type,row[2], row[3], row[4], row[5],
				row[6], row[7], row[8], smtp_port, smtp_security, smtp_verify_peer, smtp_verify_host,
				smtp_authentication, row[14], maxsessions, maxattempts, row[17], content_type,
				row[19], row[20], atoi(row[21]));

		if (NULL != mediatype)
			trx_vector_ptr_append(mediatypes, mediatype);
	}
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() mediatypes:%d/%d", __func__, mediatypes->values_num, mediatypeids_num);
}

/******************************************************************************
 *                                                                            *
 * Function: am_db_queue_alerts                                               *
 *                                                                            *
 * Purpose: reads alerts/mediatypes from database and queues them in alert    *
 *          manager                                                           *
 *                                                                            *
 * Parameters: amdb            - [IN] the alert manager cache                 *
 *                                                                            *
 ******************************************************************************/
static int	am_db_queue_alerts(trx_am_db_t *amdb)
{
	trx_vector_ptr_t	alerts, mediatypes;
	int			i, alerts_num;
	trx_am_db_alert_t	*alert;
	trx_vector_uint64_t	mediatypeids;

	trx_vector_ptr_create(&alerts);
	trx_vector_uint64_create(&mediatypeids);
	trx_vector_ptr_create(&mediatypes);

	if (FAIL == am_db_get_alerts(&alerts) || 0 == alerts.values_num)
		goto out;

	for (i = 0; i < alerts.values_num; i++)
	{
		alert = (trx_am_db_alert_t *)alerts.values[i];
		trx_vector_uint64_append(&mediatypeids, alert->mediatypeid);
	}

	trx_vector_uint64_sort(&mediatypeids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(&mediatypeids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	am_db_update_mediatypes(amdb, mediatypeids.values, mediatypeids.values_num, &mediatypes);

	if (0 != mediatypes.values_num)
	{
		unsigned char	*data;
		trx_uint32_t	data_len;

		data_len = trx_alerter_serialize_mediatypes(&data, (trx_am_db_mediatype_t **)mediatypes.values,
				mediatypes.values_num);
		trx_ipc_socket_write(&amdb->am, TRX_IPC_ALERTER_MEDIATYPES, data, data_len);
		trx_free(data);
	}

	for (i = 0; i < alerts.values_num; i += TRX_ALERT_BATCH_SIZE)
	{
		unsigned char	*data;
		trx_uint32_t	data_len;
		int		to = i + TRX_ALERT_BATCH_SIZE;

		if (to >= alerts.values_num)
			to = alerts.values_num;

		data_len = trx_alerter_serialize_alerts(&data, (trx_am_db_alert_t **)&alerts.values[i], to - i);
		trx_ipc_socket_write(&amdb->am, TRX_IPC_ALERTER_ALERTS, data, data_len);
		trx_free(data);
	}

out:
	trx_vector_ptr_destroy(&mediatypes);
	trx_vector_uint64_destroy(&mediatypeids);
	alerts_num = alerts.values_num;
	trx_vector_ptr_clear_ext(&alerts, (trx_clean_func_t)trx_am_db_alert_free);
	trx_vector_ptr_destroy(&alerts);

	return alerts_num;
}

static int	am_db_compare_tags(const void *d1, const void *d2)
{
	trx_tag_t	*tag1 = *(trx_tag_t **)d1;
	trx_tag_t	*tag2 = *(trx_tag_t **)d2;
	int		ret;

	if (0 != (ret = strcmp(tag1->tag, tag2->tag)))
		return ret;

	return strcmp(tag1->value, tag2->value);
}

/******************************************************************************
 *                                                                            *
 * Function: am_db_add_event_tags                                             *
 *                                                                            *
 * Purpose: adds event tags to sql query                                      *
 *                                                                            *
 * Comments: The event tags are in json object fotmat.*
 *                                                                            *
 ******************************************************************************/
static void	am_db_update_event_tags(trx_db_insert_t *db_event, trx_db_insert_t *db_problem, trx_uint64_t eventid,
		const char *params)
{
	DB_RESULT		result;
	DB_ROW			row;
	struct trx_json_parse	jp, jp_tags;
	const char		*pnext = NULL;
	char			key[TAG_NAME_LEN * 4 + 1], value[TAG_VALUE_LEN * 4 + 1];
	trx_vector_ptr_t	tags;
	trx_tag_t		*tag, tag_local = {.tag = key, .value = value};
	int			i, index, problem = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() eventid:" TRX_FS_UI64 " tags:%s", __func__, eventid, params);

	result = DBselect("select e.source,p.eventid"
			" from events e left join problem p"
				" on p.eventid=e.eventid"
			" where e.eventid=" TRX_FS_UI64, eventid);

	if (NULL == (row = DBfetch(result)))
	{
		treegix_log(LOG_LEVEL_DEBUG, "cannot add event tags: event " TRX_FS_UI64 " was removed", eventid);
		goto out;
	}

	if (EVENT_SOURCE_TRIGGERS != atoi(row[0]))
	{
		treegix_log(LOG_LEVEL_DEBUG, "tags can be added only for problem trigger events");
		goto out;
	}

	if (SUCCEED != DBis_null(row[1]))
		problem = 1;

	if (FAIL == trx_json_open(params, &jp))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot process returned result: %s", trx_json_strerror());
		goto out;
	}

	if (FAIL == trx_json_brackets_by_name(&jp, TRX_PROTO_TAG_TAGS, &jp_tags))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot process returned result: missing tags field");
		goto out;
	}

	trx_vector_ptr_create(&tags);

	while (NULL != (pnext = trx_json_pair_next(&jp_tags, pnext, key, sizeof(key))))
	{
		if (NULL == trx_json_decodevalue(pnext, value, sizeof(value), NULL))
		{
			treegix_log(LOG_LEVEL_DEBUG, "invalid tag value starting with %s", pnext);
			continue;
		}

		trx_ltrim(key, TRX_WHITESPACE);
		trx_ltrim(value, TRX_WHITESPACE);

		if (TAG_NAME_LEN < trx_strlen_utf8(key))
			key[trx_strlen_utf8_nchars(key, TAG_NAME_LEN)] = '\0';
		if (TAG_VALUE_LEN < trx_strlen_utf8(value))
			value[trx_strlen_utf8_nchars(value, TAG_VALUE_LEN)] = '\0';

		trx_rtrim(key, TRX_WHITESPACE);
		trx_rtrim(value, TRX_WHITESPACE);

		if (FAIL == trx_vector_ptr_search(&tags, &tag_local, am_db_compare_tags))
		{
			tag = (trx_tag_t *)trx_malloc(NULL, sizeof(trx_tag_t));
			tag->tag = trx_strdup(NULL, key);
			tag->value = trx_strdup(NULL, value);
			trx_vector_ptr_append(&tags, tag);
		}
	}

	/* remove duplicate tags */
	if (0 != tags.values_num)
	{
		DBfree_result(result);
		result = DBselect("select tag,value from event_tag where eventid=" TRX_FS_UI64, eventid);
		while (NULL != (row = DBfetch(result)))
		{
			tag_local.tag = row[0];
			tag_local.value = row[1];

			if (FAIL != (index = trx_vector_ptr_search(&tags, &tag_local, am_db_compare_tags)))
			{
				trx_free_tag(tags.values[index]);
				trx_vector_ptr_remove_noorder(&tags, index);
			}
		}
	}

	for (i = 0; i < tags.values_num; i++)
	{
		tag = (trx_tag_t *)tags.values[i];
		trx_db_insert_add_values(db_event, __UINT64_C(0), eventid, tag->tag, tag->value);
		if (0 != problem)
			trx_db_insert_add_values(db_problem, __UINT64_C(0), eventid, tag->tag, tag->value);
	}

	trx_vector_ptr_clear_ext(&tags, (trx_clean_func_t)trx_free_tag);
	trx_vector_ptr_destroy(&tags);
out:
	DBfree_result(result);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: am_db_flush_results                                              *
 *                                                                            *
 * Purpose: retrieves alert updates from alert manager and flushes them into  *
 *          database                                                          *
 *                                                                            *
 * Parameters: amdb            - [IN] the alert manager cache                 *
 *                                                                            *
 ******************************************************************************/
static int	am_db_flush_results(trx_am_db_t *amdb)
{
	trx_ipc_message_t	message;
	trx_am_result_t		**results;
	int			results_num;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_ipc_socket_write(&amdb->am, TRX_IPC_ALERTER_RESULTS, NULL, 0);
	if (SUCCEED != trx_ipc_socket_read(&amdb->am, &message))
	{
		treegix_log(LOG_LEVEL_WARNING, "cannot retrieve alert results");
		return 0;
	}

	trx_alerter_deserialize_results(message.data, &results, &results_num);

	if (0 != results_num)
	{
		int 		i;
		char		*sql;
		size_t		sql_alloc = results_num * 128, sql_offset = 0;
		trx_db_insert_t	db_event, db_problem;

		sql = (char *)trx_malloc(NULL, sql_alloc);

		DBbegin();
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);
		trx_db_insert_prepare(&db_event, "event_tag", "eventtagid", "eventid", "tag", "value", NULL);
		trx_db_insert_prepare(&db_problem, "problem_tag", "problemtagid", "eventid", "tag", "value", NULL);

		for (i = 0; i < results_num; i++)
		{
			trx_am_db_mediatype_t	*mediatype;
			trx_am_result_t		*result = results[i];

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"update alerts set status=%d,retries=%d", result->status, result->retries);
			if (NULL != result->error)
			{
				char	*error_esc;
				error_esc = DBdyn_escape_field("alerts", "error", result->error);
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, ",error='%s'", error_esc);
				trx_free(error_esc);
			}
			else
				trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",error=''");

			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where alertid=" TRX_FS_UI64 ";\n",
					result->alertid);

			if (NULL != (mediatype = trx_hashset_search(&amdb->mediatypes, &result->mediatypeid)) &&
					0 != mediatype->process_tags && NULL != result->value)
			{
				am_db_update_event_tags(&db_event, &db_problem, result->eventid, result->value);
			}

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);

			trx_free(result->value);
			trx_free(result->error);
			trx_free(result);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		if (16 < sql_offset)
			DBexecute("%s", sql);

		trx_db_insert_autoincrement(&db_event, "eventtagid");
		trx_db_insert_execute(&db_event);
		trx_db_insert_clean(&db_event);

		trx_db_insert_autoincrement(&db_problem, "problemtagid");
		trx_db_insert_execute(&db_problem);
		trx_db_insert_clean(&db_problem);

		DBcommit();
		trx_free(sql);
	}

	trx_free(results);
	trx_ipc_message_clean(&message);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() flushed:%d", __func__, results_num);

	return results_num;
}
/******************************************************************************
 *                                                                            *
 * Function: am_db_remove_expired_mediatypes                                  *
 *                                                                            *
 * Purpose: removes cached media types used more than a day ago               *
 *                                                                            *
 * Parameters: amdb            - [IN] the alert manager cache                 *
 *                                                                            *
 ******************************************************************************/
static void	am_db_remove_expired_mediatypes(trx_am_db_t *amdb)
{
	trx_hashset_iter_t	iter;
	trx_am_db_mediatype_t	*mediatype;
	time_t			now;
	trx_vector_uint64_t	dropids;
	int			num;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	trx_vector_uint64_create(&dropids);
	now = time(NULL);
	trx_hashset_iter_reset(&amdb->mediatypes, &iter);
	while (NULL != (mediatype = (trx_am_db_mediatype_t *)trx_hashset_iter_next(&iter)))
	{
		if (mediatype->last_access + TRX_MEDIATYPE_CACHE_TTL <= now)
		{
			trx_vector_uint64_append(&dropids, mediatype->mediatypeid);
			trx_am_db_mediatype_clear(mediatype);
			trx_hashset_iter_remove(&iter);
		}
	}

	if (0 != dropids.values_num)
	{
		unsigned char	*data;
		trx_uint32_t	data_len;

		data_len = trx_alerter_serialize_ids(&data, dropids.values, dropids.values_num);
		trx_ipc_socket_write(&amdb->am, TRX_IPC_ALERTER_DROP_MEDIATYPES, data, data_len);
		trx_free(data);
	}

	num = dropids.values_num;
	trx_vector_uint64_destroy(&dropids);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() removed:%d", __func__, num);
}

/******************************************************************************
 *                                                                            *
 * Function: am_db_update_watchdog                                            *
 *                                                                            *
 * Purpose: updates watchdog recipients                                       *
 *                                                                            *
 * Parameters: amdb            - [IN] the alert manager cache                 *
 *                                                                            *
 ******************************************************************************/
static void	am_db_update_watchdog(trx_am_db_t *amdb)
{
	DB_RESULT		result;
	DB_ROW			row;
	int			medias_num = 0;
	trx_am_media_t		*media;
	trx_vector_uint64_t	mediatypeids;
	trx_vector_ptr_t	medias, mediatypes;
	unsigned char		*data;
	trx_uint32_t		data_len;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	result = DBselect(
			"select m.mediaid,m.mediatypeid,m.sendto"
			" from media m,users_groups u,config c,media_type mt"
			" where m.userid=u.userid"
				" and u.usrgrpid=c.alert_usrgrpid"
				" and m.mediatypeid=mt.mediatypeid"
				" and m.active=%d"
				" and mt.status=%d",
				MEDIA_STATUS_ACTIVE,
				MEDIA_TYPE_STATUS_ACTIVE);

	trx_vector_uint64_create(&mediatypeids);
	trx_vector_ptr_create(&medias);
	trx_vector_ptr_create(&mediatypes);

	/* read watchdog alert recipients */
	while (NULL != (row = DBfetch(result)))
	{
		media = (trx_am_media_t *)trx_malloc(NULL, sizeof(trx_am_media_t));
		TRX_STR2UINT64(media->mediaid, row[0]);
		TRX_STR2UINT64(media->mediatypeid, row[1]);
		media->sendto = trx_strdup(NULL, row[2]);
		trx_vector_ptr_append(&medias, media);
		trx_vector_uint64_append(&mediatypeids, media->mediatypeid);
	}
	DBfree_result(result);

	if (0 == medias.values_num)
		goto out;

	/* update media types used for watchdog alerts */

	trx_vector_uint64_sort(&mediatypeids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(&mediatypeids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	am_db_update_mediatypes(amdb, mediatypeids.values, mediatypeids.values_num, &mediatypes);

	if (0 != mediatypes.values_num)
	{
		data_len = trx_alerter_serialize_mediatypes(&data, (trx_am_db_mediatype_t **)mediatypes.values,
				mediatypes.values_num);
		trx_ipc_socket_write(&amdb->am, TRX_IPC_ALERTER_MEDIATYPES, data, data_len);
		trx_free(data);
	}

	data_len = trx_alerter_serialize_medias(&data, (trx_am_media_t **)medias.values, medias.values_num);
	trx_ipc_socket_write(&amdb->am, TRX_IPC_ALERTER_WATCHDOG, data, data_len);
	trx_free(data);

	medias_num = medias.values_num;

	trx_vector_ptr_clear_ext(&medias, (trx_clean_func_t)trx_am_media_free);
out:
	trx_vector_ptr_destroy(&mediatypes);
	trx_vector_uint64_destroy(&mediatypeids);
	trx_vector_ptr_destroy(&medias);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() recipients:%d", __func__, medias_num);
}

TRX_THREAD_ENTRY(alert_syncer_thread, args)
{
	double		sec1, sec2;
	int		alerts_num, sleeptime, nextcheck, freq_watchdog, time_watchdog = 0, time_cleanup = 0,
			results_num;
	trx_am_db_t	amdb;
	char		*error = NULL;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	if (SUCCEED != am_db_init(&amdb, &error))
	{
		treegix_log(LOG_LEVEL_CRIT, "cannot initialize alert loader: %s", error);
		trx_free(error);
		exit(EXIT_FAILURE);
	}

	trx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));
	DBconnect(TRX_DB_CONNECT_NORMAL);

	sleeptime = TRX_POLL_INTERVAL;

	if (TRX_WATCHDOG_ALERT_FREQUENCY < (freq_watchdog = CONFIG_CONFSYNCER_FREQUENCY))
		freq_watchdog = TRX_WATCHDOG_ALERT_FREQUENCY;

	trx_setproctitle("%s [started, idle %d sec]", get_process_type_string(process_type), sleeptime);

	while (TRX_IS_RUNNING())
	{
		trx_sleep_loop(sleeptime);

		sec1 = trx_time();
		trx_update_env(sec1);

		trx_setproctitle("%s [queuing alerts]", get_process_type_string(process_type));

		alerts_num = am_db_queue_alerts(&amdb);
		results_num = am_db_flush_results(&amdb);

		if (time_cleanup + SEC_PER_HOUR < sec1)
		{
			am_db_remove_expired_mediatypes(&amdb);
			time_cleanup = sec1;
		}

		if (time_watchdog + freq_watchdog < sec1)
		{
			am_db_update_watchdog(&amdb);
			time_watchdog = sec1;
		}

		sec2 = trx_time();

		nextcheck = sec1 + TRX_POLL_INTERVAL;

		if (0 > (sleeptime = nextcheck - (int)sec2))
			sleeptime = 0;

		trx_setproctitle("%s [queued %d alerts(s), flushed %d result(s) in " TRX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), alerts_num, results_num, sec2 - sec1, sleeptime);
	}

	am_db_clear(&amdb);

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
