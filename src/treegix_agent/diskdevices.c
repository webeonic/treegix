

#ifndef _WINDOWS

#include "common.h"
#include "diskdevices.h"
#include "stats.h"
#include "log.h"
#include "mutexs.h"

extern zbx_mutex_t		diskstats_lock;
#define LOCK_DISKSTATS		zbx_mutex_lock(diskstats_lock)
#define UNLOCK_DISKSTATS	zbx_mutex_unlock(diskstats_lock)

static void	apply_diskstat(TRX_SINGLE_DISKDEVICE_DATA *device, time_t now, zbx_uint64_t *dstat)
{
	register int	i;
	time_t		clock[TRX_AVG_COUNT], sec;
	int		index[TRX_AVG_COUNT];

	assert(device);

	device->index++;

	if (MAX_COLLECTOR_HISTORY == device->index)
		device->index = 0;

	device->clock[device->index] = now;
	device->r_sect[device->index] = dstat[TRX_DSTAT_R_SECT];
	device->r_oper[device->index] = dstat[TRX_DSTAT_R_OPER];
	device->r_byte[device->index] = dstat[TRX_DSTAT_R_BYTE];
	device->w_sect[device->index] = dstat[TRX_DSTAT_W_SECT];
	device->w_oper[device->index] = dstat[TRX_DSTAT_W_OPER];
	device->w_byte[device->index] = dstat[TRX_DSTAT_W_BYTE];

	clock[TRX_AVG1] = clock[TRX_AVG5] = clock[TRX_AVG15] = now + 1;
	index[TRX_AVG1] = index[TRX_AVG5] = index[TRX_AVG15] = -1;

	for (i = 0; i < MAX_COLLECTOR_HISTORY; i++)
	{
		if (0 == device->clock[i])
			continue;

#define DISKSTAT(t)\
		if ((device->clock[i] >= (now - (t * 60))) && (clock[TRX_AVG ## t] > device->clock[i]))\
		{\
			clock[TRX_AVG ## t] = device->clock[i];\
			index[TRX_AVG ## t] = i;\
		}

		DISKSTAT(1);
		DISKSTAT(5);
		DISKSTAT(15);
	}

#define SAVE_DISKSTAT(t)\
	if (-1 == index[TRX_AVG ## t] || 0 == now - device->clock[index[TRX_AVG ## t]])\
	{\
		device->r_sps[TRX_AVG ## t] = 0;\
		device->r_ops[TRX_AVG ## t] = 0;\
		device->r_bps[TRX_AVG ## t] = 0;\
		device->w_sps[TRX_AVG ## t] = 0;\
		device->w_ops[TRX_AVG ## t] = 0;\
		device->w_bps[TRX_AVG ## t] = 0;\
	}\
	else\
	{\
		sec = now - device->clock[index[TRX_AVG ## t]];\
		device->r_sps[TRX_AVG ## t] = (dstat[TRX_DSTAT_R_SECT] - device->r_sect[index[TRX_AVG ## t]]) / (double)sec;\
		device->r_ops[TRX_AVG ## t] = (dstat[TRX_DSTAT_R_OPER] - device->r_oper[index[TRX_AVG ## t]]) / (double)sec;\
		device->r_bps[TRX_AVG ## t] = (dstat[TRX_DSTAT_R_BYTE] - device->r_byte[index[TRX_AVG ## t]]) / (double)sec;\
		device->w_sps[TRX_AVG ## t] = (dstat[TRX_DSTAT_W_SECT] - device->w_sect[index[TRX_AVG ## t]]) / (double)sec;\
		device->w_ops[TRX_AVG ## t] = (dstat[TRX_DSTAT_W_OPER] - device->w_oper[index[TRX_AVG ## t]]) / (double)sec;\
		device->w_bps[TRX_AVG ## t] = (dstat[TRX_DSTAT_W_BYTE] - device->w_byte[index[TRX_AVG ## t]]) / (double)sec;\
	}

	SAVE_DISKSTAT(1);
	SAVE_DISKSTAT(5);
	SAVE_DISKSTAT(15);
}

static void	process_diskstat(TRX_SINGLE_DISKDEVICE_DATA *device)
{
	time_t		now;
	zbx_uint64_t	dstat[TRX_DSTAT_MAX];

	now = time(NULL);
	if (FAIL == get_diskstat(device->name, dstat))
		return;

	apply_diskstat(device, now, dstat);

	device->ticks_since_polled++;
}

void	collect_stats_diskdevices(void)
{
	int	i;

	LOCK_DISKSTATS;
	diskstat_shm_reattach();

	for (i = 0; i < diskdevices->count; i++)
	{
		process_diskstat(&diskdevices->device[i]);

		/* remove device from collector if not being polled for long time */
		if (DISKDEVICE_TTL <= diskdevices->device[i].ticks_since_polled)
		{
			if ((diskdevices->count - 1) > i)
			{
				memcpy(diskdevices->device + i, diskdevices->device + i + 1,
					sizeof(TRX_SINGLE_DISKDEVICE_DATA) * (diskdevices->count - i));
			}

			diskdevices->count--;
			i--;
		}
	}

	UNLOCK_DISKSTATS;
}

TRX_SINGLE_DISKDEVICE_DATA	*collector_diskdevice_get(const char *devname)
{
	int				i;
	TRX_SINGLE_DISKDEVICE_DATA	*device = NULL;

	assert(devname);

	treegix_log(LOG_LEVEL_DEBUG, "In %s() devname:'%s'", __func__, devname);

	LOCK_DISKSTATS;
	if (0 == DISKDEVICE_COLLECTOR_STARTED(collector))
		diskstat_shm_init();
	else
		diskstat_shm_reattach();

	for (i = 0; i < diskdevices->count; i++)
	{
		if (0 == strcmp(devname, diskdevices->device[i].name))
		{
			device = &diskdevices->device[i];
			device->ticks_since_polled = 0;
			treegix_log(LOG_LEVEL_DEBUG, "%s() device '%s' found", __func__, devname);
			break;
		}
	}
	UNLOCK_DISKSTATS;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)device);

	return device;
}

TRX_SINGLE_DISKDEVICE_DATA	*collector_diskdevice_add(const char *devname)
{
	TRX_SINGLE_DISKDEVICE_DATA	*device = NULL;

	assert(devname);

	treegix_log(LOG_LEVEL_DEBUG, "In %s() devname:'%s'", __func__, devname);

	LOCK_DISKSTATS;
	if (0 == DISKDEVICE_COLLECTOR_STARTED(collector))
		diskstat_shm_init();
	else
		diskstat_shm_reattach();

	if (diskdevices->count == MAX_DISKDEVICES)
	{
		treegix_log(LOG_LEVEL_DEBUG, "%s() collector is full", __func__);
		goto end;
	}

	if (diskdevices->count == diskdevices->max_diskdev)
		diskstat_shm_extend();

	device = &(diskdevices->device[diskdevices->count]);
	memset(device, 0, sizeof(TRX_SINGLE_DISKDEVICE_DATA));
	zbx_strlcpy(device->name, devname, sizeof(device->name));
	device->index = -1;
	device->ticks_since_polled = 0;
	(diskdevices->count)++;

	process_diskstat(device);
end:
	UNLOCK_DISKSTATS;

	treegix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)device);

	return device;
}

#endif	/* _WINDOWS */
