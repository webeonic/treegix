

#include "common.h"
#include "sysinfo.h"
#include "log.h"

static int	get_fs_size_stat(const char *fs, zbx_uint64_t *total, zbx_uint64_t *free,
		zbx_uint64_t *used, double *pfree, double *pused, char **error)
{
#ifdef HAVE_SYS_STATVFS_H
#	define TRX_STATFS	statvfs
#	define TRX_BSIZE	f_frsize
#else
#	define TRX_STATFS	statfs
#	define TRX_BSIZE	f_bsize
#endif
	struct TRX_STATFS	s;

	if (0 != TRX_STATFS(fs, &s))
	{
		*error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s", zbx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	/* Available space could be negative (top bit set) if we hit disk space */
	/* reserved for non-privileged users. Treat it as 0.                    */
	if (0 != TRX_IS_TOP_BIT_SET(s.f_bavail))
		s.f_bavail = 0;

	if (NULL != total)
		*total = (zbx_uint64_t)s.f_blocks * s.TRX_BSIZE;

	if (NULL != free)
		*free = (zbx_uint64_t)s.f_bavail * s.TRX_BSIZE;

	if (NULL != used)
		*used = (zbx_uint64_t)(s.f_blocks - s.f_bfree) * s.TRX_BSIZE;

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
	zbx_uint64_t	value;
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
	zbx_uint64_t	value;
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
	zbx_uint64_t	value;
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
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	fsname = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (NULL == fsname || '\0' == *fsname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
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

	SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));

	return SYSINFO_RET_FAIL;
}

int	VFS_FS_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return zbx_execute_threaded_metric(vfs_fs_size, request, result);
}
