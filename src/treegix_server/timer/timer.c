

#include "common.h"

#include "cfg.h"
#include "pid.h"
#include "db.h"
#include "log.h"
#include "dbcache.h"
#include "trxserver.h"
#include "daemon.h"
#include "trxself.h"
#include "db.h"

#include "timer.h"

#define TRX_TIMER_DELAY		SEC_PER_MIN

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;
extern int		CONFIG_TIMER_FORKS;

/* trigger -> functions cache */
typedef struct
{
	trx_uint64_t		triggerid;
	trx_vector_uint64_t	functionids;
}
trx_trigger_functions_t;

/* addition data for event maintenance calculations to pair with trx_event_suppress_query_t */
typedef struct
{
	trx_uint64_t			eventid;
	trx_vector_uint64_pair_t	maintenances;
}
trx_event_suppress_data_t;

/******************************************************************************
 *                                                                            *
 * Function: log_host_maintenance_update                                      *
 *                                                                            *
 * Purpose: log host maintenance changes                                      *
 *                                                                            *
 ******************************************************************************/
static void	log_host_maintenance_update(const trx_host_maintenance_diff_t* diff)
{
	char	*msg = NULL;
	size_t	msg_alloc = 0, msg_offset = 0;
	int	maintenance_off = 0;

	if (0 != (diff->flags & TRX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_STATUS))
	{
		if (HOST_MAINTENANCE_STATUS_ON == diff->maintenance_status)
		{
			trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "putting host (" TRX_FS_UI64 ") into",
					diff->hostid);
		}
		else
		{
			maintenance_off = 1;
			trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "taking host (" TRX_FS_UI64 ") out of",
				diff->hostid);
		}
	}
	else
		trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "changing host (" TRX_FS_UI64 ")", diff->hostid);

	trx_strcpy_alloc(&msg, &msg_alloc, &msg_offset, " maintenance");

	if (0 != (diff->flags & TRX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCEID) && 0 != diff->maintenanceid)
		trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "(" TRX_FS_UI64 ")", diff->maintenanceid);


	if (0 != (diff->flags & TRX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_TYPE) && 0 == maintenance_off)
	{
		const char	*description[] = {"with data collection", "without data collection"};

		trx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, " %s", description[diff->maintenance_type]);
	}

	treegix_log(LOG_LEVEL_DEBUG, "%s", msg);
	trx_free(msg);
}

/******************************************************************************
 *                                                                            *
 * Function: db_update_host_maintenances                                      *
 *                                                                            *
 * Purpose: update host maintenance properties in database                    *
 *                                                                            *
 ******************************************************************************/
static void	db_update_host_maintenances(const trx_vector_ptr_t *updates)
{
	int					i;
	const trx_host_maintenance_diff_t	*diff;
	char					*sql = NULL;
	size_t					sql_alloc = 0, sql_offset = 0;

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < updates->values_num; i++)
	{
		char	delim = ' ';

		diff = (const trx_host_maintenance_diff_t *)updates->values[i];

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update hosts set");

		if (0 != (diff->flags & TRX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCEID))
		{
			if (0 != diff->maintenanceid)
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenanceid=" TRX_FS_UI64, delim,
						diff->maintenanceid);
			}
			else
			{
				trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenanceid=null", delim);
			}

			delim = ',';
		}

		if (0 != (diff->flags & TRX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_TYPE))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenance_type=%u", delim,
					diff->maintenance_type);
			delim = ',';
		}

		if (0 != (diff->flags & TRX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_STATUS))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenance_status=%u", delim,
					diff->maintenance_status);
			delim = ',';
		}

		if (0 != (diff->flags & TRX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_FROM))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenance_from=%d", delim,
					diff->maintenance_from);
		}

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where hostid=" TRX_FS_UI64 ";\n", diff->hostid);

		if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
			break;

		if (SUCCEED == TRX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
			log_host_maintenance_update(diff);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)
		DBexecute("%s", sql);

	trx_free(sql);
}

/******************************************************************************
 *                                                                            *
 * Function: db_remove_expired_event_suppress_data                            *
 *                                                                            *
 * Purpose: remove expired event_suppress records                             *
 *                                                                            *
 ******************************************************************************/
static void	db_remove_expired_event_suppress_data(int now)
{
	DBbegin();
	DBexecute("delete from event_suppress where suppress_until<%d", now);
	DBcommit();
}

/******************************************************************************
 *                                                                            *
 * Function: event_suppress_data_free                                         *
 *                                                                            *
 * Purpose: free event suppress data structure                                *
 *                                                                            *
 ******************************************************************************/
