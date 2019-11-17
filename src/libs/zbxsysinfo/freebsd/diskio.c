

#include "common.h"
#include "sysinfo.h"
#include "stats.h"
#include "diskdevices.h"

#define TRX_DEV_PFX	"/dev/"
#define TRX_DEV_READ	0
#define TRX_DEV_WRITE	1

static struct statinfo	*si = NULL;

int	get_diskstat(const char *devname, zbx_uint64_t *dstat)
{
	int		i;
	struct devstat	*ds = NULL;
	int		ret = FAIL;
	char		dev[DEVSTAT_NAME_LEN + 10];
	const char	*pd;	/* pointer to device name without '/dev/' prefix, e.g. 'da0' */

	assert(devname);

	for (i = 0; i < TRX_DSTAT_MAX; i++)
		dstat[i] = (zbx_uint64_t)__UINT64_C(0);

	if (NULL == si)
	{
		si = (struct statinfo *)zbx_malloc(si, sizeof(struct statinfo));
		si->dinfo = (struct devinfo *)zbx_malloc(NULL, sizeof(struct devinfo));
		memset(si->dinfo, 0, sizeof(struct devinfo));
	}

	pd = devname;

	/* skip prefix TRX_DEV_PFX, if present */
	if ('\0' != *devname && 0 == strncmp(pd, TRX_DEV_PFX, TRX_CONST_STRLEN(TRX_DEV_PFX)))
			pd += TRX_CONST_STRLEN(TRX_DEV_PFX);

#if DEVSTAT_USER_API_VER >= 5
	if (-1 == devstat_getdevs(NULL, si))
#else
	if (-1 == getdevs(si))
#endif
		return FAIL;

	for (i = 0; i < si->dinfo->numdevs; i++)
	{
		ds = &si->dinfo->devices[i];

		/* empty '*devname' string means adding statistics for all disks together */
		if ('\0' != *devname)
		{
			zbx_snprintf(dev, sizeof(dev), "%s%d", ds->device_name, ds->unit_number);
			if (0 != strcmp(dev, pd))
				continue;
		}

#if DEVSTAT_USER_API_VER >= 5
		dstat[TRX_DSTAT_R_OPER] += (zbx_uint64_t)ds->operations[DEVSTAT_READ];
		dstat[TRX_DSTAT_W_OPER] += (zbx_uint64_t)ds->operations[DEVSTAT_WRITE];
		dstat[TRX_DSTAT_R_BYTE] += (zbx_uint64_t)ds->bytes[DEVSTAT_READ];
		dstat[TRX_DSTAT_W_BYTE] += (zbx_uint64_t)ds->bytes[DEVSTAT_WRITE];
#else
		dstat[TRX_DSTAT_R_OPER] += (zbx_uint64_t)ds->num_reads;
		dstat[TRX_DSTAT_W_OPER] += (zbx_uint64_t)ds->num_writes;
		dstat[TRX_DSTAT_R_BYTE] += (zbx_uint64_t)ds->bytes_read;
		dstat[TRX_DSTAT_W_BYTE] += (zbx_uint64_t)ds->bytes_written;
#endif
		ret = SUCCEED;

		if ('\0' != *devname)
			break;
	}

	return ret;
}

static int	vfs_dev_rw(AGENT_REQUEST *request, AGENT_RESULT *result, int rw)
{
	TRX_SINGLE_DISKDEVICE_DATA *device;
	char		devname[32], *tmp;
	int		type, mode;
	zbx_uint64_t	dstats[TRX_DSTAT_MAX];
	char		*pd;			/* pointer to device name without '/dev/' prefix, e.g. 'da0' */

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 0);

	if (NULL == tmp || 0 == strcmp(tmp, "all"))
		*devname = '\0';
	else
		strscpy(devname, tmp);

	pd = devname;

	if ('\0' != *pd)
	{
		/* skip prefix TRX_DEV_PFX, if present */
		if (0 == strncmp(pd, TRX_DEV_PFX, TRX_CONST_STRLEN(TRX_DEV_PFX)))
			pd += TRX_CONST_STRLEN(TRX_DEV_PFX);
	}

	tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "bps"))	/* default parameter */
		type = TRX_DSTAT_TYPE_BPS;
	else if (0 == strcmp(tmp, "ops"))
		type = TRX_DSTAT_TYPE_OPS;
	else if (0 == strcmp(tmp, "bytes"))
		type = TRX_DSTAT_TYPE_BYTE;
	else if (0 == strcmp(tmp, "operations"))
		type = TRX_DSTAT_TYPE_OPER;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (type == TRX_DSTAT_TYPE_BYTE || type == TRX_DSTAT_TYPE_OPER)
	{
		if (2 < request->nparam)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
			return SYSINFO_RET_FAIL;
		}

		if (FAIL == get_diskstat(pd, dstats))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain disk information."));
			return SYSINFO_RET_FAIL;
		}

		if (TRX_DSTAT_TYPE_BYTE == type)
			SET_UI64_RESULT(result, dstats[(TRX_DEV_READ == rw ? TRX_DSTAT_R_BYTE : TRX_DSTAT_W_BYTE)]);
		else	/* TRX_DSTAT_TYPE_OPER */
			SET_UI64_RESULT(result, dstats[(TRX_DEV_READ == rw ? TRX_DSTAT_R_OPER : TRX_DSTAT_W_OPER)]);

		return SYSINFO_RET_OK;
	}

	tmp = get_rparam(request, 2);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))	/* default parameter */
		mode = TRX_AVG1;
	else if (0 == strcmp(tmp, "avg5"))
		mode = TRX_AVG5;
	else if (0 == strcmp(tmp, "avg15"))
		mode = TRX_AVG15;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == collector)
	{
		/* CPU statistics collector and (optionally) disk statistics collector is started only when Treegix */
		/* agentd is running as a daemon. When Treegix agent or agentd is started with "-p" or "-t" parameter */
		/* the collectors are not available and keys "vfs.dev.read", "vfs.dev.write" with some parameters */
		/* (e.g. sps, ops) are not supported. */

		SET_MSG_RESULT(result, zbx_strdup(NULL, "This item is available only in daemon mode when collectors are"
				" started."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (device = collector_diskdevice_get(pd)))
	{
		if (FAIL == get_diskstat(pd, dstats))	/* validate device name */
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain disk information."));
			return SYSINFO_RET_FAIL;
		}

		if (NULL == (device = collector_diskdevice_add(pd)))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot add disk device to agent collector."));
			return SYSINFO_RET_FAIL;
		}
	}

	if (TRX_DSTAT_TYPE_BPS == type)	/* default parameter */
		SET_DBL_RESULT(result, (TRX_DEV_READ == rw ? device->r_bps[mode] : device->w_bps[mode]));
	else if (TRX_DSTAT_TYPE_OPS == type)
		SET_DBL_RESULT(result, (TRX_DEV_READ == rw ? device->r_ops[mode] : device->w_ops[mode]));

	return SYSINFO_RET_OK;
}

int	VFS_DEV_READ(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return vfs_dev_rw(request, result, TRX_DEV_READ);
}

int	VFS_DEV_WRITE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return vfs_dev_rw(request, result, TRX_DEV_WRITE);
}
