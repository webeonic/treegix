

#include "common.h"
#include "sysinfo.h"
#include "stats.h"
#include "diskdevices.h"
#include "trxjson.h"

#define TRX_DEV_PFX		"/dev/"
#define TRX_DEV_READ		0
#define TRX_DEV_WRITE		1
#define TRX_SYS_BLKDEV_PFX	"/sys/dev/block/"

#if defined(KERNEL_2_4)
#	define INFO_FILE_NAME	"/proc/partitions"
#	define PARSE(line)	if (sscanf(line, TRX_FS_UI64 TRX_FS_UI64 " %*d %s " 		\
					TRX_FS_UI64 " %*d " TRX_FS_UI64 " %*d "			\
					TRX_FS_UI64 " %*d " TRX_FS_UI64 " %*d %*d %*d %*d",	\
				&rdev_major,							\
				&rdev_minor,							\
				name,								\
				&ds[TRX_DSTAT_R_OPER],						\
				&ds[TRX_DSTAT_R_SECT],						\
				&ds[TRX_DSTAT_W_OPER],						\
				&ds[TRX_DSTAT_W_SECT]						\
				) != 7) continue
#else
#	define INFO_FILE_NAME	"/proc/diskstats"
#	define PARSE(line)	if (sscanf(line, TRX_FS_UI64 TRX_FS_UI64 " %s "			\
					TRX_FS_UI64 " %*d " TRX_FS_UI64 " %*d "			\
					TRX_FS_UI64 " %*d " TRX_FS_UI64 " %*d %*d %*d %*d",	\
				&rdev_major,							\
				&rdev_minor,							\
				name,								\
				&ds[TRX_DSTAT_R_OPER],						\
				&ds[TRX_DSTAT_R_SECT],						\
				&ds[TRX_DSTAT_W_OPER],						\
				&ds[TRX_DSTAT_W_SECT]						\
				) != 7								\
				&&								\
				/* some disk partitions */					\
				sscanf(line, TRX_FS_UI64 TRX_FS_UI64 " %s "			\
					TRX_FS_UI64 TRX_FS_UI64					\
					TRX_FS_UI64 TRX_FS_UI64,				\
				&rdev_major,							\
				&rdev_minor,							\
				name,								\
				&ds[TRX_DSTAT_R_OPER],						\
				&ds[TRX_DSTAT_R_SECT],						\
				&ds[TRX_DSTAT_W_OPER],						\
				&ds[TRX_DSTAT_W_SECT]						\
				) != 7								\
				) continue
#endif

