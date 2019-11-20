

#include "common.h"

#include "db.h"
#include "log.h"
#include "dbcache.h"
#include "trxserver.h"
#include "template.h"
#include "events.h"

#define TRX_FLAGS_TRIGGER_CREATE_NOTHING		0x00
#define TRX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT		0x01
#define TRX_FLAGS_TRIGGER_CREATE_INTERNAL_EVENT		0x02
#define TRX_FLAGS_TRIGGER_CREATE_EVENT										\
		(TRX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT | TRX_FLAGS_TRIGGER_CREATE_INTERNAL_EVENT)


/******************************************************************************
 *                                                                            *
 * Function: trx_process_trigger                                              *
 *                                                                            *
 * Purpose: 1) calculate changeset of trigger fields to be updated            *
 *          2) generate events                                                *
 *                                                                            *
 * Parameters: trigger - [IN] the trigger to process                          *
 *             diffs   - [OUT] the vector with trigger changes                *
 *                                                                            *
 * Return value: SUCCEED - trigger processed successfully                     *
 *               FAIL    - no changes                                         *
 *                                                                            *
 * Comments: Trigger dependency checks will be done during event processing.  *
 *                                                                            *
 * Event generation depending on trigger value/state changes:                 *
 *                                                                            *
 * From \ To  | OK         | OK(?)      | PROBLEM    | PROBLEM(?) | NONE      *
 *----------------------------------------------------------------------------*
 * OK         | .          | I          | E          | I          | .         *
 *            |            |            |            |            |           *
 * OK(?)      | I          | .          | E,I        | -          | I         *
 *            |            |            |            |            |           *
 * PROBLEM    | E          | I          | E(m)       | I          | .         *
 *            |            |            |            |            |           *
 * PROBLEM(?) | E,I        | -          | E(m),I     | .          | I         *
 *                                                                            *
 * Legend:                                                                    *
 *        'E' - trigger event                                                 *
 *        'I' - internal event                                                *
 *        '.' - nothing                                                       *
 *        '-' - should never happen                                           *
 *                                                                            *
 ******************************************************************************/
static int	trx_process_trigger(struct _DC_TRIGGER *trigger, trx_vector_ptr_t *diffs)
{
	const char		*new_error;
	int			new_state, new_value, ret = FAIL;
	trx_uint64_t		flags = TRX_FLAGS_TRIGGER_DIFF_UNSET, event_flags = TRX_FLAGS_TRIGGER_CREATE_NOTHING;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() triggerid:" TRX_FS_UI64 " value:%d(%d) new_value:%d",
			__func__, trigger->triggerid, trigger->value, trigger->state, trigger->new_value);

	if (TRIGGER_VALUE_UNKNOWN == trigger->new_value)
	{
		new_state = TRIGGER_STATE_UNKNOWN;
		new_value = trigger->value;
	}
	else
	{
		new_state = TRIGGER_STATE_NORMAL;
		new_value = trigger->new_value;
	}
	new_error = (NULL == trigger->new_error ? "" : trigger->new_error);

	if (trigger->state != new_state)
	{
		flags |= TRX_FLAGS_TRIGGER_DIFF_UPDATE_STATE;
		event_flags |= TRX_FLAGS_TRIGGER_CREATE_INTERNAL_EVENT;
	}

	if (0 != strcmp(trigger->error, new_error))
		flags |= TRX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR;

	if (TRIGGER_STATE_NORMAL == new_state)
	{
		if (TRIGGER_VALUE_PROBLEM == new_value)
		{
			if (TRIGGER_VALUE_OK == trigger->value || TRIGGER_TYPE_MULTIPLE_TRUE == trigger->type)
				event_flags |= TRX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT;
		}
		else if (TRIGGER_VALUE_OK == new_value)
		{
			if (TRIGGER_VALUE_PROBLEM == trigger->value || 0 == trigger->lastchange)
				event_flags |= TRX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT;
		}
	}

	/* check if there is something to be updated */
	if (0 == (flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE) && 0 == (event_flags & TRX_FLAGS_TRIGGER_CREATE_EVENT))
		goto out;

	if (0 != (event_flags & TRX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT))
	{
		trx_add_event(EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, trigger->triggerid,
				&trigger->timespec, new_value, trigger->description,
				trigger->expression_orig, trigger->recovery_expression_orig,
				trigger->priority, trigger->type, &trigger->tags,
				trigger->correlation_mode, trigger->correlation_tag, trigger->value, trigger->opdata,
				NULL);
	}

	if (0 != (event_flags & TRX_FLAGS_TRIGGER_CREATE_INTERNAL_EVENT))
	{
		trx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_TRIGGER, trigger->triggerid,
				&trigger->timespec, new_state, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL,
				new_error);
	}

	trx_append_trigger_diff(diffs, trigger->triggerid, trigger->priority, flags, trigger->value, new_state,
			trigger->timespec.sec, new_error);

	ret = SUCCEED;
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%s flags:" TRX_FS_UI64, __func__, trx_result_string(ret),
			flags);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_db_save_trigger_changes                                      *
 *                                                                            *
 * Purpose: save the trigger changes to database                              *
 *                                                                            *
 * Parameters:trigger_diff - [IN] the trigger changeset                       *
 *                                                                            *
 ******************************************************************************/
