

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

	zbx_uint64_t	utime;
	zbx_uint64_t	stime;

	/* process start time, used to validate if the old */
	/* snapshot data belongs to the same process       */
	zbx_uint64_t	starttime;
}
zbx_procstat_util_t;

void	zbx_procstat_init(void);
void	zbx_procstat_destroy(void);
int	zbx_procstat_collector_started(void);
int	zbx_procstat_get_util(const char *procname, const char *username, const char *cmdline, zbx_uint64_t flags,
		int period, int type, double *value, char **errmsg);
void	zbx_procstat_collect(void);

/* external functions used by procstat collector */
int	zbx_proc_get_processes(zbx_vector_ptr_t *processes, unsigned int flags);
void	zbx_proc_get_matching_pids(const zbx_vector_ptr_t *processes, const char *procname, const char *username,
		const char *cmdline, zbx_uint64_t flags, zbx_vector_uint64_t *pids);
void	zbx_proc_get_process_stats(zbx_procstat_util_t *procs, int procs_num);
void	zbx_proc_free_processes(zbx_vector_ptr_t *processes);

#endif	/* TRX_PROCSTAT_COLLECTOR */

#endif	/* TREEGIX_PROCSTAT_H */