static void	event_suppress_data_free(trx_event_suppress_data_t *data)
{
	trx_vector_uint64_pair_destroy(&data->maintenances);
	trx_free(data);
}

/******************************************************************************
 *                                                                            *
 * Function: db_get_query_events                                              *
 *                                                                            *
 * Purpose: get open, recently resolved and resolved problems with suppress   *
 *          data from database and prepare event query, event data structures *
 *                                                                            *
 ******************************************************************************/
static void	db_get_query_events(trx_vector_ptr_t *event_queries, trx_vector_ptr_t *event_data)
{
	DB_ROW				row;
	DB_RESULT			result;
	trx_event_suppress_query_t	*query;
	trx_event_suppress_data_t	*data = NULL;
	trx_uint64_t			eventid;
	trx_uint64_pair_t		pair;
	trx_vector_uint64_t		eventids;

	/* get open or recently closed problems */

	result = DBselect("select eventid,objectid,r_eventid"
			" from problem"
			" where source=%d"
				" and object=%d"
				" and " TRX_SQL_MOD(eventid, %d) "=%d"
			" order by eventid",
			EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, CONFIG_TIMER_FORKS, process_num - 1);

	while (NULL != (row = DBfetch(result)))
	{
		query = (trx_event_suppress_query_t *)trx_malloc(NULL, sizeof(trx_event_suppress_query_t));
		TRX_STR2UINT64(query->eventid, row[0]);
		TRX_STR2UINT64(query->triggerid, row[1]);
		TRX_DBROW2UINT64(query->r_eventid, row[2]);
		trx_vector_uint64_create(&query->functionids);
		trx_vector_ptr_create(&query->tags);
		trx_vector_uint64_pair_create(&query->maintenances);
		trx_vector_ptr_append(event_queries, query);
	}
	DBfree_result(result);

	/* get event suppress data */

	trx_vector_uint64_create(&eventids);

	result = DBselect("select eventid,maintenanceid,suppress_until"
			" from event_suppress"
			" where " TRX_SQL_MOD(eventid, %d) "=%d"
			" order by eventid",
			CONFIG_TIMER_FORKS, process_num - 1);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(eventid, row[0]);

		if (FAIL == trx_vector_ptr_bsearch(event_queries, &eventid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC))
			trx_vector_uint64_append(&eventids, eventid);

		if (NULL == data || data->eventid != eventid)
		{
			data = (trx_event_suppress_data_t *)trx_malloc(NULL, sizeof(trx_event_suppress_data_t));
			data->eventid = eventid;
			trx_vector_uint64_pair_create(&data->maintenances);
			trx_vector_ptr_append(event_data, data);
		}

		TRX_DBROW2UINT64(pair.first, row[1]);
		pair.second = atoi(row[2]);
		trx_vector_uint64_pair_append(&data->maintenances, pair);
	}
	DBfree_result(result);

	/* get missing event data */

	if (0 != eventids.values_num)
	{
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		trx_vector_uint64_uniq(&eventids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select e.eventid,e.objectid,er.r_eventid"
				" from events e"
				" left join event_recovery er"
					" on e.eventid=er.eventid"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "e.eventid", eventids.values, eventids.values_num);

		result = DBselect("%s", sql);
		trx_free(sql);

		while (NULL != (row = DBfetch(result)))
		{
			query = (trx_event_suppress_query_t *)trx_malloc(NULL, sizeof(trx_event_suppress_query_t));
			TRX_STR2UINT64(query->eventid, row[0]);
			TRX_STR2UINT64(query->triggerid, row[1]);
			TRX_DBROW2UINT64(query->r_eventid, row[2]);
			trx_vector_uint64_create(&query->functionids);
			trx_vector_ptr_create(&query->tags);
			trx_vector_uint64_pair_create(&query->maintenances);
			trx_vector_ptr_append(event_queries, query);
		}
		DBfree_result(result);

		trx_vector_ptr_sort(event_queries, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	trx_vector_uint64_destroy(&eventids);
}

/******************************************************************************
 *                                                                            *
 * Function: db_get_query_functions                                           *
 *                                                                            *
 * Purpose: get event query functionids from database                         *
 *                                                                            *
 ******************************************************************************/
static void	db_get_query_functions(trx_vector_ptr_t *event_queries)
{
	DB_ROW				row;
	DB_RESULT			result;
	int				i;
	trx_vector_uint64_t		triggerids;
	trx_hashset_t			triggers;
	trx_hashset_iter_t		iter;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	trx_trigger_functions_t		*trigger = NULL, trigger_local;
	trx_uint64_t			triggerid, functionid;
	trx_event_suppress_query_t	*query;

	/* cache functionids by triggerids */

	trx_hashset_create(&triggers, 100, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_vector_uint64_create(&triggerids);

	for (i = 0; i < event_queries->values_num; i++)
	{
		query = (trx_event_suppress_query_t *)event_queries->values[i];
		trx_vector_uint64_append(&triggerids, query->triggerid);
	}

	trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select functionid,triggerid from functions where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids.values,
			triggerids.values_num);
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by triggerid");

	result = DBselect("%s", sql);
	trx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(functionid, row[0]);
		TRX_STR2UINT64(triggerid, row[1]);

		if (NULL == trigger || trigger->triggerid != triggerid)
		{
			trigger_local.triggerid = triggerid;
			trigger = (trx_trigger_functions_t *)trx_hashset_insert(&triggers, &trigger_local,
					sizeof(trigger_local));
			trx_vector_uint64_create(&trigger->functionids);
		}
		trx_vector_uint64_append(&trigger->functionids, functionid);
	}
	DBfree_result(result);

	/*  copy functionids to event queries */

	for (i = 0; i < event_queries->values_num; i++)
	{
		query = (trx_event_suppress_query_t *)event_queries->values[i];

		if (NULL == (trigger = (trx_trigger_functions_t *)trx_hashset_search(&triggers, &query->triggerid)))
			continue;

		trx_vector_uint64_append_array(&query->functionids, trigger->functionids.values,
				trigger->functionids.values_num);
	}

	trx_hashset_iter_reset(&triggers, &iter);
	while (NULL != (trigger = (trx_trigger_functions_t *)trx_hashset_iter_next(&iter)))
		trx_vector_uint64_destroy(&trigger->functionids);
	trx_hashset_destroy(&triggers);

	trx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: db_get_query_tags                                                *
 *                                                                            *
 * Purpose: get event query tags from database                                *
 *                                                                            *
 ******************************************************************************/
static void	db_get_query_tags(trx_vector_ptr_t *event_queries)
{
	DB_ROW				row;
	DB_RESULT			result;
	int				i;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	trx_event_suppress_query_t	*query;
	trx_vector_uint64_t		eventids;
	trx_uint64_t			eventid;
	trx_tag_t			*tag;

	trx_vector_uint64_create(&eventids);

	for (i = 0; i < event_queries->values_num; i++)
	{
		query = (trx_event_suppress_query_t *)event_queries->values[i];
		trx_vector_uint64_append(&eventids, query->eventid);
	}

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select eventid,tag,value from problem_tag where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids.values, eventids.values_num);
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by eventid");

	result = DBselect("%s", sql);
	trx_free(sql);

	i = 0;
	query = (trx_event_suppress_query_t *)event_queries->values[0];

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(eventid, row[0]);

		while (query->eventid != eventid)
			query = (trx_event_suppress_query_t *)event_queries->values[++i];

		tag = (trx_tag_t *)trx_malloc(NULL, sizeof(trx_tag_t));
		tag->tag = trx_strdup(NULL, row[1]);
		tag->value = trx_strdup(NULL, row[2]);
		trx_vector_ptr_append(&query->tags, tag);
	}
	DBfree_result(result);

	trx_vector_uint64_destroy(&eventids);
}