void	trx_db_save_trigger_changes(const trx_vector_ptr_t *trigger_diff)
{
	int				i;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	const trx_trigger_diff_t	*diff;

	treegix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < trigger_diff->values_num; i++)
	{
		char	delim = ' ';
		diff = (const trx_trigger_diff_t *)trigger_diff->values[i];

		if (0 == (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE))
			continue;

		trx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update triggers set");

		if (0 != (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%clastchange=%d", delim, diff->lastchange);
			delim = ',';
		}

		if (0 != (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cvalue=%d", delim, diff->value);
			delim = ',';
		}

		if (0 != (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_STATE))
		{
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cstate=%d", delim, diff->state);
			delim = ',';
		}

		if (0 != (diff->flags & TRX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR))
		{
			char	*error_esc;

			error_esc = DBdyn_escape_field("triggers", "error", diff->error);
			trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cerror='%s'", delim, error_esc);
			trx_free(error_esc);
		}

		trx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where triggerid=" TRX_FS_UI64 ";\n",
				diff->triggerid);

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (sql_offset > 16)	/* in ORACLE always present begin..end; */
		DBexecute("%s", sql);

	trx_free(sql);

	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_trigger_diff_free                                            *
 *                                                                            *
 * Purpose: frees trigger changeset                                           *
 *                                                                            *
 ******************************************************************************/
void	trx_trigger_diff_free(trx_trigger_diff_t *diff)
{
	trx_free(diff->error);
	trx_free(diff);
}

/******************************************************************************
 *                                                                            *
 * Comments: helper function for trx_process_triggers()                       *
 *                                                                            *
 ******************************************************************************/
static int	trx_trigger_topoindex_compare(const void *d1, const void *d2)
{
	const DC_TRIGGER	*t1 = *(const DC_TRIGGER **)d1;
	const DC_TRIGGER	*t2 = *(const DC_TRIGGER **)d2;

	TRX_RETURN_IF_NOT_EQUAL(t1->topoindex, t2->topoindex);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: trx_process_triggers                                             *
 *                                                                            *
 * Purpose: process triggers - calculates property changeset and generates    *
 *          events                                                            *
 *                                                                            *
 * Parameters: triggers     - [IN] the triggers to process                    *
 *             trigger_diff - [OUT] the trigger changeset                     *
 *                                                                            *
 * Comments: The trigger_diff changeset must be cleaned by the caller:        *
 *                trx_vector_ptr_clear_ext(trigger_diff,                      *
 *                              (trx_clean_func_t)trx_trigger_diff_free);     *
 *                                                                            *
 ******************************************************************************/
void	trx_process_triggers(trx_vector_ptr_t *triggers, trx_vector_ptr_t *trigger_diff)
{
	int	i;

	treegix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __func__, triggers->values_num);

	if (0 == triggers->values_num)
		goto out;

	trx_vector_ptr_sort(triggers, trx_trigger_topoindex_compare);

	for (i = 0; i < triggers->values_num; i++)
		trx_process_trigger((struct _DC_TRIGGER *)triggers->values[i], trigger_diff);

	trx_vector_ptr_sort(trigger_diff, TRX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
out:
	treegix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: trx_append_trigger_diff                                          *
 *                                                                            *
 * Purpose: Adds a new trigger diff to trigger changeset vector               *
 *                                                                            *
 ******************************************************************************/
void	trx_append_trigger_diff(trx_vector_ptr_t *trigger_diff, trx_uint64_t triggerid, unsigned char priority,
		trx_uint64_t flags, unsigned char value, unsigned char state, int lastchange, const char *error)
{
	trx_trigger_diff_t	*diff;

	diff = (trx_trigger_diff_t *)trx_malloc(NULL, sizeof(trx_trigger_diff_t));
	diff->triggerid = triggerid;
	diff->priority = priority;
	diff->flags = flags;
	diff->value = value;
	diff->state = state;
	diff->lastchange = lastchange;
	diff->error = (NULL != error ? trx_strdup(NULL, error) : NULL);

	diff->problem_count = 0;

	trx_vector_ptr_append(trigger_diff, diff);
}
