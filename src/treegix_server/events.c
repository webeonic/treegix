

#include "common.h"
#include "db.h"
#include "log.h"

#include "actions.h"
#include "events.h"
#include "trxserver.h"
#include "export.h"

/* event recovery data */
typedef struct
{
	trx_uint64_t	eventid;
	trx_uint64_t	objectid;
	DB_EVENT	*r_event;
	trx_uint64_t	correlationid;
	trx_uint64_t	c_eventid;
	trx_uint64_t	userid;
	trx_timespec_t	ts;
}
trx_event_recovery_t;

/* problem event, used to cache open problems for recovery attempts */
typedef struct
{
	trx_uint64_t		eventid;
	trx_uint64_t		triggerid;

	trx_vector_ptr_t	tags;
}
trx_event_problem_t;

static trx_vector_ptr_t		events;
static trx_hashset_t		event_recovery;
static trx_hashset_t		correlation_cache;
static trx_correlation_rules_t	correlation_rules;

/******************************************************************************
 *                                                                            *
 * Function: validate_event_tag                                               *
 *                                                                            *
 * Purpose: Check that tag name is not empty and that tag is not duplicate.   *
 *                                                                            *
 ******************************************************************************/
static int	validate_event_tag(const DB_EVENT* event, const trx_tag_t *tag)
{
	int	i;

	if ('\0' == *tag->tag)
		return FAIL;

	/* check for duplicated tags */
	for (i = 0; i < event->tags.values_num; i++)
	{
		trx_tag_t	*event_tag = (trx_tag_t *)event->tags.values[i];

		if (0 == strcmp(event_tag->tag, tag->tag) && 0 == strcmp(event_tag->value, tag->value))
			return FAIL;
	}

	return SUCCEED;
}

static trx_tag_t	*duplicate_tag(const trx_tag_t *tag)
{
	trx_tag_t	*t;

	t = (trx_tag_t *)trx_malloc(NULL, sizeof(trx_tag_t));
	t->tag = trx_strdup(NULL, tag->tag);
	t->value = trx_strdup(NULL, tag->value);

	return t;
}

static void	validate_and_add_tag(DB_EVENT* event, trx_tag_t *tag)
{
	trx_ltrim(tag->tag, TRX_WHITESPACE);
	trx_ltrim(tag->value, TRX_WHITESPACE);

	if (TAG_NAME_LEN < trx_strlen_utf8(tag->tag))
		tag->tag[trx_strlen_utf8_nchars(tag->tag, TAG_NAME_LEN)] = '\0';
	if (TAG_VALUE_LEN < trx_strlen_utf8(tag->value))
		tag->value[trx_strlen_utf8_nchars(tag->value, TAG_VALUE_LEN)] = '\0';

	trx_rtrim(tag->tag, TRX_WHITESPACE);
	trx_rtrim(tag->value, TRX_WHITESPACE);

	if (SUCCEED == validate_event_tag(event, tag))
		trx_vector_ptr_append(&event->tags, tag);
	else
		trx_free_tag(tag);
}

static void	substitute_trigger_tag_macro(const DB_EVENT* event, char **str)
{
	substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL, str, MACRO_TYPE_TRIGGER_TAG, NULL, 0);
}

static void	process_trigger_tag(DB_EVENT* event, const trx_tag_t *tag)
{
	trx_tag_t	*t;

	t = duplicate_tag(tag);
	substitute_trigger_tag_macro(event, &t->tag);
	substitute_trigger_tag_macro(event, &t->value);
	validate_and_add_tag(event, t);
}

static void	substitute_item_tag_macro(const DB_EVENT* event, const DC_ITEM *dc_item, char **str)
{
	substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, dc_item, NULL,
			NULL, str, MACRO_TYPE_ITEM_TAG, NULL, 0);
}

static void	process_item_tag(DB_EVENT* event, const trx_item_tag_t *item_tag)
{
	trx_tag_t	*t;
	DC_ITEM		dc_item; /* used to pass data into substitute_simple_macros() function */

	t = duplicate_tag(&item_tag->tag);

	dc_item.host.hostid = item_tag->hostid;
	dc_item.itemid = item_tag->itemid;

	substitute_item_tag_macro(event, &dc_item, &t->tag);
	substitute_item_tag_macro(event, &dc_item, &t->value);
	validate_and_add_tag(event, t);
}

static void	get_item_tags_by_expression(const char *expression, trx_vector_ptr_t *item_tags)
{
	trx_vector_uint64_t	functionids;

	trx_vector_uint64_create(&functionids);
	get_functionids(&functionids, expression);
	trx_dc_get_item_tags_by_functionids(functionids.values, functionids.values_num, item_tags);
	trx_vector_uint64_destroy(&functionids);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_add_event                                                    *
 *                                                                            *
 * Purpose: add event to an array                                             *
 *                                                                            *
 * Parameters: source   - [IN] event source (EVENT_SOURCE_*)                  *
 *             object   - [IN] event object (EVENT_OBJECT_*)                  *
 *             objectid - [IN] trigger, item ... identificator from database, *
 *                             depends on source and object                   *
 *             timespec - [IN] event time                                     *
 *             value    - [IN] event value (TRIGGER_VALUE_*,                  *
 *                             TRIGGER_STATE_*, ITEM_STATE_* ... depends on   *
 *                             source and object)                             *
 *             trigger_description         - [IN] trigger description         *
 *             trigger_expression          - [IN] trigger short expression    *
 *             trigger_recovery_expression - [IN] trigger recovery expression *
 *             trigger_priority            - [IN] trigger priority            *
 *             trigger_type                - [IN] TRIGGER_TYPE_* defines      *
 *             trigger_tags                - [IN] trigger tags                *
 *             trigger_correlation_mode    - [IN] trigger correlation mode    *
 *             trigger_correlation_tag     - [IN] trigger correlation tag     *
 *             trigger_value               - [IN] trigger value               *
 *             trigger_opdata              - [IN] trigger operational data    *
 *             error                       - [IN] error for internal events   *
 *                                                                            *
 * Return value: The added event.                                             *
 *                                                                            *
 ******************************************************************************/
DB_EVENT	*trx_add_event(unsigned char source, unsigned char object, trx_uint64_t objectid,
		const trx_timespec_t *timespec, int value, const char *trigger_description,
		const char *trigger_expression, const char *trigger_recovery_expression, unsigned char trigger_priority,
		unsigned char trigger_type, const trx_vector_ptr_t *trigger_tags,
		unsigned char trigger_correlation_mode, const char *trigger_correlation_tag,
		unsigned char trigger_value, const char *trigger_opdata, const char *error)
{
	trx_vector_ptr_t	item_tags;
	int			i;
	DB_EVENT		*event;

	event = trx_malloc(NULL, sizeof(DB_EVENT));

	event->eventid = 0;
	event->source = source;
	event->object = object;
	event->objectid = objectid;
	event->name = NULL;
	event->clock = timespec->sec;
	event->ns = timespec->ns;
	event->value = value;
	event->acknowledged = EVENT_NOT_ACKNOWLEDGED;
	event->flags = TRX_FLAGS_DB_EVENT_CREATE;
	event->severity = TRIGGER_SEVERITY_NOT_CLASSIFIED;
	event->suppressed = TRX_PROBLEM_SUPPRESSED_FALSE;

	if (EVENT_SOURCE_TRIGGERS == source)
	{
		if (TRIGGER_VALUE_PROBLEM == value)
			event->severity = trigger_priority;

		event->trigger.triggerid = objectid;
		event->trigger.description = trx_strdup(NULL, trigger_description);
		event->trigger.expression = trx_strdup(NULL, trigger_expression);
		event->trigger.recovery_expression = trx_strdup(NULL, trigger_recovery_expression);
		event->trigger.priority = trigger_priority;
		event->trigger.type = trigger_type;
		event->trigger.correlation_mode = trigger_correlation_mode;
		event->trigger.correlation_tag = trx_strdup(NULL, trigger_correlation_tag);
		event->trigger.value = trigger_value;
		event->trigger.opdata = trx_strdup(NULL, trigger_opdata);
		event->name = trx_strdup(NULL, trigger_description);

		substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				&event->trigger.correlation_tag, MACRO_TYPE_TRIGGER_TAG, NULL, 0);

		substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				&event->name, MACRO_TYPE_TRIGGER_DESCRIPTION, NULL, 0);

		trx_vector_ptr_create(&event->tags);

		if (NULL != trigger_tags)
		{
			for (i = 0; i < trigger_tags->values_num; i++)
				process_trigger_tag(event, (const trx_tag_t *)trigger_tags->values[i]);
		}

		trx_vector_ptr_create(&item_tags);
		get_item_tags_by_expression(trigger_expression, &item_tags);

		for (i = 0; i < item_tags.values_num; i++)
		{
			process_item_tag(event, (const trx_item_tag_t *)item_tags.values[i]);
			trx_free_item_tag(item_tags.values[i]);
		}

		trx_vector_ptr_destroy(&item_tags);
	}
	else if (EVENT_SOURCE_INTERNAL == source && NULL != error)
		event->name = trx_strdup(NULL, error);

	trx_vector_ptr_append(&events, event);

	return event;
}

/******************************************************************************
 *                                                                            *
 * Function: close_trigger_event                                              *
 *                                                                            *
 * Purpose: add closing OK event for the specified problem event to an array  *
 *                                                                            *
 * Parameters: eventid  - [IN] the problem eventid                            *
 *             objectid - [IN] trigger, item ... identificator from database, *
 *                             depends on source and object                   *
 *             ts       - [IN] event time                                     *
 *             userid   - [IN] the user closing the problem                   *
 *             correlationid - [IN] the correlation rule                      *
 *             c_eventid - [IN] the correlation event                         *
 *             trigger_description         - [IN] trigger description         *
 *             trigger_expression          - [IN] trigger short expression    *
 *             trigger_recovery_expression - [IN] trigger recovery expression *
 *             trigger_priority            - [IN] trigger priority            *
 *             trigger_type                - [IN] TRIGGER_TYPE_* defines      *
 *             trigger_opdata              - [IN] trigger operational data    *
 *                                                                            *
 * Return value: Recovery event, created to close the specified event.        *
 *                                                                            *
 ******************************************************************************/