/******************************************************************************
 *                                                                            *
 * Function: db_update_event_suppress_data                                    *
 *                                                                            *
 * Purpose: create/update event suppress data to reflect latest maintenance   *
 *          changes in cache                                                  *
 *                                                                            *
 * Parameters: suppressed_num - [OUT] the number of suppressed events         *
 *                                                                            *
 ******************************************************************************/
static void	db_update_event_suppress_data(int *suppressed_num)
{
	trx_vector_ptr_t	event_queries, event_data;

	*suppressed_num = 0;

	trx_vector_ptr_create(&event_queries);
	trx_vector_ptr_create(&event_data);

	db_get_query_events(&event_queries, &event_data);

	if (0 != event_queries.values_num)
	{
		trx_db_insert_t			db_insert;
		char				*sql = NULL;
		size_t				sql_alloc = 0, sql_offset = 0;
		int				i, j, k;
		trx_event_suppress_query_t	*query;
		trx_event_suppress_data_t	*data;
		trx_vector_uint64_pair_t	del_event_maintenances;
		trx_vector_uint64_t		maintenanceids;
		trx_uint64_pair_t		pair;

		trx_vector_uint64_create(&maintenanceids);
		trx_vector_uint64_pair_create(&del_event_maintenances);

		db_get_query_functions(&event_queries);
		db_get_query_tags(&event_queries);

		trx_dc_get_running_maintenanceids(&maintenanceids);

		DBbegin();

		if (0 != maintenanceids.values_num && SUCCEED == trx_db_lock_maintenanceids(&maintenanceids))
			trx_dc_get_event_maintenances(&event_queries, &maintenanceids);

		trx_db_insert_prepare(&db_insert, "event_suppress", "event_suppressid", "eventid", "maintenanceid",
				"suppress_until", NULL);
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < event_queries.values_num; i++)
		{
			query = (trx_event_suppress_query_t *)event_queries.values[i];
			trx_vector_uint64_pair_sort(&query->maintenances, TRX_DEFAULT_UINT64_COMPARE_FUNC);

			k = 0;

			if (FAIL != (j = trx_vector_ptr_bsearch(&event_data, &query->eventid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				data = (trx_event_suppress_data_t *)event_data.values[j];
				trx_vector_uint64_pair_sort(&data->maintenances, TRX_DEFAULT_UINT64_COMPARE_FUNC);

				j = 0;

				while (j < data->maintenances.values_num && k < query->maintenances.values_num)
				{
					if (data->maintenances.values[j].first < query->maintenances.values[k].first)
					{
						pair.first = query->eventid;
						pair.second = data->maintenances.values[j].first;
						trx_vector_uint64_pair_append(&del_event_maintenances, pair);

						j++;
						continue;
					}

					if (data->maintenances.values[j].first > query->maintenances.values[k].first)
					{
						if (0 == query->r_eventid)
						{
							trx_db_insert_add_values(&db_insert, __UINT64_C(0),
									query->eventid,
									query->maintenances.values[k].first,
									(int)query->maintenances.values[k].second);

							(*suppressed_num)++;
						}

						k++;
						continue;
					}

					if (data->maintenances.values[j].second != query->maintenances.values[k].second)
					{
						trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
								"update event_suppress"
								" set suppress_until=%d"
								" where eventid=" TRX_FS_UI64
									" and maintenanceid=" TRX_FS_UI64 ";\n",
									(int)query->maintenances.values[k].second,
									query->eventid,
									query->maintenances.values[k].first);

						if (FAIL == DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
							goto cleanup;
					}
					j++;
					k++;
				}

				for (;j < data->maintenances.values_num; j++)
				{
					pair.first = query->eventid;
					pair.second = data->maintenances.values[j].first;
					trx_vector_uint64_pair_append(&del_event_maintenances, pair);
				}
			}

			if (0 == query->r_eventid)
			{
				for (;k < query->maintenances.values_num; k++)
				{
					trx_db_insert_add_values(&db_insert, __UINT64_C(0), query->eventid,
							query->maintenances.values[k].first,
							(int)query->maintenances.values[k].second);

					(*suppressed_num)++;
				}
			}
		}

		for (i = 0; i < del_event_maintenances.values_num; i++)
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"delete from event_suppress"
					" where eventid=" TRX_FS_UI64
						" and maintenanceid=" TRX_FS_UI64 ";\n",
						del_event_maintenances.values[i].first,
						del_event_maintenances.values[i].second);

			if (FAIL == DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
				goto cleanup;
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)
		{
			if (TRX_DB_OK > DBexecute("%s", sql))
				goto cleanup;
		}

		trx_db_insert_autoincrement(&db_insert, "event_suppressid");
		trx_db_insert_execute(&db_insert);
cleanup:
		DBcommit();

		trx_db_insert_clean(&db_insert);
		trx_free(sql);

		trx_vector_uint64_pair_destroy(&del_event_maintenances);
		trx_vector_uint64_destroy(&maintenanceids);
	}

	trx_vector_ptr_clear_ext(&event_data, (trx_clean_func_t)event_suppress_data_free);
	trx_vector_ptr_destroy(&event_data);

	trx_vector_ptr_clear_ext(&event_queries, (trx_clean_func_t)trx_event_suppress_query_free);
	trx_vector_ptr_destroy(&event_queries);
}

