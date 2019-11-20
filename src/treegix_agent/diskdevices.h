

#ifndef TREEGIX_DISKDEVICES_H
#define TREEGIX_DISKDEVICES_H

#ifdef _WINDOWS
#	error "This module allowed only for Unix OS"
#endif

#include "sysinfo.h"

#define	MAX_DISKDEVICES	1024

/* Disk device time to live: if disk statistics is being collected but not polled (using passive  */
/* or active check) DISKDEVICE_TTL or more seconds then delete this disk from collector.          */
/* Update interval for vfs.dev.read[] and vfs.dev.write[] items must be less than DISKDEVICE_TTL. */
#define	DISKDEVICE_TTL	(3 * SEC_PER_HOUR)

typedef struct c_single_diskdevice_data
{
	char		name[32];
	int		index;
	/* Counter used to detect devices no longer polled and to delete them from collector. It is set */
	/* to 0 when disk statistics is polled and incremented when disk statistics is updated. For     */
	/* example, value 3600 means that approximately 1 hour statistics was not polled for this disk. */
	int 		ticks_since_polled;
	time_t		clock[MAX_COLLECTOR_HISTORY];
	trx_uint64_t	r_sect[MAX_COLLECTOR_HISTORY];
	trx_uint64_t	r_oper[MAX_COLLECTOR_HISTORY];
	trx_uint64_t	r_byte[MAX_COLLECTOR_HISTORY];
	trx_uint64_t	w_sect[MAX_COLLECTOR_HISTORY];
	trx_uint64_t	w_oper[MAX_COLLECTOR_HISTORY];
	trx_uint64_t	w_byte[MAX_COLLECTOR_HISTORY];
	double		r_sps[TRX_AVG_COUNT];
	double		r_ops[TRX_AVG_COUNT];
	double		r_bps[TRX_AVG_COUNT];
	double		w_sps[TRX_AVG_COUNT];
	double		w_ops[TRX_AVG_COUNT];
	double		w_bps[TRX_AVG_COUNT];
} TRX_SINGLE_DISKDEVICE_DATA;

typedef struct c_diskdevices_data
{
	int				count;		/* number of disks to collect statistics for */
	int				max_diskdev;	/* number of "slots" for disk statistics */
	TRX_SINGLE_DISKDEVICE_DATA	device[1];	/* more "slots" for disk statistics added dynamically */
} TRX_DISKDEVICES_DATA;

#define DISKDEVICE_COLLECTOR_STARTED(collector)	((collector) && (collector)->diskstat_shmid != TRX_NONEXISTENT_SHMID)

TRX_SINGLE_DISKDEVICE_DATA	*collector_diskdevice_get(const char *devname);
TRX_SINGLE_DISKDEVICE_DATA	*collector_diskdevice_add(const char *devname);
void				collect_stats_diskdevices(void);

#endif
