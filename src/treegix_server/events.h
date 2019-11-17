

#ifndef TREEGIX_EVENTS_H
#define TREEGIX_EVENTS_H

void	zbx_initialize_events(void);
void	zbx_uninitialize_events(void);
DB_EVENT	*zbx_add_event(unsigned char source, unsigned char object, zbx_uint64_t objectid,
		const zbx_timespec_t *timespec, int value, const char *trigger_description,
		const char *trigger_expression, const char *trigger_recovery_expression, unsigned char trigger_priority,
		unsigned char trigger_type, const zbx_vector_ptr_t *trigger_tags,
		unsigned char trigger_correlation_mode, const char *trigger_correlation_tag,
		unsigned char trigger_value, const char *trigger_opdata, const char *error);

int	zbx_close_problem(zbx_uint64_t triggerid, zbx_uint64_t eventid, zbx_uint64_t userid);

int	zbx_process_events(zbx_vector_ptr_t *trigger_diff, zbx_vector_uint64_t *triggerids_lock);
void	zbx_clean_events(void);
void	zbx_reset_event_recovery(void);
void	zbx_export_events(void);

#endif
