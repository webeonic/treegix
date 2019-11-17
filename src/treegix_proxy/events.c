

#include "common.h"
#include "zbxalgo.h"
#include "db.h"
#include "../treegix_server/events.h"

void	zbx_initialize_events(void)
{
	THIS_SHOULD_NEVER_HAPPEN;
}

void	zbx_uninitialize_events(void)
{
	THIS_SHOULD_NEVER_HAPPEN;
}

DB_EVENT	*zbx_add_event(unsigned char source, unsigned char object, zbx_uint64_t objectid,
		const zbx_timespec_t *timespec, int value, const char *trigger_description,
		const char *trigger_expression, const char *trigger_recovery_expression, unsigned char trigger_priority,
		unsigned char trigger_type, const zbx_vector_ptr_t *trigger_tags,
		unsigned char trigger_correlation_mode, const char *trigger_correlation_tag,
		unsigned char trigger_value, const char *trigger_opdata, const char *error)
{
	TRX_UNUSED(source);
	TRX_UNUSED(object);
	TRX_UNUSED(objectid);
	TRX_UNUSED(timespec);
	TRX_UNUSED(value);
	TRX_UNUSED(trigger_description);
	TRX_UNUSED(trigger_expression);
	TRX_UNUSED(trigger_recovery_expression);
	TRX_UNUSED(trigger_priority);
	TRX_UNUSED(trigger_type);
	TRX_UNUSED(trigger_tags);
	TRX_UNUSED(trigger_correlation_mode);
	TRX_UNUSED(trigger_correlation_tag);
	TRX_UNUSED(trigger_value);
	TRX_UNUSED(trigger_opdata);
	TRX_UNUSED(error);

	THIS_SHOULD_NEVER_HAPPEN;

	return NULL;
}

int	zbx_close_problem(zbx_uint64_t triggerid, zbx_uint64_t eventid, zbx_uint64_t userid)
{
	TRX_UNUSED(triggerid);
	TRX_UNUSED(eventid);
	TRX_UNUSED(userid);

	THIS_SHOULD_NEVER_HAPPEN;
	return 0;
}

int	zbx_process_events(zbx_vector_ptr_t *trigger_diff, zbx_vector_uint64_t *triggerids_lock)
{
	TRX_UNUSED(trigger_diff);
	TRX_UNUSED(triggerids_lock);

	THIS_SHOULD_NEVER_HAPPEN;
	return 0;
}

void	zbx_clean_events(void)
{
	THIS_SHOULD_NEVER_HAPPEN;
}

void	zbx_reset_event_recovery(void)
{
	THIS_SHOULD_NEVER_HAPPEN;
}

void	zbx_export_events(void)
{
	THIS_SHOULD_NEVER_HAPPEN;
}