static DB_EVENT	*close_trigger_event(trx_uint64_t eventid, trx_uint64_t objectid, const trx_timespec_t *ts,
		trx_uint64_t userid, trx_uint64_t correlationid, trx_uint64_t c_eventid,
		const char *trigger_description, const char *trigger_expression,
		const char *trigger_recovery_expression, unsigned char trigger_priority, unsigned char trigger_type,
		const char *trigger_opdata)
{
	trx_event_recovery_t	recovery_local;
	DB_EVENT		*r_event;

	r_event = trx_add_event(EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, objectid, ts, TRIGGER_VALUE_OK,
			trigger_description, trigger_expression, trigger_recovery_expression, trigger_priority,
			trigger_type, NULL, TRX_TRIGGER_CORRELATION_NONE, "", TRIGGER_VALUE_PROBLEM, trigger_opdata,
			NULL);

	recovery_local.eventid = eventid;
	recovery_local.objectid = objectid;
	recovery_local.correlationid = correlationid;
	recovery_local.c_eventid = c_eventid;
	recovery_local.r_event = r_event;
	recovery_local.userid = userid;

	trx_hashset_insert(&event_recovery, &recovery_local, sizeof(recovery_local));

	return r_event;
}

/******************************************************************************
 *                                                                            *
 * Function: save_events                                                      *
 *                                                                            *
 * Purpose: flushes the events into a database                                *
 *                                                                            *
 ******************************************************************************/
static int	save_events(void)
{
	int			i;
	trx_db_insert_t		db_insert, db_insert_tags;
	int			j, num = 0, insert_tags = 0;
	trx_uint64_t		eventid;
	DB_EVENT		*event;

	for (i = 0; i < events.values_num; i++)
	{
		event = (DB_EVENT *)events.values[i];

		if (0 != (event->flags & TRX_FLAGS_DB_EVENT_CREATE) && 0 == event->eventid)
			num++;
	}

	trx_db_insert_prepare(&db_insert, "events", "eventid", "source", "object", "objectid", "clock", "ns", "value",
			"name", "severity", NULL);

	eventid = DBget_maxid_num("events", num);

	num = 0;

	for (i = 0; i < events.values_num; i++)
	{
		event = (DB_EVENT *)events.values[i];

		if (0 == (event->flags & TRX_FLAGS_DB_EVENT_CREATE))
			continue;

		if (0 == event->eventid)
			event->eventid = eventid++;

		trx_db_insert_add_values(&db_insert, event->eventid, event->source, event->object,
				event->objectid, event->clock, event->ns, event->value,
				TRX_NULL2EMPTY_STR(event->name), event->severity);

		num++;

		if (EVENT_SOURCE_TRIGGERS != event->source)
			continue;

		if (0 == event->tags.values_num)
			continue;

		if (0 == insert_tags)
		{
			trx_db_insert_prepare(&db_insert_tags, "event_tag", "eventtagid", "eventid", "tag", "value",
					NULL);
			insert_tags = 1;
		}

		for (j = 0; j < event->tags.values_num; j++)
		{
			trx_tag_t	*tag = (trx_tag_t *)event->tags.values[j];

			trx_db_insert_add_values(&db_insert_tags, __UINT64_C(0), event->eventid, tag->tag, tag->value);
		}
	}

	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);

	if (0 != insert_tags)
	{
		trx_db_insert_autoincrement(&db_insert_tags, "eventtagid");
		trx_db_insert_execute(&db_insert_tags);
		trx_db_insert_clean(&db_insert_tags);
	}

	return num;
}

/******************************************************************************
 *                                                                            *
 * Function: save_problems                                                    *
 *                                                                            *
 * Purpose: generates problems from problem events (trigger and internal      *
 *          event sources)                                                    *
 *                                                                            *
 ******************************************************************************/
static void	save_problems(void)
{
	int			i;
	trx_vector_ptr_t	problems;
	int			j, tags_num = 0;

	trx_vector_ptr_create(&problems);

	for (i = 0; i < events.values_num; i++)
	{
		DB_EVENT	*event = events.values[i];

		if (0 == (event->flags & TRX_FLAGS_DB_EVENT_CREATE))
			continue;

		if (EVENT_SOURCE_TRIGGERS == event->source)
		{
			if (EVENT_OBJECT_TRIGGER != event->object || TRIGGER_VALUE_PROBLEM != event->value)
				continue;

			tags_num += event->tags.values_num;
		}
		else if (EVENT_SOURCE_INTERNAL == event->source)
		{
			switch (event->object)
			{
				case EVENT_OBJECT_TRIGGER:
					if (TRIGGER_STATE_UNKNOWN != event->value)
						continue;
					break;
				case EVENT_OBJECT_ITEM:
					if (ITEM_STATE_NOTSUPPORTED != event->value)
						continue;
					break;
				case EVENT_OBJECT_LLDRULE:
					if (ITEM_STATE_NOTSUPPORTED != event->value)
						continue;
					break;
				default:
					continue;
			}
		}
		else
			continue;

		trx_vector_ptr_append(&problems, event);
	}

	if (0 != problems.values_num)
	{
		trx_db_insert_t	db_insert;

		trx_db_insert_prepare(&db_insert, "problem", "eventid", "source", "object", "objectid", "clock", "ns",
				"name", "severity", NULL);

		for (j = 0; j < problems.values_num; j++)
		{
			const DB_EVENT	*event = (const DB_EVENT *)problems.values[j];

			trx_db_insert_add_values(&db_insert, event->eventid, event->source, event->object,
					event->objectid, event->clock, event->ns, TRX_NULL2EMPTY_STR(event->name),
					event->severity);
		}

		trx_db_insert_execute(&db_insert);
		trx_db_insert_clean(&db_insert);

		if (0 != tags_num)
		{
			int	k;

			trx_db_insert_prepare(&db_insert, "problem_tag", "problemtagid", "eventid", "tag", "value",
					NULL);

			for (j = 0; j < problems.values_num; j++)
			{
				const DB_EVENT	*event = (const DB_EVENT *)problems.values[j];

				if (EVENT_SOURCE_TRIGGERS != event->source)
					continue;

				for (k = 0; k < event->tags.values_num; k++)
				{
					trx_tag_t	*tag = (trx_tag_t *)event->tags.values[k];

					trx_db_insert_add_values(&db_insert, __UINT64_C(0), event->eventid, tag->tag,
							tag->value);
				}
			}

			trx_db_insert_autoincrement(&db_insert, "problemtagid");
			trx_db_insert_execute(&db_insert);
			trx_db_insert_clean(&db_insert);
		}
	}

	trx_vector_ptr_destroy(&problems);
}

/******************************************************************************
 *                                                                            *
 * Function: save_event_recovery                                              *
 *                                                                            *
 * Purpose: saves event recovery data and removes recovered events from       *
 *          problem table                                                     *
 *                                                                            *
 ******************************************************************************/
static void	save_event_recovery(void)
{
	trx_db_insert_t		db_insert;
	trx_event_recovery_t	*recovery;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_hashset_iter_t	iter;

	if (0 == event_recovery.num_data)
		return;

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	trx_db_insert_prepare(&db_insert, "event_recovery", "eventid", "r_eventid", "correlationid", "c_eventid",
			"userid", NULL);

	trx_hashset_iter_reset(&event_recovery, &iter);
	while (NULL != (recovery = (trx_event_recovery_t *)trx_hashset_iter_next(&iter)))
	{
		trx_db_insert_add_values(&db_insert, recovery->eventid, recovery->r_event->eventid,
				recovery->correlationid, recovery->c_eventid, recovery->userid);

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"update problem set"
			" r_eventid=" TRX_FS_UI64
			",r_clock=%d"
			",r_ns=%d"
			",userid=" TRX_FS_UI64,
			recovery->r_event->eventid,
			recovery->r_event->clock,
			recovery->r_event->ns,
			recovery->userid);

		if (0 != recovery->correlationid)
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, ",correlationid=" TRX_FS_UI64,
					recovery->correlationid);
		}

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where eventid=" TRX_FS_UI64 ";\n",
				recovery->eventid);

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	trx_db_insert_execute(&db_insert);
	trx_db_insert_clean(&db_insert);

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
		DBexecute("%s", sql);

	trx_free(sql);
}

/******************************************************************************
 *                                                                            *
 * Function: get_event_index_by_source_object_id                              *
 *                                                                            *
 * Purpose: find event index by its source object                             *
 *                                                                            *
 * Parameters: source   - [IN] the event source                               *
 *             object   - [IN] the object type                                *
 *             objectid - [IN] the object id                                  *
 *                                                                            *
 * Return value: the event or NULL                                            *
 *                                                                            *
 ******************************************************************************/