/******************************************************************************
 *                                                                            *
 * Function: db_update_host_maintenances                                      *
 *                                                                            *
 * Purpose: update host maintenance parameters in cache and database          *
 *                                                                            *
 ******************************************************************************/
static int	update_host_maintenances(void)
{
	trx_vector_uint64_t	maintenanceids;
	trx_vector_ptr_t	updates;
	int			hosts_num = 0;
	int			tnx_error;

	trx_vector_uint64_create(&maintenanceids);
	trx_vector_ptr_create(&updates);
	trx_vector_ptr_reserve(&updates, 100);

	do
	{
		DBbegin();

		if (SUCCEED == trx_dc_get_running_maintenanceids(&maintenanceids))
			trx_db_lock_maintenanceids(&maintenanceids);

		/* host maintenance update must be called even with no maintenances running */
		/* to reset host maintenance status if necessary                            */
		trx_dc_get_host_maintenance_updates(&maintenanceids, &updates);

		if (0 != updates.values_num)
			db_update_host_maintenances(&updates);

		if (TRX_DB_OK == (tnx_error = DBcommit()) && 0 != (hosts_num = updates.values_num))
			trx_dc_flush_host_maintenance_updates(&updates);

		trx_vector_ptr_clear_ext(&updates, (trx_clean_func_t)trx_ptr_free);
		trx_vector_uint64_clear(&maintenanceids);
	}
	while (TRX_DB_DOWN == tnx_error);

	trx_vector_ptr_destroy(&updates);
	trx_vector_uint64_destroy(&maintenanceids);

	return hosts_num;
}

