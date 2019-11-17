

#ifndef TREEGIX_STATS_H
#define TREEGIX_STATS_H

#include "threads.h"
#include "mutexs.h"

#ifndef _WINDOWS
#	include "diskdevices.h"
#	include "ipc.h"
#endif

#include "cpustat.h"

#ifdef _AIX
#	include "vmstats.h"
#endif

#ifdef ZBX_PROCSTAT_COLLECTOR
#	include "procstat.h"
#endif

typedef struct
{
	ZBX_CPUS_STAT_DATA	cpus;
#ifndef _WINDOWS
	int 			diskstat_shmid;
#endif
#ifdef ZBX_PROCSTAT_COLLECTOR
	zbx_dshm_t		procstat;
#endif
#ifdef _AIX
	ZBX_VMSTAT_DATA		vmstat;
#endif
}
ZBX_COLLECTOR_DATA;

extern ZBX_COLLECTOR_DATA	*collector;
#ifndef _WINDOWS
extern ZBX_DISKDEVICES_DATA	*diskdevices;
extern int			my_diskstat_shmid;
#endif

ZBX_THREAD_ENTRY(collector_thread, pSemColectorStarted);

int	init_collector_data(char **error);
void	free_collector_data(void);
void	diskstat_shm_init(void);
void	diskstat_shm_reattach(void);
void	diskstat_shm_extend(void);

#endif	/* TREEGIX_STATS_H */
