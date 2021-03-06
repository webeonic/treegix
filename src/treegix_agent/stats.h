

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

#ifdef TRX_PROCSTAT_COLLECTOR
#	include "procstat.h"
#endif

typedef struct
{
	TRX_CPUS_STAT_DATA	cpus;
#ifndef _WINDOWS
	int 			diskstat_shmid;
#endif
#ifdef TRX_PROCSTAT_COLLECTOR
	trx_dshm_t		procstat;
#endif
#ifdef _AIX
	TRX_VMSTAT_DATA		vmstat;
#endif
}
TRX_COLLECTOR_DATA;

extern TRX_COLLECTOR_DATA	*collector;
#ifndef _WINDOWS
extern TRX_DISKDEVICES_DATA	*diskdevices;
extern int			my_diskstat_shmid;
#endif

TRX_THREAD_ENTRY(collector_thread, pSemColectorStarted);

int	init_collector_data(char **error);
void	free_collector_data(void);
void	diskstat_shm_init(void);
void	diskstat_shm_reattach(void);
void	diskstat_shm_extend(void);

#endif	/* TREEGIX_STATS_H */