/******************************************************************************
 *                                                                            *
 * Function: timer_thread                                                     *
 *                                                                            *
 * Purpose: periodically processes maintenance                                *
 *                                                                            *
 ******************************************************************************/
TRX_THREAD_ENTRY(timer_thread, args)
{
	double		sec = 0.0;
	int		maintenance_time = 0, update_time = 0, idle = 1, events_num, hosts_num, update;
	char		*info = NULL;
	size_t		info_alloc = 0, info_offset = 0;

	process_type = ((trx_thread_args_t *)args)->process_type;
	server_num = ((trx_thread_args_t *)args)->server_num;
	process_num = ((trx_thread_args_t *)args)->process_num;

	treegix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	trx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);
	trx_strcpy_alloc(&info, &info_alloc, &info_offset, "started");

	DBconnect(TRX_DB_CONNECT_NORMAL);

	while (TRX_IS_RUNNING())
	{
		sec = trx_time();
		trx_update_env(sec);

		if (1 == process_num)
		{
			/* start update process only when all timers have finished their updates */
			if (sec - maintenance_time >= TRX_TIMER_DELAY && FAIL == trx_dc_maintenance_check_update_flags())
			{
				trx_setproctitle("%s #%d [%s, processing maintenances]",
						get_process_type_string(process_type), process_num, info);

				update = trx_dc_update_maintenances();

				/* force maintenance updates at server startup */
				if (0 == maintenance_time)
					update = SUCCEED;

				/* update hosts if there are modified (stopped, started, changed) maintenances */
				if (SUCCEED == update)
					hosts_num = update_host_maintenances();
				else
					hosts_num = 0;

				db_remove_expired_event_suppress_data((int)sec);

				if (SUCCEED == update)
				{
					trx_dc_maintenance_set_update_flags();
					db_update_event_suppress_data(&events_num);
					trx_dc_maintenance_reset_update_flag(process_num);
				}
				else
					events_num = 0;

				info_offset = 0;
				trx_snprintf_alloc(&info, &info_alloc, &info_offset,
						"updated %d hosts, suppressed %d events in " TRX_FS_DBL " sec",
						hosts_num, events_num, trx_time() - sec);

				update_time = (int)sec;
			}
		}
		else if (SUCCEED == trx_dc_maintenance_check_update_flag(process_num))
		{
			trx_setproctitle("%s #%d [%s, processing maintenances]", get_process_type_string(process_type),
					process_num, info);

			db_update_event_suppress_data(&events_num);

			info_offset = 0;
			trx_snprintf_alloc(&info, &info_alloc, &info_offset, "suppressed %d events in " TRX_FS_DBL
					" sec", events_num, trx_time() - sec);

			update_time = (int)sec;
			trx_dc_maintenance_reset_update_flag(process_num);
		}

		if (maintenance_time != update_time)
		{
			update_time -= update_time % 60;
			maintenance_time = update_time;

			if (0 > (idle = TRX_TIMER_DELAY - (trx_time() - maintenance_time)))
				idle = 0;

			trx_setproctitle("%s #%d [%s, idle %d sec]",
					get_process_type_string(process_type), process_num, info, idle);
		}

		if (0 != idle)
			trx_sleep_loop(1);

		idle = 1;
	}

	trx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		trx_sleep(SEC_PER_MIN);
}
