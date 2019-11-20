

#include "common.h"
#include "db.h"
#include "log.h"
#include "dbcache.h"

/******************************************************************************
 *                                                                            *
 * Function: trx_get_events_by_eventids                                       *
 *                                                                            *
 * Purpose: get events and flags that indicate what was filled in DB_EVENT    *
 *          structure                                                         *
 *                                                                            *
 * Parameters: eventids   - [IN] requested event ids                          *
 *             events     - [OUT] the array of events                         *
 *                                                                            *
 * Comments: use 'free_db_event' function to release allocated memory         *
 *                                                                            *
 ******************************************************************************/
void	trx_db_get_events_by_eventids(trx_vector_uint64_t *eventids, trx_vector_ptr_t *events)
{
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_vector_uint64_t	trigger_eventids, triggerids;
	int			i, index;

	trx_vector_uint64_create(&trigger_eventids);
	trx_vector_uint64_create(&triggerids);

	trx_vector_uint64_sort(eventids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(eventids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	/* read event data */

	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select eventid,source,object,objectid,clock,value,acknowledged,ns,name,severity"
			" from events"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids->values, eventids->values_num);
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by eventid");

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		DB_EVENT	*event = NULL;

		event = (DB_EVENT *)trx_malloc(event, sizeof(DB_EVENT));
		TRX_STR2UINT64(event->eventid, row[0]);
		event->source = atoi(row[1]);
		event->object = atoi(row[2]);
		TRX_STR2UINT64(event->objectid, row[3]);
		event->clock = atoi(row[4]);
		event->value = atoi(row[5]);
		event->acknowledged = atoi(row[6]);
		event->ns = atoi(row[7]);
		event->name = trx_strdup(NULL, row[8]);
		event->severity = atoi(row[9]);
		event->suppressed = TRX_PROBLEM_SUPPRESSED_FALSE;

		event->trigger.triggerid = 0;

		if (EVENT_SOURCE_TRIGGERS == event->source)
		{
			trx_vector_ptr_create(&event->tags);
			trx_vector_uint64_append(&trigger_eventids, event->eventid);
		}

		if (EVENT_OBJECT_TRIGGER == event->object)
			trx_vector_uint64_append(&triggerids, event->objectid);

		trx_vector_ptr_append(events, event);
	}
	DBfree_result(result);

	/* read event_suppress data */

	sql_offset = 0;
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select distinct eventid from event_suppress where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids->values, eventids->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		DB_EVENT	*event;
		trx_uint64_t	eventid;

		TRX_STR2UINT64(eventid, row[0]);
		if (FAIL == (index = trx_vector_ptr_bsearch(events, &eventid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		event = (DB_EVENT *)events->values[index];
		event->suppressed = TRX_PROBLEM_SUPPRESSED_TRUE;
	}
	DBfree_result(result);

	if (0 != trigger_eventids.values_num)	/* EVENT_SOURCE_TRIGGERS */
	{
		DB_EVENT	*event = NULL;

		sql_offset = 0;
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", trigger_eventids.values,
				trigger_eventids.values_num);

		result = DBselect("select eventid,tag,value from event_tag where%s order by eventid", sql);

		while (NULL != (row = DBfetch(result)))
		{
			trx_uint64_t	eventid;
			trx_tag_t	*tag;

			TRX_STR2UINT64(eventid, row[0]);

			if (NULL == event || eventid != event->eventid)
			{
				if (FAIL == (index = trx_vector_ptr_bsearch(events, &eventid,
						TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
				{
					THIS_SHOULD_NEVER_HAPPEN;
					continue;
				}

				event = (DB_EVENT *)events->values[index];
			}

			tag = (trx_tag_t *)trx_malloc(NULL, sizeof(trx_tag_t));
			tag->tag = trx_strdup(NULL, row[1]);
			tag->value = trx_strdup(NULL, row[2]);
			trx_vector_ptr_append(&event->tags, tag);
		}
		DBfree_result(result);
	}

	if (0 != triggerids.values_num)	/* EVENT_OBJECT_TRIGGER */
	{
		trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		trx_vector_uint64_uniq(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids.values,
				triggerids.values_num);

		result = DBselect(
				"select triggerid,description,expression,priority,comments,url,recovery_expression,"
					"recovery_mode,value,opdata"
				" from triggers"
				" where%s",
				sql);

		while (NULL != (row = DBfetch(result)))
		{
			trx_uint64_t	triggerid;

			TRX_STR2UINT64(triggerid, row[0]);

			for (i = 0; i < events->values_num; i++)
			{
				DB_EVENT	*event = (DB_EVENT *)events->values[i];

				if (EVENT_OBJECT_TRIGGER != event->object)
					continue;

				if (triggerid == event->objectid)
				{
					event->trigger.triggerid = triggerid;
					event->trigger.description = trx_strdup(NULL, row[1]);
					event->trigger.expression = trx_strdup(NULL, row[2]);
					TRX_STR2UCHAR(event->trigger.priority, row[3]);
					event->trigger.comments = trx_strdup(NULL, row[4]);
					event->trigger.url = trx_strdup(NULL, row[5]);
					event->trigger.recovery_expression = trx_strdup(NULL, row[6]);
					TRX_STR2UCHAR(event->trigger.recovery_mode, row[7]);
					TRX_STR2UCHAR(event->trigger.value, row[8]);
					event->trigger.opdata = trx_strdup(NULL, row[9]);
				}
			}
		}
		DBfree_result(result);
	}

	trx_free(sql);

	trx_vector_uint64_destroy(&trigger_eventids);
	trx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_trigger_clean                                             *
 *                                                                            *
 * Purpose: frees resources allocated to store trigger data                   *
 *                                                                            *
 * Parameters: trigger -                                                      *
 *                                                                            *
 ******************************************************************************/
void	trx_db_trigger_clean(DB_TRIGGER *trigger)
{
	trx_free(trigger->description);
	trx_free(trigger->expression);
	trx_free(trigger->recovery_expression);
	trx_free(trigger->comments);
	trx_free(trigger->url);
	trx_free(trigger->opdata);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_free_event                                                   *
 *                                                                            *
 * Purpose: deallocate memory allocated in function 'get_db_events_info'      *
 *                                                                            *
 * Parameters: event - [IN] event data                                        *
 *                                                                            *
 ******************************************************************************/
void	trx_db_free_event(DB_EVENT *event)
{
	if (EVENT_SOURCE_TRIGGERS == event->source)
	{
		trx_vector_ptr_clear_ext(&event->tags, (trx_clean_func_t)trx_free_tag);
		trx_vector_ptr_destroy(&event->tags);
	}

	if (0 != event->trigger.triggerid)
		trx_db_trigger_clean(&event->trigger);

	trx_free(event->name);
	trx_free(event);
}

/******************************************************************************
 *                                                                            *
 * Function: get_db_eventid_r_eventid_pairs                                   *
 *                                                                            *
 * Purpose: get recovery event IDs by event IDs then map them together also   *
 *          additional create a separate array of recovery event IDs          *
 *                                                                            *
 * Parameters: eventids    - [IN] requested event IDs                         *
 *             event_pairs - [OUT] the array of event ID and recovery event   *
 *                                 pairs                                      *
 *             r_eventids  - [OUT] array of recovery event IDs                *
 *                                                                            *
 ******************************************************************************/
void	trx_db_get_eventid_r_eventid_pairs(trx_vector_uint64_t *eventids, trx_vector_uint64_pair_t *event_pairs,
		trx_vector_uint64_t *r_eventids)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*filter = NULL;
	size_t		filter_alloc = 0, filter_offset = 0;

	DBadd_condition_alloc(&filter, &filter_alloc, &filter_offset, "eventid", eventids->values,
			eventids->values_num);

	result = DBselect("select eventid,r_eventid"
			" from event_recovery"
			" where%s order by eventid",
			filter);

	while (NULL != (row = DBfetch(result)))
	{
		trx_uint64_pair_t	r_event;

		TRX_STR2UINT64(r_event.first, row[0]);
		TRX_STR2UINT64(r_event.second, row[1]);

		trx_vector_uint64_pair_append(event_pairs, r_event);
		trx_vector_uint64_append(r_eventids, r_event.second);
	}
	DBfree_result(result);

	trx_free(filter);
}
