

#include "common.h"
#include "sysinfo.h"
#include "trxjson.h"
#include "log.h"

static int	get_fs_size_stat(const char *fs, trx_uint64_t *total, trx_uint64_t *free,
		trx_uint64_t *used, double *pfree, double *pused, char **error)
{
#ifdef HAVE_SYS_STATVFS_H
#	ifdef HAVE_SYS_STATVFS64
#		define TRX_STATFS	statvfs64
#	else
#		define TRX_STATFS	statvfs
#	endif
#	define TRX_BSIZE	f_frsize
#else
#	define TRX_STATFS	statfs
#	define TRX_BSIZE	f_bsize
#endif
	struct TRX_STATFS	s;

	if (0 != TRX_STATFS(fs, &s))
	{
		*error = trx_dsprintf(NULL, "Cannot obtain filesystem information: %s", trx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	/* Available space could be negative (top bit set) if we hit disk space */
	/* reserved for non-privileged users. Treat it as 0.                    */
	if (0 != TRX_IS_TOP_BIT_SET(s.f_bavail))
		s.f_bavail = 0;

	if (NULL != total)
		*total = (trx_uint64_t)s.f_blocks * s.TRX_BSIZE;

	if (NULL != free)
		*free = (trx_uint64_t)s.f_bavail * s.TRX_BSIZE;

	if (NULL != used)
		*used = (trx_uint64_t)(s.f_blocks - s.f_bfree) * s.TRX_BSIZE;

	if (NULL != pfree)
	{
		if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
			*pfree = (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
		else
			*pfree = 0;
	}

	if (NULL != pused)
	{
		if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
			*pused = 100.0 - (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
		else
			*pused = 0;
	}

	return SYSINFO_RET_OK;
}

static int	VFS_FS_USED(const char *fs, AGENT_RESULT *result)
{
	trx_uint64_t	value;
	char		*error;

	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, &value, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	VFS_FS_FREE(const char *fs, AGENT_RESULT *result)
{
	trx_uint64_t	value;
	char		*error;

	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, &value, NULL, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	VFS_FS_TOTAL(const char *fs, AGENT_RESULT *result)
{
	trx_uint64_t	value;
	char		*error;

	if (SYSINFO_RET_OK != get_fs_size_stat(fs, &value, NULL, NULL, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	VFS_FS_PFREE(const char *fs, AGENT_RESULT *result)
{
	double	value;
	char	*error;

	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, NULL, &value, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	VFS_FS_PUSED(const char *fs, AGENT_RESULT *result)
{
	double	value;
	char	*error;

	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, NULL, NULL, &value, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	vfs_fs_size(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*fsname, *mode;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	fsname = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (NULL == fsname || '\0' == *fsname)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* default parameter */
		return VFS_FS_TOTAL(fsname, result);
	if (0 == strcmp(mode, "free"))
		return VFS_FS_FREE(fsname, result);
	if (0 == strcmp(mode, "pfree"))
		return VFS_FS_PFREE(fsname, result);
	if (0 == strcmp(mode, "used"))
		return VFS_FS_USED(fsname, result);
	if (0 == strcmp(mode, "pused"))
		return VFS_FS_PUSED(fsname, result);

	SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));

	return SYSINFO_RET_FAIL;
}

int	VFS_FS_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return trx_execute_threaded_metric(vfs_fs_size, request, result);
}

int	VFS_FS_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct mnttab	mt;
	FILE		*f;
	struct trx_json	j;

	/* opening the mounted filesystems file */
	if (NULL == (f = fopen("/etc/mnttab", "r")))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open /etc/mnttab: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	trx_json_initarray(&j, TRX_JSON_STAT_BUF_LEN);

	/* fill mnttab structure from file */
	while (-1 != getmntent(f, &mt))
	{
		trx_json_addobject(&j, NULL);
		trx_json_addstring(&j, "{#FSNAME}", mt.mnt_mountp, TRX_JSON_TYPE_STRING);
		trx_json_addstring(&j, "{#FSTYPE}", mt.mnt_fstype, TRX_JSON_TYPE_STRING);
		trx_json_close(&j);
	}

	trx_fclose(f);

	trx_json_close(&j);

	SET_STR_RESULT(result, trx_strdup(NULL, j.buffer));

	trx_json_free(&j);

	return SYSINFO_RET_OK;
}