int	get_diskstat(const char *devname, trx_uint64_t *dstat)
{
	FILE		*f;
	char		tmp[MAX_STRING_LEN], name[MAX_STRING_LEN], dev_path[MAX_STRING_LEN];
	int		i, ret = FAIL, dev_exists = FAIL;
	trx_uint64_t	ds[TRX_DSTAT_MAX], rdev_major, rdev_minor;
	trx_stat_t 	dev_st;
	int		found = 0;

	for (i = 0; i < TRX_DSTAT_MAX; i++)
		dstat[i] = (trx_uint64_t)__UINT64_C(0);

	if (NULL != devname && '\0' != *devname && 0 != strcmp(devname, "all"))
	{
		*dev_path = '\0';
		if (0 != strncmp(devname, TRX_DEV_PFX, TRX_CONST_STRLEN(TRX_DEV_PFX)))
			strscpy(dev_path, TRX_DEV_PFX);
		strscat(dev_path, devname);

		if (trx_stat(dev_path, &dev_st) == 0)
			dev_exists = SUCCEED;
	}

	if (NULL == (f = fopen(INFO_FILE_NAME, "r")))
		return FAIL;

	while (NULL != fgets(tmp, sizeof(tmp), f))
	{
		PARSE(tmp);

		if (NULL != devname && '\0' != *devname && 0 != strcmp(devname, "all"))
		{
			if (0 != strcmp(name, devname))
			{
				if (SUCCEED != dev_exists
					|| major(dev_st.st_rdev) != rdev_major
					|| minor(dev_st.st_rdev) != rdev_minor)
					continue;
			}
			else
				found = 1;
		}

		dstat[TRX_DSTAT_R_OPER] += ds[TRX_DSTAT_R_OPER];
		dstat[TRX_DSTAT_R_SECT] += ds[TRX_DSTAT_R_SECT];
		dstat[TRX_DSTAT_W_OPER] += ds[TRX_DSTAT_W_OPER];
		dstat[TRX_DSTAT_W_SECT] += ds[TRX_DSTAT_W_SECT];

		ret = SUCCEED;

		if (1 == found)
			break;
	}
	trx_fclose(f);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Comments: Translate device name to the one used internally by kernel. The  *
 *           translation is done based on minor and major device numbers      *
 *           listed in INFO_FILE_NAME . If the names differ it is usually an  *
 *           LVM device which is listed in kernel device mapper.              *
 *                                                                            *
 ******************************************************************************/
static int	get_kernel_devname(const char *devname, char *kernel_devname, size_t max_kernel_devname_len)
{
	FILE		*f;
	char		tmp[MAX_STRING_LEN], name[MAX_STRING_LEN], dev_path[MAX_STRING_LEN];
	int		ret = FAIL;
	trx_uint64_t	ds[TRX_DSTAT_MAX], rdev_major, rdev_minor;
	trx_stat_t	dev_st;

	if ('\0' == *devname)
		return ret;

	*dev_path = '\0';
	if (0 != strncmp(devname, TRX_DEV_PFX, TRX_CONST_STRLEN(TRX_DEV_PFX)))
		strscpy(dev_path, TRX_DEV_PFX);
	strscat(dev_path, devname);

	if (trx_stat(dev_path, &dev_st) < 0 || NULL == (f = fopen(INFO_FILE_NAME, "r")))
		return ret;

	while (NULL != fgets(tmp, sizeof(tmp), f))
	{
		PARSE(tmp);
		if (major(dev_st.st_rdev) != rdev_major || minor(dev_st.st_rdev) != rdev_minor)
			continue;

		trx_strlcpy(kernel_devname, name, max_kernel_devname_len);
		ret = SUCCEED;
		break;
	}
	trx_fclose(f);

	return ret;
}

static int	vfs_dev_rw(AGENT_REQUEST *request, AGENT_RESULT *result, int rw)
{
	TRX_SINGLE_DISKDEVICE_DATA	*device;
	char				*devname, *tmp, kernel_devname[MAX_STRING_LEN];
	int				type, mode;
	trx_uint64_t			dstats[TRX_DSTAT_MAX];

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	devname = get_rparam(request, 0);
	tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "sps"))	/* default parameter */
		type = TRX_DSTAT_TYPE_SPS;
	else if (0 == strcmp(tmp, "ops"))
		type = TRX_DSTAT_TYPE_OPS;
	else if (0 == strcmp(tmp, "sectors"))
		type = TRX_DSTAT_TYPE_SECT;
	else if (0 == strcmp(tmp, "operations"))
		type = TRX_DSTAT_TYPE_OPER;
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (type == TRX_DSTAT_TYPE_SECT || type == TRX_DSTAT_TYPE_OPER)
	{
		if (request->nparam > 2)
		{
			/* Mode is supported only if type is in: operations, sectors. */
			SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid number of parameters."));
			return SYSINFO_RET_FAIL;
		}

		if (SUCCEED != get_diskstat(devname, dstats))
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain disk information."));
			return SYSINFO_RET_FAIL;
		}

		if (TRX_DSTAT_TYPE_SECT == type)
			SET_UI64_RESULT(result, dstats[(TRX_DEV_READ == rw ? TRX_DSTAT_R_SECT : TRX_DSTAT_W_SECT)]);
		else
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
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == collector)
	{
		/* CPU statistics collector and (optionally) disk statistics collector is started only when Treegix */
		/* agentd is running as a daemon. When Treegix agent or agentd is started with "-p" or "-t" parameter */
		/* the collectors are not available and keys "vfs.dev.read", "vfs.dev.write" with some parameters */
		/* (e.g. sps, ops) are not supported. */

		SET_MSG_RESULT(result, trx_strdup(NULL, "This item is available only in daemon mode when collectors are"
				" started."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == devname || '\0' == *devname || 0 == strcmp(devname, "all"))
	{
		*kernel_devname = '\0';
	}
	else if (SUCCEED != get_kernel_devname(devname, kernel_devname, sizeof(kernel_devname)))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain device name used internally by the kernel."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (device = collector_diskdevice_get(kernel_devname)))
	{
		if (SUCCEED != get_diskstat(kernel_devname, dstats))
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain disk information."));

			return SYSINFO_RET_FAIL;
		}

		if (NULL == (device = collector_diskdevice_add(kernel_devname)))
		{
			SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot add disk device to agent collector."));
			return SYSINFO_RET_FAIL;
		}
	}

	if (TRX_DSTAT_TYPE_SPS == type)
		SET_DBL_RESULT(result, (TRX_DEV_READ == rw ? device->r_sps[mode] : device->w_sps[mode]));
	else
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

int	VFS_DEV_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#define DEVTYPE_STR	"DEVTYPE="
#define DEVTYPE_STR_LEN	TRX_CONST_STRLEN(DEVTYPE_STR)

	DIR		*dir;
	struct dirent	*entries;
	char		tmp[MAX_STRING_LEN];
	trx_stat_t	stat_buf;
	struct trx_json	j;
	int		devtype_found, sysfs_found;

	TRX_UNUSED(request);

	if (NULL != (dir = opendir(TRX_DEV_PFX)))
	{
		trx_json_initarray(&j, TRX_JSON_STAT_BUF_LEN);

		/* check if sys fs with block devices is available */
		if (0 == trx_stat(TRX_SYS_BLKDEV_PFX, &stat_buf) && 0 != S_ISDIR(stat_buf.st_mode))
			sysfs_found = 1;
		else
			sysfs_found = 0;

		while (NULL != (entries = readdir(dir)))
		{
			trx_snprintf(tmp, sizeof(tmp), TRX_DEV_PFX "%s", entries->d_name);

			if (0 == trx_stat(tmp, &stat_buf) && 0 != S_ISBLK(stat_buf.st_mode))
			{
				devtype_found = 0;
				trx_json_addobject(&j, NULL);
				trx_json_addstring(&j, "{#DEVNAME}", entries->d_name, TRX_JSON_TYPE_STRING);

				if (1 == sysfs_found)
				{
					FILE	*f;

					trx_snprintf(tmp, sizeof(tmp), TRX_SYS_BLKDEV_PFX "%u:%u/uevent",
							major(stat_buf.st_rdev), minor(stat_buf.st_rdev));

					if (NULL != (f = fopen(tmp, "r")))
					{
						while (NULL != fgets(tmp, sizeof(tmp), f))
						{
							if (0 == strncmp(tmp, DEVTYPE_STR, DEVTYPE_STR_LEN))
							{
								char	*p;

								/* dismiss trailing \n */
								p = tmp + strlen(tmp) - 1;
								if ('\n' == *p)
									*p = '\0';

								devtype_found = 1;
								break;
							}
						}
						trx_fclose(f);
					}
				}

				trx_json_addstring(&j, "{#DEVTYPE}", 1 == devtype_found ? tmp + DEVTYPE_STR_LEN : "",
						TRX_JSON_TYPE_STRING);
				trx_json_close(&j);
			}
		}
		closedir(dir);
	}
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL,
				"Cannot obtain device list: failed to open " TRX_DEV_PFX " directory."));
		return SYSINFO_RET_FAIL;
	}

	SET_STR_RESULT(result, trx_strdup(NULL, j.buffer));
	trx_json_free(&j);

	return SYSINFO_RET_OK;

#undef DEVTYPE_STR
#undef DEVTYPE_STR_LEN
}
