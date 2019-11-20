

#ifndef TREEGIX_VMSTAT_H
#define TREEGIX_VMSTAT_H

#include "sysinfo.h"

#ifdef _AIX

typedef struct
{
	/* public */
	unsigned char	enabled;		/* collecting enabled */
	unsigned char	data_available;		/* data is collected and available */
	unsigned char	shared_enabled; 	/* partition runs in shared mode */
	unsigned char	pool_util_authority;	/* pool utilization available */
	unsigned char	aix52stats;
	/* - general -- */
	double		ent;
	/* --- kthr --- */
	double		kthr_r, kthr_b/*, kthr_p*/;
	/* --- page --- */
	double		fi, fo, pi, po, fr, sr;
	/* -- faults -- */
	double		in, sy, cs;
	/* --- cpu ---- */
	double		cpu_us, cpu_sy, cpu_id, cpu_wa, cpu_pc, cpu_ec, cpu_lbusy, cpu_app;
	/* --- disk --- */
	trx_uint64_t	disk_bps;
	double		disk_tps;
	/* -- memory -- */
	trx_uint64_t	mem_avm, mem_fre;
}
TRX_VMSTAT_DATA;

#define VMSTAT_COLLECTOR_STARTED(collector)	(collector)

void	collect_vmstat_data(TRX_VMSTAT_DATA *vmstat);

#endif /* _AIX */

#endif
