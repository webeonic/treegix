

#ifndef TREEGIX_PROCSTAT_H
#define TREEGIX_PROCSTAT_H

#ifdef TRX_PROCSTAT_COLLECTOR

#define TRX_PROCSTAT_CPU_USER			0x01
#define TRX_PROCSTAT_CPU_SYSTEM			0x02
#define TRX_PROCSTAT_CPU_TOTAL			(TRX_PROCSTAT_CPU_USER | TRX_PROCSTAT_CPU_SYSTEM)

#define TRX_PROCSTAT_FLAGS_ZONE_CURRENT		0
#define TRX_PROCSTAT_FLAGS_ZONE_ALL		1

/* process cpu utilization data */
typedef struct
{
	pid_t		pid;

	/* errno error code */
	int		error;

	trx_uint64_t	utime;
	trx_uint64_t	stime;

	/* process start time, used to validate if the old */
	/* snapshot data belongs to the same process       */
	trx_uint64_t	starttime;
}
trx_procstat_util_t;

void	trx_procstat_init(void);
void	trx_procstat_destroy(void);
int	trx_procstat_collector_started(void);
int	trx_procstat_get_util(const char *procname, const char *username, const char *cmdline, trx_uint64_t flags,
		int period, int type, double *value, char **errmsg);
void	trx_procstat_collect(void);

/* external functions used by procstat collector */
int	trx_proc_get_processes(trx_vector_ptr_t *processes, unsigned int flags);
void	trx_proc_get_matching_pids(const trx_vector_ptr_t *processes, const char *procname, const char *username,
		const char *cmdline, trx_uint64_t flags, trx_vector_uint64_t *pids);
void	trx_proc_get_process_stats(trx_procstat_util_t *procs, int procs_num);
void	trx_proc_free_processes(trx_vector_ptr_t *processes);

#endif	/* TRX_PROCSTAT_COLLECTOR */

#endif	/* TREEGIX_PROCSTAT_H */
