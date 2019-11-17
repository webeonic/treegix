

#ifndef TREEGIX_PERFSTAT_H
#define TREEGIX_PERFSTAT_H

#ifndef _WINDOWS
#	error "This module is only available for Windows OS"
#endif

#include "perfmon.h"

zbx_perf_counter_data_t	*add_perf_counter(const char *name, const char *counterpath, int interval,
		zbx_perf_counter_lang_t lang, char **error);
void			remove_perf_counter(zbx_perf_counter_data_t *counter);

typedef enum
{
	ZBX_SINGLE_THREADED,
	ZBX_MULTI_THREADED
}
zbx_threadedness_t;

int	init_perf_collector(zbx_threadedness_t threadedness, char **error);
void	free_perf_collector(void);
void	collect_perfstat(void);

int	get_perf_counter_value_by_name(const char *name, double *value, char **error);
int	get_perf_counter_value_by_path(const char *counterpath, int interval, zbx_perf_counter_lang_t lang,
		double *value, char **error);
int	get_perf_counter_value(zbx_perf_counter_data_t *counter, int interval, double *value, char **error);

#endif