static DB_EVENT	*get_event_by_source_object_id(int source, int object, trx_uint64_t objectid)
{
	int		i;
	DB_EVENT	*event;

	for (i = 0; i < events.values_num; i++)
	{
		event = (DB_EVENT *)events.values[i];

		if (event->source == source && event->object == object && event->objectid == objectid)
			return event;
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_match_event_hostgroup                                *
 *                                                                            *
 * Purpose: checks if the event matches the specified host group              *
 *          (including nested groups)                                         *
 *                                                                            *
 * Parameters: event   - [IN] the new event to check                          *
 *             groupid - [IN] the group id to match                           *
 *                                                                            *
 * Return value: SUCCEED - the group matches                                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	correlation_match_event_hostgroup(const DB_EVENT *event, trx_uint64_t groupid)
{
	DB_RESULT		result;
	int			ret = FAIL;
	trx_vector_uint64_t	groupids;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	trx_vector_uint64_create(&groupids);
	trx_dc_get_nested_hostgroupids(&groupid, 1, &groupids);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select hg.groupid"
				" from hstgrp g,hosts_groups hg,items i,functions f"
				" where f.triggerid=" TRX_FS_UI64
				" and i.itemid=f.itemid"
				" and hg.hostid=i.hostid"
				" and",
				event->objectid);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.groupid", groupids.values,
			groupids.values_num);

	result = DBselect("%s", sql);

	if (NULL != DBfetch(result))
		ret = SUCCEED;

	DBfree_result(result);
	trx_free(sql);
	trx_vector_uint64_destroy(&groupids);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_condition_match_new_event                            *
 *                                                                            *
 * Purpose: checks if the correlation condition matches the new event         *
 *                                                                            *
 * Parameters: condition - [IN] the correlation condition to check            *
 *             event     - [IN] the new event to match                        *
 *             old_value - [IN] SUCCEED - the old event conditions always     *
 *                                        match event                         *
 *                              FAIL    - the old event conditions never      *
 *                                        match event                         *
 *                                                                            *
 * Return value: SUCCEED - the correlation condition matches                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	correlation_condition_match_new_event(trx_corr_condition_t *condition, const DB_EVENT *event,
		int old_value)
{
	int		i, ret;
	trx_tag_t	*tag;

	/* return SUCCEED for conditions using old events */
	switch (condition->type)
	{
		case TRX_CORR_CONDITION_EVENT_TAG_PAIR:
			/* If old event condition never matches event we can return FAIL.  */
			/* Otherwise we must check if the new event has the requested tag. */
			if (SUCCEED != old_value)
				return FAIL;
			break;
		case TRX_CORR_CONDITION_OLD_EVENT_TAG:
		case TRX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			return old_value;
	}

	switch (condition->type)
	{
		case TRX_CORR_CONDITION_NEW_EVENT_TAG:
			for (i = 0; i < event->tags.values_num; i++)
			{
				tag = (trx_tag_t *)event->tags.values[i];
				if (0 == strcmp(tag->tag, condition->data.tag.tag))
					return SUCCEED;
			}
			return FAIL;

		case TRX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
			for (i = 0; i < event->tags.values_num; i++)
			{
				trx_corr_condition_tag_value_t	*cond = &condition->data.tag_value;

				tag = (trx_tag_t *)event->tags.values[i];
				if (0 == strcmp(tag->tag, cond->tag) &&
					SUCCEED == trx_strmatch_condition(tag->value, cond->value, cond->op))
				{
					return SUCCEED;
				}
			}
			return FAIL;

		case TRX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
			ret =  correlation_match_event_hostgroup(event, condition->data.group.groupid);
			if (CONDITION_OPERATOR_NOT_EQUAL == condition->data.group.op)
				ret = (SUCCEED == ret ? FAIL : SUCCEED);

			return ret;

		case TRX_CORR_CONDITION_EVENT_TAG_PAIR:
			for (i = 0; i < event->tags.values_num; i++)
			{
				tag = (trx_tag_t *)event->tags.values[i];
				if (0 == strcmp(tag->tag, condition->data.tag_pair.newtag))
					return SUCCEED;
			}
			return FAIL;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_match_new_event                                      *
 *                                                                            *
 * Purpose: checks if the correlation rule might match the new event          *
 *                                                                            *
 * Parameters: correlation - [IN] the correlation rule to check               *
 *             event       - [IN] the new event to match                      *
 *             old_value   - [IN] SUCCEED - the old event conditions always   *
 *                                        match event                         *
 *                              FAIL    - the old event conditions never      *
 *                                        match event                         *
 *                                                                            *
 *                                                                            *
 * Return value: SUCCEED - the correlation rule might match depending on old  *
 *                         events                                             *
 *               FAIL    - the correlation rule doesn't match the new event   *
 *                         (no matter what the old events are)                *
 *                                                                            *
 ******************************************************************************/
static int	correlation_match_new_event(trx_correlation_t *correlation, const DB_EVENT *event, int old_value)
{
	char			*expression, error[256];
	const char		*value;
	trx_token_t		token;
	int			pos = 0, ret = FAIL;
	trx_uint64_t		conditionid;
	trx_strloc_t		*loc;
	trx_corr_condition_t	*condition;
	double			result;

	if ('\0' == *correlation->formula)
		return SUCCEED;

	expression = trx_strdup(NULL, correlation->formula);

	for (; SUCCEED == trx_token_find(expression, pos, &token, TRX_TOKEN_SEARCH_BASIC); pos++)
	{
		if (TRX_TOKEN_OBJECTID != token.type)
			continue;

		loc = &token.data.objectid.name;

		if (SUCCEED != is_uint64_n(expression + loc->l, loc->r - loc->l + 1, &conditionid))
			continue;

		if (NULL == (condition = (trx_corr_condition_t *)trx_hashset_search(&correlation_rules.conditions, &conditionid)))
			goto out;

		if (SUCCEED == correlation_condition_match_new_event(condition, event, old_value))
			value = "1";
		else
			value = "0";

		trx_replace_string(&expression, token.loc.l, &token.loc.r, value);
		pos = token.loc.r;
	}

	if (SUCCEED == evaluate(&result, expression, error, sizeof(error), NULL))
		ret = trx_double_compare(result, 1);
out:
	trx_free(expression);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_has_old_event_filter                                 *
 *                                                                            *
 * Purpose: checks if correlation has conditions to match old events          *
 *                                                                            *
 * Parameters: correlation - [IN] the correlation to check                    *
 *                                                                            *
 * Return value: SUCCEED - correlation has conditions to match old events     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	correlation_has_old_event_filter(const trx_correlation_t *correlation)
{
	int				i;
	const trx_corr_condition_t	*condition;

	for (i = 0; i < correlation->conditions.values_num; i++)
	{
		condition = (trx_corr_condition_t *)correlation->conditions.values[i];

		switch (condition->type)
		{
			case TRX_CORR_CONDITION_OLD_EVENT_TAG:
			case TRX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			case TRX_CORR_CONDITION_EVENT_TAG_PAIR:
				return SUCCEED;
		}
	}
	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_has_old_event_operation                              *
 *                                                                            *
 * Purpose: checks if correlation has operations to change old events         *
 *                                                                            *
 * Parameters: correlation - [IN] the correlation to check                    *
 *                                                                            *
 * Return value: SUCCEED - correlation has operations to change old events    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	correlation_has_old_event_operation(const trx_correlation_t *correlation)
{
	int				i;
	const trx_corr_operation_t	*operation;

	for (i = 0; i < correlation->operations.values_num; i++)
	{
		operation = (trx_corr_operation_t *)correlation->operations.values[i];

		switch (operation->type)
		{
			case TRX_CORR_OPERATION_CLOSE_OLD:
				return SUCCEED;
		}
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_condition_add_tag_match                              *
 *                                                                            *
 * Purpose: adds sql statement to match tag according to the defined          *
 *          matching operation                                                *
 *                                                                            *
 * Parameters: sql         - [IN/OUT]                                         *
 *             sql_alloc   - [IN/OUT]                                         *
 *             sql_offset  - [IN/OUT]                                         *
 *             tag         - [IN] the tag to match                            *
 *             value       - [IN] the tag value to match                      *
 *             op          - [IN] the matching operation (CONDITION_OPERATOR_)*
 *                                                                            *
 ******************************************************************************/
static void	correlation_condition_add_tag_match(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *tag,
		const char *value, unsigned char op)
{
	char	*tag_esc, *value_esc;

	tag_esc = DBdyn_escape_string(tag);
	value_esc = DBdyn_escape_string(value);

	switch (op)
	{
		case CONDITION_OPERATOR_NOT_EQUAL:
		case CONDITION_OPERATOR_NOT_LIKE:
			trx_strcpy_alloc(sql, sql_alloc, sql_offset, "not ");
			break;
	}

	trx_strcpy_alloc(sql, sql_alloc, sql_offset,
			"exists (select null from problem_tag pt where p.eventid=pt.eventid and ");

	switch (op)
	{
		case CONDITION_OPERATOR_EQUAL:
		case CONDITION_OPERATOR_NOT_EQUAL:
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "pt.tag='%s' and pt.value" TRX_SQL_STRCMP,
					tag_esc, TRX_SQL_STRVAL_EQ(value_esc));
			break;
		case CONDITION_OPERATOR_LIKE:
		case CONDITION_OPERATOR_NOT_LIKE:
			trx_snprintf_alloc(sql, sql_alloc, sql_offset, "pt.tag='%s' and pt.value like '%%%s%%'",
					tag_esc, value_esc);
			break;
	}

	trx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

	trx_free(value_esc);
	trx_free(tag_esc);
}


/******************************************************************************
 *                                                                            *
 * Function: correlation_condition_get_event_filter                           *
 *                                                                            *
 * Purpose: creates sql filter to find events matching a correlation          *
 *          condition                                                         *
 *                                                                            *
 * Parameters: condition - [IN] the correlation condition to match            *
 *             event     - [IN] the new event to match                        *
 *                                                                            *
 * Return value: the created filter or NULL                                   *
 *                                                                            *
 ******************************************************************************/
static char	*correlation_condition_get_event_filter(trx_corr_condition_t *condition, const DB_EVENT *event)
{
	int			i;
	trx_tag_t		*tag;
	char			*tag_esc, *filter = NULL;
	size_t			filter_alloc = 0, filter_offset = 0;
	trx_vector_str_t	values;

	/* replace new event dependent condition with precalculated value */
	switch (condition->type)
	{
		case TRX_CORR_CONDITION_NEW_EVENT_TAG:
		case TRX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
		case TRX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
			if (SUCCEED == correlation_condition_match_new_event(condition, event, SUCCEED))
				filter = (char *)"1=1";
			else
				filter = (char *)"0=1";

			return trx_strdup(NULL, filter);
	}

	/* replace old event dependent condition with sql filter on problem_tag pt table */
	switch (condition->type)
	{
		case TRX_CORR_CONDITION_OLD_EVENT_TAG:
			tag_esc = DBdyn_escape_string(condition->data.tag.tag);
			trx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
					"exists (select null from problem_tag pt"
						" where p.eventid=pt.eventid"
							" and pt.tag='%s')",
					tag_esc);
			trx_free(tag_esc);
			return filter;

		case TRX_CORR_CONDITION_EVENT_TAG_PAIR:
			trx_vector_str_create(&values);

			for (i = 0; i < event->tags.values_num; i++)
			{
				tag = (trx_tag_t *)event->tags.values[i];
				if (0 == strcmp(tag->tag, condition->data.tag_pair.newtag))
					trx_vector_str_append(&values, trx_strdup(NULL, tag->value));
			}

			if (0 == values.values_num)
			{
				/* no new tag found, substitute condition with failure expression */
				filter = trx_strdup(NULL, "0");
			}
			else
			{
				tag_esc = DBdyn_escape_string(condition->data.tag_pair.oldtag);

				trx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						"exists (select null from problem_tag pt"
							" where p.eventid=pt.eventid"
								" and pt.tag='%s'"
								" and",
						tag_esc);

				DBadd_str_condition_alloc(&filter, &filter_alloc, &filter_offset, "pt.value",
						(const char **)values.values, values.values_num);

				trx_chrcpy_alloc(&filter, &filter_alloc, &filter_offset, ')');

				trx_free(tag_esc);
				trx_vector_str_clear_ext(&values, trx_str_free);
			}

			trx_vector_str_destroy(&values);
			return filter;

		case TRX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			correlation_condition_add_tag_match(&filter, &filter_alloc, &filter_offset,
					condition->data.tag_value.tag, condition->data.tag_value.value,
					condition->data.tag_value.op);
			return filter;
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_add_event_filter                                     *
 *                                                                            *
 * Purpose: add sql statement to filter out correlation conditions and        *
 *          matching events                                                   *
 *                                                                            *
 * Parameters: sql         - [IN/OUT]                                         *
 *             sql_alloc   - [IN/OUT]                                         *
 *             sql_offset  - [IN/OUT]                                         *
 *             correlation - [IN] the correlation rule to match               *
 *             event       - [IN] the new event to match                      *
 *                                                                            *
 * Return value: SUCCEED - the filter was added successfully                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	correlation_add_event_filter(char **sql, size_t *sql_alloc, size_t *sql_offset,
		trx_correlation_t *correlation, const DB_EVENT *event)
{
	char			*expression, *filter;
	trx_token_t		token;
	int			pos = 0, ret = FAIL;
	trx_uint64_t		conditionid;
	trx_strloc_t		*loc;
	trx_corr_condition_t	*condition;

	trx_snprintf_alloc(sql, sql_alloc, sql_offset, "c.correlationid=" TRX_FS_UI64, correlation->correlationid);

	expression = trx_strdup(NULL, correlation->formula);

	for (; SUCCEED == trx_token_find(expression, pos, &token, TRX_TOKEN_SEARCH_BASIC); pos++)
	{
		if (TRX_TOKEN_OBJECTID != token.type)
			continue;

		loc = &token.data.objectid.name;

		if (SUCCEED != is_uint64_n(expression + loc->l, loc->r - loc->l + 1, &conditionid))
			continue;

		if (NULL == (condition = (trx_corr_condition_t *)trx_hashset_search(&correlation_rules.conditions, &conditionid)))
			goto out;

		if (NULL == (filter = correlation_condition_get_event_filter(condition, event)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			goto out;
		}

		trx_replace_string(&expression, token.loc.l, &token.loc.r, filter);
		pos = token.loc.r;
		trx_free(filter);
	}

	if ('\0' != *expression)
		trx_snprintf_alloc(sql, sql_alloc, sql_offset, " and (%s)", expression);

	ret = SUCCEED;
out:
	trx_free(expression);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_execute_operations                                   *
 *                                                                            *
 * Purpose: execute correlation operations for the new event and matched      *
 *          old eventid                                                       *
 *                                                                            *
 * Parameters: correlation  - [IN] the correlation to execute                 *
 *             event        - [IN] the new event                              *
 *             old_eventid  - [IN] the old eventid                            *
 *             old_objectid - [IN] the old event source objectid (triggerid)  *
 *                                                                            *
 ******************************************************************************/
static void	correlation_execute_operations(trx_correlation_t *correlation, DB_EVENT *event,
		trx_uint64_t old_eventid, trx_uint64_t old_objectid)
{
	int			i;
	trx_corr_operation_t	*operation;
	trx_event_recovery_t	recovery_local;
	trx_timespec_t		ts;
	DB_EVENT		*r_event;

	for (i = 0; i < correlation->operations.values_num; i++)
	{
		operation = (trx_corr_operation_t *)correlation->operations.values[i];

		switch (operation->type)
		{
			case TRX_CORR_OPERATION_CLOSE_NEW:
				/* generate OK event to close the new event */

				/* check if this event was not been closed by another correlation rule */
				if (NULL != trx_hashset_search(&event_recovery, &event->eventid))
					break;

				ts.sec = event->clock;
				ts.ns = event->ns;

				r_event = close_trigger_event(event->eventid, event->objectid, &ts, 0,
						correlation->correlationid, event->eventid, event->trigger.description,
						event->trigger.expression, event->trigger.recovery_expression,
						event->trigger.priority, event->trigger.type, event->trigger.opdata);

				event->flags |= TRX_FLAGS_DB_EVENT_NO_ACTION;
				r_event->flags |= TRX_FLAGS_DB_EVENT_NO_ACTION;

				break;
			case TRX_CORR_OPERATION_CLOSE_OLD:
				/* queue closing of old events to lock them by triggerids */
				if (0 != old_eventid)
				{
					recovery_local.eventid = old_eventid;
					recovery_local.c_eventid = event->eventid;
					recovery_local.correlationid = correlation->correlationid;
					recovery_local.objectid = old_objectid;
					recovery_local.ts.sec = event->clock;
					recovery_local.ts.ns = event->ns;

					trx_hashset_insert(&correlation_cache, &recovery_local, sizeof(recovery_local));
				}
				break;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: correlate_event_by_global_rules                                  *
 *                                                                            *
 * Purpose: find problem events that must be recovered by global correlation  *
 *          rules and check if the new event must be closed                   *
 *                                                                            *
 * Comments: The correlation data (trx_event_recovery_t) of events that       *
 *           must be closed are added to event_correlation hashset            *
 *                                                                            *
 *           The global event correlation matching is done in two parts:      *
 *             1) exclude correlations that can't possibly match the event    *
 *                based on new event tag/value/group conditions               *
 *             2) assemble sql statement to select problems/correlations      *
 *                based on the rest correlation conditions                    *
 *                                                                            *
 ******************************************************************************/
static void	correlate_event_by_global_rules(DB_EVENT *event)
{
	int			i;
	trx_correlation_t	*correlation;
	trx_vector_ptr_t	corr_old, corr_new;
	char			*sql = NULL;
	const char		*delim = "";
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_uint64_t		eventid, correlationid, objectid;
	DB_RESULT		result;
	DB_ROW			row;

	trx_vector_ptr_create(&corr_old);
	trx_vector_ptr_create(&corr_new);

	for (i = 0; i < correlation_rules.correlations.values_num; i++)
	{
		correlation = (trx_correlation_t *)correlation_rules.correlations.values[i];

		if (SUCCEED == correlation_match_new_event(correlation, event, SUCCEED))
		{
			if (SUCCEED == correlation_has_old_event_filter(correlation) ||
					SUCCEED == correlation_has_old_event_operation(correlation))
			{
				trx_vector_ptr_append(&corr_old, correlation);
			}
			else
			{
				if (SUCCEED == correlation_match_new_event(correlation, event, FAIL))
					trx_vector_ptr_append(&corr_new, correlation);
			}
		}
	}

	if (0 != corr_new.values_num)
	{
		/* Process correlations that matches new event and does not use or affect old events. */
		/* Those correlations can be executed directly, without checking database.            */
		for (i = 0; i < corr_new.values_num; i++)
			correlation_execute_operations((trx_correlation_t *)corr_new.values[i], event, 0, 0);
	}

	if (0 != corr_old.values_num)
	{
		/* Process correlations that matches new event and either uses old events in conditions */
		/* or has operations involving old events.                                              */

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select p.eventid,p.objectid,c.correlationid"
								" from correlation c,problem p"
								" where p.r_eventid is null"
								" and (");

		for (i = 0; i < corr_old.values_num; i++)
		{
			correlation = (trx_correlation_t *)corr_old.values[i];

			trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, delim);
			correlation_add_event_filter(&sql, &sql_alloc, &sql_offset, correlation, event);
			delim = " or ";
		}

		trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(eventid, row[0]);

			/* check if this event is not already recovered by another correlation rule */
			if (NULL != trx_hashset_search(&correlation_cache, &eventid))
				continue;

			TRX_STR2UINT64(correlationid, row[2]);

			if (FAIL == (i = trx_vector_ptr_bsearch(&corr_old, &correlationid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			TRX_STR2UINT64(objectid, row[1]);
			correlation_execute_operations((trx_correlation_t *)corr_old.values[i], event, eventid, objectid);
		}

		DBfree_result(result);
		trx_free(sql);
	}

	trx_vector_ptr_destroy(&corr_new);
	trx_vector_ptr_destroy(&corr_old);
}

/******************************************************************************
 *                                                                            *
 * Function: correlate_events_by_global_rules                                 *
 *                                                                            *
 * Purpose: add events to the closing queue according to global correlation   *
 *          rules                                                             *
 *                                                                            *
 ******************************************************************************/
static void	correlate_events_by_global_rules(trx_vector_ptr_t *trigger_events, trx_vector_ptr_t *trigger_diff)
{
	int			i, index;
	trx_trigger_diff_t	*diff;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() events:%d", __func__, correlation_cache.num_data);

	trx_dc_correlation_rules_get(&correlation_rules);

	/* process global correlation and queue the events that must be closed */
	for (i = 0; i < trigger_events->values_num; i++)
	{
		DB_EVENT	*event = (DB_EVENT *)trigger_events->values[i];

		if (0 == (TRX_FLAGS_DB_EVENT_CREATE & event->flags))
			continue;

		correlate_event_by_global_rules(event);

		/* force value recalculation based on open problems for triggers with */
		/* events closed by 'close new' correlation operation                */
		if (0 != (event->flags & TRX_FLAGS_DB_EVENT_NO_ACTION))
		{
			if (FAIL != (index = trx_vector_ptr_bsearch(trigger_diff, &event->objectid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				diff = (trx_trigger_diff_t *)trigger_diff->values[index];
				diff->flags |= TRX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT;
			}
		}
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: flush_correlation_queue                                          *
 *                                                                            *
 * Purpose: try flushing correlation close events queue, generated by         *
 *          correlation rules                                                 *
 *                                                                            *
 ******************************************************************************/
static void	flush_correlation_queue(trx_vector_ptr_t *trigger_diff, trx_vector_uint64_t *triggerids_lock)
{
	trx_vector_uint64_t	triggerids, lockids, eventids;
	trx_hashset_iter_t	iter;
	trx_event_recovery_t	*recovery;
	int			i, closed_num = 0;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() events:%d", __func__, correlation_cache.num_data);

	if (0 == correlation_cache.num_data)
		goto out;

	trx_vector_uint64_create(&triggerids);
	trx_vector_uint64_create(&lockids);
	trx_vector_uint64_create(&eventids);

	/* lock source triggers of events to be closed by global correlation rules */

	trx_vector_uint64_sort(triggerids_lock, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	/* create a list of triggers that must be locked to close correlated events */
	trx_hashset_iter_reset(&correlation_cache, &iter);
	while (NULL != (recovery = (trx_event_recovery_t *)trx_hashset_iter_next(&iter)))
	{
		if (FAIL != trx_vector_uint64_bsearch(triggerids_lock, recovery->objectid,
				TRX_DEFAULT_UINT64_COMPARE_FUNC))
		{
			/* trigger already locked by this process, add to locked triggerids */
			trx_vector_uint64_append(&triggerids, recovery->objectid);
		}
		else
			trx_vector_uint64_append(&lockids, recovery->objectid);
	}

	if (0 != lockids.values_num)
	{
		int	num = triggerids_lock->values_num;

		trx_vector_uint64_sort(&lockids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		trx_vector_uint64_uniq(&lockids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		DCconfig_lock_triggers_by_triggerids(&lockids, triggerids_lock);

		/* append the locked trigger ids to already locked trigger ids */
		for (i = num; i < triggerids_lock->values_num; i++)
			trx_vector_uint64_append(&triggerids, triggerids_lock->values[i]);
	}

	/* process global correlation actions if we have successfully locked trigger(s) */
	if (0 != triggerids.values_num)
	{
		DC_TRIGGER		*triggers, *trigger;
		int			*errcodes, index;
		char			*sql = NULL;
		size_t			sql_alloc = 0, sql_offset = 0;
		trx_trigger_diff_t	*diff;

		/* get locked trigger data - needed for trigger diff and event generation */

		trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		triggers = (DC_TRIGGER *)trx_malloc(NULL, sizeof(DC_TRIGGER) * triggerids.values_num);
		errcodes = (int *)trx_malloc(NULL, sizeof(int) * triggerids.values_num);

		DCconfig_get_triggers_by_triggerids(triggers, triggerids.values, errcodes, triggerids.values_num);

		/* add missing diffs to the trigger changeset */

		for (i = 0; i < triggerids.values_num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			trigger = &triggers[i];

			if (FAIL == (index = trx_vector_ptr_bsearch(trigger_diff, &triggerids.values[i],
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				trx_append_trigger_diff(trigger_diff, trigger->triggerid, trigger->priority,
						TRX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT, trigger->value,
						TRIGGER_STATE_NORMAL, 0, NULL);

				/* TODO: should we store trigger diffs in hashset rather than vector? */
				trx_vector_ptr_sort(trigger_diff, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
			}
			else
			{
				diff = (trx_trigger_diff_t *)trigger_diff->values[index];
				diff->flags |= TRX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT;
			}
		}

		/* get correlated eventids that are still open (unresolved) */

		trx_hashset_iter_reset(&correlation_cache, &iter);
		while (NULL != (recovery = (trx_event_recovery_t *)trx_hashset_iter_next(&iter)))
		{
			/* close event only if its source trigger has been locked */
			if (FAIL == (index = trx_vector_uint64_bsearch(&triggerids, recovery->objectid,
					TRX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				continue;
			}

			if (SUCCEED != errcodes[index])
				continue;

			trx_vector_uint64_append(&eventids, recovery->eventid);
		}

		trx_vector_uint64_sort(&eventids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select eventid from problem"
								" where r_eventid is null and");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids.values, eventids.values_num);
		trx_vector_uint64_clear(&eventids);
		DBselect_uint64(sql, &eventids);
		trx_free(sql);

		/* generate OK events and add event_recovery data for closed events */
		trx_hashset_iter_reset(&correlation_cache, &iter);
		while (NULL != (recovery = (trx_event_recovery_t *)trx_hashset_iter_next(&iter)))
		{
			if (FAIL == (index = trx_vector_uint64_bsearch(&triggerids, recovery->objectid,
					TRX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				continue;
			}

			/* close the old problem only if it's still open and trigger is not removed */
			if (SUCCEED == errcodes[index] && FAIL != trx_vector_uint64_bsearch(&eventids, recovery->eventid,
					TRX_DEFAULT_UINT64_COMPARE_FUNC))
			{
				trigger = &triggers[index];

				close_trigger_event(recovery->eventid, recovery->objectid, &recovery->ts, 0,
						recovery->correlationid, recovery->c_eventid, trigger->description,
						trigger->expression_orig, trigger->recovery_expression_orig,
						trigger->priority, trigger->type, trigger->opdata);

				closed_num++;
			}

			trx_hashset_iter_remove(&iter);
		}

		DCconfig_clean_triggers(triggers, errcodes, triggerids.values_num);
		trx_free(errcodes);
		trx_free(triggers);
	}

	trx_vector_uint64_destroy(&eventids);
	trx_vector_uint64_destroy(&lockids);
	trx_vector_uint64_destroy(&triggerids);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s() closed:%d", __func__, closed_num);
}

/******************************************************************************
 *                                                                            *
 * Function: update_trigger_problem_count                                     *
 *                                                                            *
 * Purpose: update number of open problems                                    *
 *                                                                            *
 * Parameters: trigger_diff    - [IN/OUT] the changeset of triggers that      *
 *                               generated the events in local cache.         *
 *                                                                            *
 * Comments: When a specific event is closed (by correlation or manually) the *
 *           open problem count has to be queried from problem table to       *
 *           correctly calculate new trigger value.                           *
 *                                                                            *
 ******************************************************************************/
static void	update_trigger_problem_count(trx_vector_ptr_t *trigger_diff)
{
	DB_RESULT		result;
	DB_ROW			row;
	trx_vector_uint64_t	triggerids;
	trx_trigger_diff_t	*diff;
	int			i, index;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_uint64_t		triggerid;

	trx_vector_uint64_create(&triggerids);

	for (i = 0; i < trigger_diff->values_num; i++)
	{
		diff = (trx_trigger_diff_t *)trigger_diff->values[i];

		if (0 != (diff->flags & TRX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT))
		{
			trx_vector_uint64_append(&triggerids, diff->triggerid);

			/* reset problem count, it will be updated from database if there are open problems */
			diff->problem_count = 0;
			diff->flags |= TRX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT;
		}
	}

	if (0 == triggerids.values_num)
		goto out;

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select objectid,count(objectid) from problem"
			" where r_eventid is null"
				" and source=%d"
				" and object=%d"
				" and",
			EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", triggerids.values, triggerids.values_num);
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " group by objectid");

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(triggerid, row[0]);

		if (FAIL == (index = trx_vector_ptr_bsearch(trigger_diff, &triggerid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		diff = (trx_trigger_diff_t *)trigger_diff->values[index];
		diff->problem_count = atoi(row[1]);
		diff->flags |= TRX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT;
	}
	DBfree_result(result);

	trx_free(sql);
out:
	trx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: update_trigger_changes                                           *
 *                                                                            *
 * Purpose: update trigger value, problem count fields depending on problem   *
 *          and recovered events                                              *
 *                                                                            *
 ******************************************************************************/
static void	update_trigger_changes(trx_vector_ptr_t *trigger_diff)
{
	int			i;
	int			index, j, new_value;
	trx_trigger_diff_t	*diff;

	update_trigger_problem_count(trigger_diff);

	/* update trigger problem_count for new problem events */
	for (i = 0; i < events.values_num; i++)
	{
		DB_EVENT	*event = (DB_EVENT *)events.values[i];

		if (EVENT_SOURCE_TRIGGERS != event->source || EVENT_OBJECT_TRIGGER != event->object)
			continue;

		if (FAIL == (index = trx_vector_ptr_bsearch(trigger_diff, &event->objectid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		diff = (trx_trigger_diff_t *)trigger_diff->values[index];

		if (0 == (event->flags & TRX_FLAGS_DB_EVENT_CREATE))
		{
			diff->flags &= ~(trx_uint64_t)(TRX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT |
					TRX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE);
			continue;
		}

		/* always update trigger last change whenever a trigger event has been created */
		diff->lastchange = event->clock;
		diff->flags |= TRX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE;
	}

	/* recalculate trigger value from problem_count and mark for updating if necessary */
	for (j = 0; j < trigger_diff->values_num; j++)
	{
		diff = (trx_trigger_diff_t *)trigger_diff->values[j];

		if (0 == (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT))
			continue;

		new_value = (0 == diff->problem_count ? TRIGGER_VALUE_OK : TRIGGER_VALUE_PROBLEM);

		if (new_value != diff->value)
		{
			diff->value = new_value;
			diff->flags |= TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: trx_initialize_events                                            *
 *                                                                            *
 * Purpose: initializes the data structures required for event processing     *
 *                                                                            *
 ******************************************************************************/
void	trx_initialize_events(void)
{
	trx_vector_ptr_create(&events);
	trx_hashset_create(&event_recovery, 0, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_hashset_create(&correlation_cache, 0, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	trx_dc_correlation_rules_init(&correlation_rules);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_uninitialize_events                                          *
 *                                                                            *
 * Purpose: uninitializes the data structures required for event processing   *
 *                                                                            *
 ******************************************************************************/
void	trx_uninitialize_events(void)
{
	trx_vector_ptr_destroy(&events);
	trx_hashset_destroy(&event_recovery);
	trx_hashset_destroy(&correlation_cache);

	trx_dc_correlation_rules_free(&correlation_rules);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_reset_event_recovery                                         *
 *                                                                            *
 * Purpose: reset event_recovery data                                         *
 *                                                                            *
 ******************************************************************************/
void	trx_reset_event_recovery(void)
{
	trx_hashset_clear(&event_recovery);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_clean_event                                                  *
 *                                                                            *
 * Purpose: cleans single event                                               *
 *                                                                            *
 ******************************************************************************/
static void	trx_clean_event(DB_EVENT *event)
{
	trx_free(event->name);

	if (EVENT_SOURCE_TRIGGERS == event->source)
	{
		trx_free(event->trigger.description);
		trx_free(event->trigger.expression);
		trx_free(event->trigger.recovery_expression);
		trx_free(event->trigger.correlation_tag);
		trx_free(event->trigger.opdata);

		trx_vector_ptr_clear_ext(&event->tags, (trx_clean_func_t)trx_free_tag);
		trx_vector_ptr_destroy(&event->tags);
	}

	trx_free(event);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_clean_events                                                 *
 *                                                                            *
 * Purpose: cleans all events and events recoveries                           *
 *                                                                            *
 ******************************************************************************/
void	trx_clean_events(void)
{
	trx_vector_ptr_clear_ext(&events, (trx_clean_func_t)trx_clean_event);

	trx_reset_event_recovery();
}

/******************************************************************************
 *                                                                            *
 * Function: get_hosts_by_expression                                          *
 *                                                                            *
 * Purpose:  get hosts that are used in expression                            *
 *                                                                            *
 ******************************************************************************/
static void	get_hosts_by_expression(trx_hashset_t *hosts, const char *expression, const char *recovery_expression)
{
	trx_vector_uint64_t	functionids;

	trx_vector_uint64_create(&functionids);
	get_functionids(&functionids, expression);
	get_functionids(&functionids, recovery_expression);
	DCget_hosts_by_functionids(&functionids, hosts);
	trx_vector_uint64_destroy(&functionids);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_export_events                                                *
 *                                                                            *
 * Purpose: export events                                                     *
 *                                                                            *
 ******************************************************************************/
void	trx_export_events(void)
{
	int			i, j;
	struct trx_json		json;
	size_t			sql_alloc = 256, sql_offset;
	char			*sql = NULL;
	DB_RESULT		result;
	DB_ROW			row;
	trx_hashset_t		hosts;
	trx_vector_uint64_t	hostids;
	trx_hashset_iter_t	iter;
	trx_event_recovery_t	*recovery;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() events:" TRX_FS_SIZE_T, __func__, (trx_fs_size_t)events.values_num);

	if (0 == events.values_num)
		goto exit;

	trx_json_init(&json, TRX_JSON_STAT_BUF_LEN);
	sql = (char *)trx_malloc(sql, sql_alloc);
	trx_hashset_create(&hosts, events.values_num, TRX_DEFAULT_UINT64_HASH_FUNC, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_create(&hostids);

	for (i = 0; i < events.values_num; i++)
	{
		DC_HOST		*host;
		DB_EVENT	*event;

		event = (DB_EVENT *)events.values[i];

		if (EVENT_SOURCE_TRIGGERS != event->source || 0 == (event->flags & TRX_FLAGS_DB_EVENT_CREATE))
			continue;

		if (TRIGGER_VALUE_PROBLEM != event->value)
			continue;

		trx_json_clean(&json);

		trx_json_addint64(&json, TRX_PROTO_TAG_CLOCK, event->clock);
		trx_json_addint64(&json, TRX_PROTO_TAG_NS, event->ns);
		trx_json_addint64(&json, TRX_PROTO_TAG_VALUE, event->value);
		trx_json_adduint64(&json, TRX_PROTO_TAG_EVENTID, event->eventid);
		trx_json_addstring(&json, TRX_PROTO_TAG_NAME, event->name, TRX_JSON_TYPE_STRING);

		get_hosts_by_expression(&hosts, event->trigger.expression,
				event->trigger.recovery_expression);

		trx_json_addarray(&json, TRX_PROTO_TAG_HOSTS);

		trx_hashset_iter_reset(&hosts, &iter);

		while (NULL != (host = (DC_HOST *)trx_hashset_iter_next(&iter)))
		{
			trx_json_addobject(&json,NULL);
			trx_json_addstring(&json, TRX_PROTO_TAG_HOST, host->host, TRX_JSON_TYPE_STRING);
			trx_json_addstring(&json, TRX_PROTO_TAG_NAME, host->name, TRX_JSON_TYPE_STRING);
			trx_json_close(&json);
			trx_vector_uint64_append(&hostids, host->hostid);
		}

		trx_json_close(&json);

		sql_offset = 0;
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"select distinct g.name"
					" from hstgrp g, hosts_groups hg"
					" where g.groupid=hg.groupid"
						" and");

		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.hostid", hostids.values,
				hostids.values_num);

		result = DBselect("%s", sql);

		trx_json_addarray(&json, TRX_PROTO_TAG_GROUPS);

		while (NULL != (row = DBfetch(result)))
			trx_json_addstring(&json, NULL, row[0], TRX_JSON_TYPE_STRING);
		DBfree_result(result);

		trx_json_close(&json);

		trx_json_addarray(&json, TRX_PROTO_TAG_TAGS);
		for (j = 0; j < event->tags.values_num; j++)
		{
			trx_tag_t	*tag = (trx_tag_t *)event->tags.values[j];

			trx_json_addobject(&json, NULL);
			trx_json_addstring(&json, TRX_PROTO_TAG_TAG, tag->tag, TRX_JSON_TYPE_STRING);
			trx_json_addstring(&json, TRX_PROTO_TAG_VALUE, tag->value, TRX_JSON_TYPE_STRING);
			trx_json_close(&json);
		}

		trx_hashset_clear(&hosts);
		trx_vector_uint64_clear(&hostids);

		trx_problems_export_write(json.buffer, json.buffer_size);
	}

	trx_hashset_iter_reset(&event_recovery, &iter);
	while (NULL != (recovery = (trx_event_recovery_t *)trx_hashset_iter_next(&iter)))
	{
		if (EVENT_SOURCE_TRIGGERS != recovery->r_event->source)
			continue;

		trx_json_clean(&json);

		trx_json_addint64(&json, TRX_PROTO_TAG_CLOCK, recovery->r_event->clock);
		trx_json_addint64(&json, TRX_PROTO_TAG_NS, recovery->r_event->ns);
		trx_json_addint64(&json, TRX_PROTO_TAG_VALUE, recovery->r_event->value);
		trx_json_adduint64(&json, TRX_PROTO_TAG_EVENTID, recovery->r_event->eventid);
		trx_json_adduint64(&json, TRX_PROTO_TAG_PROBLEM_EVENTID, recovery->eventid);

		trx_problems_export_write(json.buffer, json.buffer_size);
	}

	trx_problems_export_flush();

	trx_hashset_destroy(&hosts);
	trx_vector_uint64_destroy(&hostids);
	trx_free(sql);
	trx_json_free(&json);
exit:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: add_event_suppress_data                                          *
 *                                                                            *
 * Purpose: adds event suppress data for problem events matching active       *
 *          maintenance periods                                               *
 *                                                                            *
 ******************************************************************************/
static void	add_event_suppress_data(trx_vector_ptr_t *event_refs, trx_vector_uint64_t *maintenanceids)
{
	trx_vector_ptr_t		event_queries;
	int				i, j;
	trx_event_suppress_query_t	*query;

	/* prepare query data  */

	trx_vector_ptr_create(&event_queries);

	for (i = 0; i < event_refs->values_num; i++)
	{
		DB_EVENT	*event = (DB_EVENT *)event_refs->values[i];

		query = (trx_event_suppress_query_t *)trx_malloc(NULL, sizeof(trx_event_suppress_query_t));
		query->eventid = event->eventid;

		trx_vector_uint64_create(&query->functionids);
		get_functionids(&query->functionids, event->trigger.expression);
		get_functionids(&query->functionids, event->trigger.recovery_expression);

		trx_vector_ptr_create(&query->tags);
		if (0 != event->tags.values_num)
			trx_vector_ptr_append_array(&query->tags, event->tags.values, event->tags.values_num);

		trx_vector_uint64_pair_create(&query->maintenances);

		trx_vector_ptr_append(&event_queries, query);
	}

	if (0 != event_queries.values_num)
	{
		trx_db_insert_t	db_insert;

		/* get maintenance data and save it in database */
		if (SUCCEED == trx_dc_get_event_maintenances(&event_queries, maintenanceids) &&
				SUCCEED == trx_db_lock_maintenanceids(maintenanceids))
		{
			trx_db_insert_prepare(&db_insert, "event_suppress", "event_suppressid", "eventid",
					"maintenanceid", "suppress_until", NULL);

			for (j = 0; j < event_queries.values_num; j++)
			{
				query = (trx_event_suppress_query_t *)event_queries.values[j];

				for (i = 0; i < query->maintenances.values_num; i++)
				{
					/* when locking maintenances not-locked (deleted) maintenance ids */
					/* are removed from the maintenanceids vector                   */
					if (FAIL == trx_vector_uint64_bsearch(maintenanceids,
							query->maintenances.values[i].first,
							TRX_DEFAULT_UINT64_COMPARE_FUNC))
					{
						continue;
					}

					trx_db_insert_add_values(&db_insert, __UINT64_C(0), query->eventid,
							query->maintenances.values[i].first,
							(int)query->maintenances.values[i].second);

					((DB_EVENT *)event_refs->values[j])->suppressed = TRX_PROBLEM_SUPPRESSED_TRUE;
				}
			}

			trx_db_insert_autoincrement(&db_insert, "event_suppressid");
			trx_db_insert_execute(&db_insert);
			trx_db_insert_clean(&db_insert);
		}

		for (j = 0; j < event_queries.values_num; j++)
		{
			query = (trx_event_suppress_query_t *)event_queries.values[j];
			/* reset tags vector to avoid double freeing copied tag name/value pointers */
			trx_vector_ptr_clear(&query->tags);
		}
		trx_vector_ptr_clear_ext(&event_queries, (trx_clean_func_t)trx_event_suppress_query_free);
	}

	trx_vector_ptr_destroy(&event_queries);
}

/******************************************************************************
 *                                                                            *
 * Function: save_event_suppress_data                                         *
 *                                                                            *
 * Purpose: retrieve running maintenances for each event and saves it in      *
 *          event_suppress table                                              *
 *                                                                            *
 ******************************************************************************/
static void	update_event_suppress_data(void)
{
	trx_vector_ptr_t	event_refs;
	trx_vector_uint64_t	maintenanceids;
	int			i;
	DB_EVENT		*event;

	trx_vector_uint64_create(&maintenanceids);
	trx_vector_ptr_create(&event_refs);
	trx_vector_ptr_reserve(&event_refs, events.values_num);

	/* prepare trigger problem event vector */
	for (i = 0; i < events.values_num; i++)
	{
		event = (DB_EVENT *)events.values[i];

		if (0 == (event->flags & TRX_FLAGS_DB_EVENT_CREATE))
			continue;

		if (EVENT_SOURCE_TRIGGERS != event->source)
			continue;

		if (TRIGGER_VALUE_PROBLEM == event->value)
			trx_vector_ptr_append(&event_refs, event);
	}

	if (0 == event_refs.values_num)
		goto out;

	if (SUCCEED != trx_dc_get_running_maintenanceids(&maintenanceids))
		goto out;

	if (0 != event_refs.values_num)
		add_event_suppress_data(&event_refs, &maintenanceids);
out:
	trx_vector_ptr_destroy(&event_refs);
	trx_vector_uint64_destroy(&maintenanceids);
}

/******************************************************************************
 *                                                                            *
 * Function: flush_events                                                     *
 *                                                                            *
 * Purpose: flushes local event cache to database                             *
 *                                                                            *
 ******************************************************************************/
static int	flush_events(void)
{
	int				ret;
	trx_event_recovery_t		*recovery;
	trx_vector_uint64_pair_t	closed_events;
	trx_hashset_iter_t		iter;

	ret = save_events();
	save_problems();
	save_event_recovery();
	update_event_suppress_data();

	trx_vector_uint64_pair_create(&closed_events);

	trx_hashset_iter_reset(&event_recovery, &iter);
	while (NULL != (recovery = (trx_event_recovery_t *)trx_hashset_iter_next(&iter)))
	{
		trx_uint64_pair_t	pair = {recovery->eventid, recovery->r_event->eventid};

		trx_vector_uint64_pair_append_ptr(&closed_events, &pair);
	}

	trx_vector_uint64_pair_sort(&closed_events, TRX_DEFAULT_UINT64_COMPARE_FUNC);

	process_actions(&events, &closed_events);
	trx_vector_uint64_pair_destroy(&closed_events);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: recover_event                                                    *
 *                                                                            *
 * Purpose: recover an event                                                  *
 *                                                                            *
 * Parameters: eventid   - [IN] the event to recover                          *
 *             source    - [IN] the recovery event source                     *
 *             object    - [IN] the recovery event object                     *
 *             objectid  - [IN] the recovery event object id                  *
 *                                                                            *
 ******************************************************************************/
static void	recover_event(trx_uint64_t eventid, int source, int object, trx_uint64_t objectid)
{
	DB_EVENT		*event;
	trx_event_recovery_t	recovery_local;

	if (NULL == (event = get_event_by_source_object_id(source, object, objectid)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return;
	}

	recovery_local.eventid = eventid;

	if (NULL != trx_hashset_search(&event_recovery, &recovery_local))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return;
	}

	recovery_local.objectid = objectid;
	recovery_local.r_event = event;
	recovery_local.correlationid = 0;
	recovery_local.c_eventid = 0;
	recovery_local.userid = 0;
	trx_hashset_insert(&event_recovery, &recovery_local, sizeof(recovery_local));
}

/******************************************************************************
 *                                                                            *
 * Function: process_internal_ok_events                                       *
 *                                                                            *
 * Purpose: process internal recovery events                                  *
 *                                                                            *
 * Parameters: ok_events - [IN] the recovery events to process                *
 *                                                                            *
 ******************************************************************************/
static void	process_internal_ok_events(trx_vector_ptr_t *ok_events)
{
	int			i, object;
	trx_uint64_t		objectid, eventid;
	char			*sql = NULL;
	const char		*separator = "";
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_vector_uint64_t	triggerids, itemids, lldruleids;
	DB_RESULT		result;
	DB_ROW			row;
	DB_EVENT		*event;

	trx_vector_uint64_create(&triggerids);
	trx_vector_uint64_create(&itemids);
	trx_vector_uint64_create(&lldruleids);

	for (i = 0; i < ok_events->values_num; i++)
	{
		event = (DB_EVENT *)ok_events->values[i];

		if (TRX_FLAGS_DB_EVENT_UNSET == event->flags)
			continue;

		switch (event->object)
		{
			case EVENT_OBJECT_TRIGGER:
				trx_vector_uint64_append(&triggerids, event->objectid);
				break;
			case EVENT_OBJECT_ITEM:
				trx_vector_uint64_append(&itemids, event->objectid);
				break;
			case EVENT_OBJECT_LLDRULE:
				trx_vector_uint64_append(&lldruleids, event->objectid);
				break;
		}
	}

	if (0 == triggerids.values_num && 0 == itemids.values_num && 0 == lldruleids.values_num)
		goto out;

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select eventid,object,objectid from problem"
			" where r_eventid is null"
				" and source=%d"
			" and (", EVENT_SOURCE_INTERNAL);

	if (0 != triggerids.values_num)
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s (object=%d and",
				separator, EVENT_OBJECT_TRIGGER);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", triggerids.values,
				triggerids.values_num);
		trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
		separator=" or";
	}

	if (0 != itemids.values_num)
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s (object=%d and",
				separator, EVENT_OBJECT_ITEM);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", itemids.values,
				itemids.values_num);
		trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
		separator=" or";
	}

	if (0 != lldruleids.values_num)
	{
		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s (object=%d and",
				separator, EVENT_OBJECT_LLDRULE);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", lldruleids.values,
				lldruleids.values_num);
		trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
	}

	trx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		TRX_STR2UINT64(eventid, row[0]);
		object = atoi(row[1]);
		TRX_STR2UINT64(objectid, row[2]);

		recover_event(eventid, EVENT_SOURCE_INTERNAL, object, objectid);
	}

	DBfree_result(result);
	trx_free(sql);

out:
	trx_vector_uint64_destroy(&lldruleids);
	trx_vector_uint64_destroy(&itemids);
	trx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: get_open_problems                                                *
 *                                                                            *
 * Purpose: gets open problems created by the specified triggers              *
 *                                                                            *
 * Parameters: triggerids - [IN] the trigger identifiers (sorted)             *
 *             problems   - [OUT] the problems                                *
 *                                                                            *
 ******************************************************************************/
static void	get_open_problems(const trx_vector_uint64_t *triggerids, trx_vector_ptr_t *problems)
{
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	trx_event_problem_t	*problem;
	trx_tag_t		*tag;
	trx_uint64_t		eventid;
	int			index;
	trx_vector_uint64_t	eventids;

	trx_vector_uint64_create(&eventids);

	trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select eventid,objectid from problem where source=%d and object=%d and",
			EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", triggerids->values, triggerids->values_num);
	trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and r_eventid is null");

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		problem = (trx_event_problem_t *)trx_malloc(NULL, sizeof(trx_event_problem_t));

		TRX_STR2UINT64(problem->eventid, row[0]);
		TRX_STR2UINT64(problem->triggerid, row[1]);
		trx_vector_ptr_create(&problem->tags);
		trx_vector_ptr_append(problems, problem);

		trx_vector_uint64_append(&eventids, problem->eventid);
	}
	DBfree_result(result);

	if (0 != problems->values_num)
	{
		trx_vector_ptr_sort(problems, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		trx_vector_uint64_sort(&eventids, TRX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select eventid,tag,value from problem_tag where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids.values, eventids.values_num);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			TRX_STR2UINT64(eventid, row[0]);
			if (FAIL == (index = trx_vector_ptr_bsearch(problems, &eventid,
					TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			problem = (trx_event_problem_t *)problems->values[index];

			tag = (trx_tag_t *)trx_malloc(NULL, sizeof(trx_tag_t));
			tag->tag = trx_strdup(NULL, row[1]);
			tag->value = trx_strdup(NULL, row[2]);
			trx_vector_ptr_append(&problem->tags, tag);
		}
		DBfree_result(result);
	}

	trx_free(sql);

	trx_vector_uint64_destroy(&eventids);
}

/******************************************************************************
 *                                                                            *
 * Function: event_problem_free                                               *
 *                                                                            *
 * Purpose: frees cached problem event                                        *
 *                                                                            *
 ******************************************************************************/
static void	event_problem_free(trx_event_problem_t *problem)
{
	trx_vector_ptr_clear_ext(&problem->tags, (trx_clean_func_t)trx_free_tag);
	trx_vector_ptr_destroy(&problem->tags);
	trx_free(problem);
}


/******************************************************************************
 *                                                                            *
 * Function: trigger_dep_free                                                 *
 *                                                                            *
 * Purpose: frees trigger dependency                                          *
 *                                                                            *
 ******************************************************************************/

static void	trigger_dep_free(trx_trigger_dep_t *dep)
{
	trx_vector_uint64_destroy(&dep->masterids);
	trx_free(dep);
}

/******************************************************************************
 *                                                                            *
 * Function: event_check_dependency                                           *
 *                                                                            *
 * Purpose: check event dependency based on cached and actual trigger values  *
 *                                                                            *
 * Parameters: event        - [IN] the event to check                         *
 *             deps         - [IN] the trigger dependency data (sorted by     *
 *                                 triggerid)                                 *
 *             trigger_diff - [IN] the trigger changeset - source of actual   *
 *                                 trigger values (sorted by triggerid)       *
 *                                                                            *
 ******************************************************************************/
static int	event_check_dependency(const DB_EVENT *event, const trx_vector_ptr_t *deps,
		const trx_vector_ptr_t *trigger_diff)
{
	int			i, index;
	trx_trigger_dep_t	*dep;
	trx_trigger_diff_t	*diff;

	if (FAIL == (index = trx_vector_ptr_bsearch(deps, &event->objectid, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		return SUCCEED;

	dep = (trx_trigger_dep_t *)deps->values[index];

	if (TRX_TRIGGER_DEPENDENCY_FAIL == dep->status)
		return FAIL;

	/* check the trigger dependency based on actual (currently being processed) trigger values */
	for (i = 0; i < dep->masterids.values_num; i++)
	{
		if (FAIL == (index = trx_vector_ptr_bsearch(trigger_diff, &dep->masterids.values[i],
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		diff = (trx_trigger_diff_t *)trigger_diff->values[index];

		if (0 == (TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE & diff->flags))
			continue;

		if (TRIGGER_VALUE_PROBLEM == diff->value)
			return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: match_tag                                                        *
 *                                                                            *
 * Purpose: checks if the two tag sets have matching tag                      *
 *                                                                            *
 * Parameters: name  - [IN] the name of tag to match                          *
 *             tags1 - [IN] the first tag vector                              *
 *             tags2 - [IN] the second tag vector                             *
 *                                                                            *
 * Return value: SUCCEED - both tag sets contains a tag with the specified    *
 *                         name and the same value                            *
 *               FAIL    - otherwise.                                         *
 *                                                                            *
 ******************************************************************************/
static int	match_tag(const char *name, const trx_vector_ptr_t *tags1, const trx_vector_ptr_t *tags2)
{
	int		i, j;
	trx_tag_t	*tag1, *tag2;

	for (i = 0; i < tags1->values_num; i++)
	{
		tag1 = (trx_tag_t *)tags1->values[i];

		if (0 != strcmp(tag1->tag, name))
			continue;

		for (j = 0; j < tags2->values_num; j++)
		{
			tag2 = (trx_tag_t *)tags2->values[j];

			if (0 == strcmp(tag2->tag, name) && 0 == strcmp(tag1->value, tag2->value))
				return SUCCEED;
		}
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: process_trigger_events                                           *
 *                                                                            *
 * Purpose: processes trigger events                                          *
 *                                                                            *
 * Parameters: trigger_events - [IN] the trigger events to process            *
 *             trigger_diff   - [IN] the trigger changeset                    *
 *                                                                            *
 ******************************************************************************/
static void	process_trigger_events(trx_vector_ptr_t *trigger_events, trx_vector_ptr_t *trigger_diff)
{
	int			i, j, index;
	trx_vector_uint64_t	triggerids;
	trx_vector_ptr_t	problems, deps;
	DB_EVENT		*event;
	trx_event_problem_t	*problem;
	trx_trigger_diff_t	*diff;
	unsigned char		value;

	trx_vector_uint64_create(&triggerids);
	trx_vector_uint64_reserve(&triggerids, trigger_events->values_num);

	trx_vector_ptr_create(&problems);
	trx_vector_ptr_reserve(&problems, trigger_events->values_num);

	trx_vector_ptr_create(&deps);
	trx_vector_ptr_reserve(&deps, trigger_events->values_num);

	/* cache relevant problems */

	for (i = 0; i < trigger_events->values_num; i++)
	{
		event = (DB_EVENT *)trigger_events->values[i];

		if (TRIGGER_VALUE_OK == event->value)
			trx_vector_uint64_append(&triggerids, event->objectid);
	}

	if (0 != triggerids.values_num)
	{
		trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
		get_open_problems(&triggerids, &problems);
	}

	/* get trigger dependency data */

	trx_vector_uint64_clear(&triggerids);
	for (i = 0; i < trigger_events->values_num; i++)
	{
		event = (DB_EVENT *)trigger_events->values[i];
		trx_vector_uint64_append(&triggerids, event->objectid);
	}

	trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_dc_get_trigger_dependencies(&triggerids, &deps);

	/* process trigger events */

	for (i = 0; i < trigger_events->values_num; i++)
	{
		event = (DB_EVENT *)trigger_events->values[i];

		if (FAIL == (index = trx_vector_ptr_search(trigger_diff, &event->objectid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		diff = (trx_trigger_diff_t *)trigger_diff->values[index];

		if (FAIL == (event_check_dependency(event, &deps, trigger_diff)))
		{
			/* reset event data/trigger changeset if dependency check failed */
			event->flags = TRX_FLAGS_DB_EVENT_UNSET;
			diff->flags = TRX_FLAGS_TRIGGER_DIFF_UNSET;
			continue;
		}

		if (TRIGGER_VALUE_PROBLEM == event->value)
		{
			/* Problem events always sets problem value to trigger.    */
			/* if the trigger is affected by global correlation rules, */
			/* its value is recalculated later.                        */
			diff->value = TRIGGER_VALUE_PROBLEM;
			diff->lastchange = event->clock;
			diff->flags |= (TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE | TRX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE);
			continue;
		}

		if (TRIGGER_VALUE_OK != event->value)
			continue;

		/* attempt to recover problem events/triggers */

		if (TRX_TRIGGER_CORRELATION_NONE == event->trigger.correlation_mode)
		{
			/* with trigger correlation disabled the recovery event recovers */
			/* all problem events generated by the same trigger and sets     */
			/* trigger value to OK                                           */
			for (j = 0; j < problems.values_num; j++)
			{
				problem = (trx_event_problem_t *)problems.values[j];

				if (problem->triggerid == event->objectid)
				{
					recover_event(problem->eventid, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER,
							event->objectid);
				}
			}

			diff->value = TRIGGER_VALUE_OK;
			diff->flags |= TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE;
		}
		else
		{
			/* With trigger correlation enabled the recovery event recovers    */
			/* all problem events generated by the same trigger and matching   */
			/* recovery event tags. The trigger value is set to OK only if all */
			/* problem events were recovered.                                  */

			value = TRIGGER_VALUE_OK;
			event->flags = TRX_FLAGS_DB_EVENT_UNSET;

			for (j = 0; j < problems.values_num; j++)
			{
				problem = (trx_event_problem_t *)problems.values[j];

				if (problem->triggerid == event->objectid)
				{
					if (SUCCEED == match_tag(event->trigger.correlation_tag,
							&problem->tags, &event->tags))
					{
						recover_event(problem->eventid, EVENT_SOURCE_TRIGGERS,
								EVENT_OBJECT_TRIGGER, event->objectid);
						event->flags = TRX_FLAGS_DB_EVENT_CREATE;
					}
					else
						value = TRIGGER_VALUE_PROBLEM;

				}
			}

			diff->value = value;
			diff->flags |= TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE;
		}
	}

	trx_vector_ptr_clear_ext(&problems, (trx_clean_func_t)event_problem_free);
	trx_vector_ptr_destroy(&problems);

	trx_vector_ptr_clear_ext(&deps, (trx_clean_func_t)trigger_dep_free);
	trx_vector_ptr_destroy(&deps);

	trx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: process_internal_events_dependency                               *
 *                                                                            *
 * Purpose: process internal trigger events                                   *
 *          to avoid trigger dependency                                       *
 *                                                                            *
 * Parameters: internal_events - [IN] the internal events to process          *
 *             trigger_events  - [IN] the trigger events used for dependency  *
 *             trigger_diff   -  [IN] the trigger changeset                   *
 *                                                                            *
 ******************************************************************************/
static void	process_internal_events_dependency(trx_vector_ptr_t *internal_events, trx_vector_ptr_t *trigger_events,
		trx_vector_ptr_t *trigger_diff)
{
	int			i, index;
	DB_EVENT		*event;
	trx_vector_uint64_t	triggerids;
	trx_vector_ptr_t	deps;
	trx_trigger_diff_t	*diff;

	trx_vector_uint64_create(&triggerids);
	trx_vector_uint64_reserve(&triggerids, internal_events->values_num + trigger_events->values_num);

	trx_vector_ptr_create(&deps);
	trx_vector_ptr_reserve(&deps, internal_events->values_num + trigger_events->values_num);

	for (i = 0; i < internal_events->values_num; i++)
	{
		event = (DB_EVENT *)internal_events->values[i];
		trx_vector_uint64_append(&triggerids, event->objectid);
	}

	for (i = 0; i < trigger_events->values_num; i++)
	{
		event = (DB_EVENT *)trigger_events->values[i];
		trx_vector_uint64_append(&triggerids, event->objectid);
	}

	trx_vector_uint64_sort(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_vector_uint64_uniq(&triggerids, TRX_DEFAULT_UINT64_COMPARE_FUNC);
	trx_dc_get_trigger_dependencies(&triggerids, &deps);

	for (i = 0; i < internal_events->values_num; i++)
	{
		event = (DB_EVENT *)internal_events->values[i];

		if (FAIL == (index = trx_vector_ptr_search(trigger_diff, &event->objectid,
				TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		diff = (trx_trigger_diff_t *)trigger_diff->values[index];

		if (FAIL == (event_check_dependency(event, &deps, trigger_diff)))
		{
			/* reset event data/trigger changeset if dependency check failed */
			event->flags = TRX_FLAGS_DB_EVENT_UNSET;
			diff->flags = TRX_FLAGS_TRIGGER_DIFF_UNSET;
			continue;
		}
	}

	trx_vector_ptr_clear_ext(&deps, (trx_clean_func_t)trigger_dep_free);
	trx_vector_ptr_destroy(&deps);

	trx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_process_events                                               *
 *                                                                            *
 * Purpose: processes cached events                                           *
 *                                                                            *
 * Parameters: trigger_diff    - [IN/OUT] the changeset of triggers that      *
 *                               generated the events in local cache. When    *
 *                               processing global correlation rules new      *
 *                               diffs can be added to trigger changeset.     *
 *                               Can be NULL when processing events from      *
 *                               non trigger sources                          *
 *             triggerids_lock - [IN/OUT] the ids of triggers locked by items.*
 *                               When processing global correlation rules new *
 *                               triggers can be locked and added to this     *
 *                               vector.                                      *
 *                               Can be NULL when processing events from      *
 *                               non trigger sources                          *
 *                                                                            *
 * Return value: The number of processed events                               *
 *                                                                            *
 ******************************************************************************/
int	trx_process_events(trx_vector_ptr_t *trigger_diff, trx_vector_uint64_t *triggerids_lock)
{
	int			i, processed_num = 0;
	trx_uint64_t		eventid;
	trx_vector_ptr_t	internal_ok_events, trigger_events, internal_events;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() events_num:" TRX_FS_SIZE_T, __func__, (trx_fs_size_t)events.values_num);

	if (NULL != trigger_diff && 0 != correlation_cache.num_data)
		flush_correlation_queue(trigger_diff, triggerids_lock);

	if (0 != events.values_num)
	{
		trx_vector_ptr_create(&internal_ok_events);
		trx_vector_ptr_reserve(&internal_ok_events, events.values_num);

		trx_vector_ptr_create(&trigger_events);
		trx_vector_ptr_reserve(&trigger_events, events.values_num);

		trx_vector_ptr_create(&internal_events);
		trx_vector_ptr_reserve(&internal_events, events.values_num);

		/* assign event identifiers - they are required to set correlation event ids */
		eventid = DBget_maxid_num("events", events.values_num);
		for (i = 0; i < events.values_num; i++)
		{
			DB_EVENT	*event = (DB_EVENT *)events.values[i];

			event->eventid = eventid++;

			if (EVENT_SOURCE_TRIGGERS == event->source)
			{
				trx_vector_ptr_append(&trigger_events, event);
				continue;
			}

			if (EVENT_SOURCE_INTERNAL == event->source)
			{
				switch (event->object)
				{
					case EVENT_OBJECT_TRIGGER:
						if (TRIGGER_STATE_NORMAL == event->value)
							trx_vector_ptr_append(&internal_ok_events, event);
						trx_vector_ptr_append(&internal_events, event);
						break;
					case EVENT_OBJECT_ITEM:
						if (ITEM_STATE_NORMAL == event->value)
							trx_vector_ptr_append(&internal_ok_events, event);
						break;
					case EVENT_OBJECT_LLDRULE:
						if (ITEM_STATE_NORMAL == event->value)
							trx_vector_ptr_append(&internal_ok_events, event);
						break;
				}
			}
		}

		if (0 != internal_events.values_num)
			process_internal_events_dependency(&internal_events, &trigger_events, trigger_diff);

		if (0 != internal_ok_events.values_num)
			process_internal_ok_events(&internal_ok_events);

		if (0 != trigger_events.values_num)
		{
			process_trigger_events(&trigger_events, trigger_diff);
			correlate_events_by_global_rules(&trigger_events, trigger_diff);
			flush_correlation_queue(trigger_diff, triggerids_lock);
		}

		processed_num = flush_events();

		if (0 != trigger_events.values_num)
			update_trigger_changes(trigger_diff);

		trx_vector_ptr_destroy(&trigger_events);
		trx_vector_ptr_destroy(&internal_ok_events);
		trx_vector_ptr_destroy(&internal_events);
	}

	treegix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __func__, (int)processed_num);

	return processed_num;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_close_problem                                                *
 *                                                                            *
 * Purpose: closes problem event                                              *
 *                                                                            *
 * Parameters: triggerid - [IN] the source trigger id                         *
 *             eventid   - [IN] the event to close                            *
 *             userid    - [IN] the user closing the event                    *
 *                                                                            *
 * Return value: SUCCEED - the problem was closed                             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	trx_close_problem(trx_uint64_t triggerid, trx_uint64_t eventid, trx_uint64_t userid)
{
	DC_TRIGGER	trigger;
	int		errcode, processed_num = 0;
	trx_timespec_t	ts;
	DB_EVENT	*r_event;

	DCconfig_get_triggers_by_triggerids(&trigger, &triggerid, &errcode, 1);

	if (SUCCEED == errcode)
	{
		trx_vector_ptr_t	trigger_diff;

		trx_vector_ptr_create(&trigger_diff);

		trx_append_trigger_diff(&trigger_diff, triggerid, trigger.priority,
				TRX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT, trigger.value,
				TRIGGER_STATE_NORMAL, 0, NULL);

		trx_timespec(&ts);

		DBbegin();

		r_event = close_trigger_event(eventid, triggerid, &ts, userid, 0, 0, trigger.description,
				trigger.expression_orig, trigger.recovery_expression_orig, trigger.priority,
				trigger.type, trigger.opdata);

		r_event->eventid = DBget_maxid_num("events", 1);

		processed_num = flush_events();
		update_trigger_changes(&trigger_diff);
		trx_db_save_trigger_changes(&trigger_diff);

		DBcommit();

		DCconfig_triggers_apply_changes(&trigger_diff);
		DBupdate_itservices(&trigger_diff);

		if (SUCCEED == trx_is_export_enabled())
			trx_export_events();

		trx_clean_events();
		trx_vector_ptr_clear_ext(&trigger_diff, (trx_clean_func_t)trx_trigger_diff_free);
		trx_vector_ptr_destroy(&trigger_diff);
	}

	DCconfig_clean_triggers(&trigger, &errcode, 1);

	return (0 == processed_num ? FAIL : SUCCEED);
}
