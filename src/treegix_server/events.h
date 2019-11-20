

#ifndef TREEGIX_EVENTS_H
#define TREEGIX_EVENTS_H

void	trx_initialize_events(void);
void	trx_uninitialize_events(void);
DB_EVENT	*trx_add_event(unsigned char source, unsigned char object, trx_uint64_t objectid,
		const trx_timespec_t *timespec, int value, const char *trigger_description,
		const char *trigger_expression, const char *trigger_recovery_expression, unsigned char trigger_priority,
		unsigned char trigger_type, const trx_vector_ptr_t *trigger_tags,
		unsigned char trigger_correlation_mode, const char *trigger_correlation_tag,
		unsigned char trigger_value, const char *trigger_opdata, const char *error);

int	trx_close_problem(trx_uint64_t triggerid, trx_uint64_t eventid, trx_uint64_t userid);

int	trx_process_events(trx_vector_ptr_t *trigger_diff, trx_vector_uint64_t *triggerids_lock);
void	trx_clean_events(void);
void	trx_reset_event_recovery(void);
void	trx_export_events(void);

#endif
