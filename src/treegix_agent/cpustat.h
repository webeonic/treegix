

#ifndef TREEGIX_CPUSTAT_H
#define TREEGIX_CPUSTAT_H

#include "sysinfo.h"
#include "zbxalgo.h"

#ifdef _WINDOWS
#	include "perfmon.h"

typedef struct
{
	zbx_perf_counter_data_t	**cpu_counter;
	zbx_perf_counter_data_t	*queue_counter;
	int			count;
}
TRX_CPUS_STAT_DATA;

#define CPU_COLLECTOR_STARTED(collector)	((collector) && (collector)->cpus.queue_counter)

int	get_cpu_perf_counter_value(int cpu_num, int interval, double *value, char **error);

#else	/* not _WINDOWS */

typedef struct
{
	zbx_uint64_t	h_counter[TRX_CPU_STATE_COUNT][MAX_COLLECTOR_HISTORY];
	unsigned char	h_status[MAX_COLLECTOR_HISTORY];
#if (MAX_COLLECTOR_HISTORY % 8) > 0
	unsigned char	padding0[8 - (MAX_COLLECTOR_HISTORY % 8)];	/* for 8-byte alignment */
#endif
	int		h_first;
	int		h_count;
	int		cpu_num;
	int		padding1;	/* for 8-byte alignment */
}
TRX_SINGLE_CPU_STAT_DATA;

typedef struct
{
	TRX_SINGLE_CPU_STAT_DATA	*cpu;
	int				count;
}
TRX_CPUS_STAT_DATA;

#define CPU_COLLECTOR_STARTED(collector)	(collector)

void	collect_cpustat(TRX_CPUS_STAT_DATA *pcpus);
int	get_cpustat(AGENT_RESULT *result, int cpu_num, int state, int mode);

#endif	/* _WINDOWS */

int	init_cpu_collector(TRX_CPUS_STAT_DATA *pcpus);
void	free_cpu_collector(TRX_CPUS_STAT_DATA *pcpus);

#define TRX_CPUNUM_UNDEF	-1	/* unidentified yet CPUs */
#define TRX_CPUNUM_ALL		-2	/* request data for all CPUs */

#define TRX_CPU_STATUS_ONLINE	0
#define TRX_CPU_STATUS_OFFLINE	1
#define TRX_CPU_STATUS_UNKNOWN	2

int	get_cpus(zbx_vector_uint64_pair_t *vector);

#endif
