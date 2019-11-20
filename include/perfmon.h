

#ifndef TREEGIX_PERFMON_H
#define TREEGIX_PERFMON_H

#ifndef _WINDOWS
#	error "This module is only available for Windows OS"
#endif

/* this struct must be only modified along with mapping builtin_counter_ref[] in perfmon.c */
typedef enum
{
	PCI_SYSTEM = 0,
	PCI_PROCESSOR,
	PCI_PROCESSOR_INFORMATION,
	PCI_PROCESSOR_TIME,
	PCI_PROCESSOR_QUEUE_LENGTH,
	PCI_SYSTEM_UP_TIME,
	PCI_TERMINAL_SERVICES,
	PCI_TOTAL_SESSIONS,
	PCI_MAX_INDEX = PCI_TOTAL_SESSIONS
}
trx_builtin_counter_ref_t;

typedef enum
{
	PERF_COUNTER_NOTSUPPORTED = 0,
	PERF_COUNTER_INITIALIZED,
	PERF_COUNTER_GET_SECOND_VALUE,	/* waiting for the second raw value (needed for some, e.g. rate, counters) */
	PERF_COUNTER_ACTIVE
}
trx_perf_counter_status_t;

typedef enum
{
	PERF_COUNTER_LANG_DEFAULT = 0,
	PERF_COUNTER_LANG_EN
}
trx_perf_counter_lang_t;

typedef struct perf_counter_id
{
	struct perf_counter_id	*next;
	unsigned long		pdhIndex;
	wchar_t			name[PDH_MAX_COUNTER_NAME];
}
trx_perf_counter_id_t;

typedef struct perf_counter_data
{
	struct perf_counter_data	*next;
	char				*name;
	char				*counterpath;
	int				interval;
	trx_perf_counter_lang_t		lang;
	trx_perf_counter_status_t	status;
	HCOUNTER			handle;
	PDH_RAW_COUNTER			rawValues[2];	/* rate counters need two raw values */
	int				olderRawValue;	/* index of the older of both values */
	double				*value_array;	/* a circular buffer of values */
	int				value_current;	/* index of the last stored value */
	int				value_count;	/* number of values in the array */
	double				sum;		/* sum of last value_count values */
}
trx_perf_counter_data_t;

PDH_STATUS	trx_PdhMakeCounterPath(const char *function, PDH_COUNTER_PATH_ELEMENTS *cpe, char *counterpath);
PDH_STATUS	trx_PdhOpenQuery(const char *function, PDH_HQUERY query);
PDH_STATUS	trx_PdhAddCounter(const char *function, trx_perf_counter_data_t *counter, PDH_HQUERY query,
		const char *counterpath, trx_perf_counter_lang_t lang, PDH_HCOUNTER *handle);
PDH_STATUS	trx_PdhCollectQueryData(const char *function, const char *counterpath, PDH_HQUERY query);
PDH_STATUS	trx_PdhGetRawCounterValue(const char *function, const char *counterpath, PDH_HCOUNTER handle, PPDH_RAW_COUNTER value);

PDH_STATUS	calculate_counter_value(const char *function, const char *counterpath, trx_perf_counter_lang_t lang, double *value);
wchar_t		*get_counter_name(DWORD pdhIndex);
int		check_counter_path(char *counterPath, int convert_from_numeric);
int		init_builtin_counter_indexes(void);
DWORD 		get_builtin_counter_index(trx_builtin_counter_ref_t ref);

#endif /* TREEGIX_PERFMON_H */
