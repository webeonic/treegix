

#include "common.h"
#include "sysinfo.h"
#include "log.h"

static int	vfs_fs_inode(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef HAVE_SYS_STATVFS_H
#	ifdef HAVE_SYS_STATVFS64
#		define ZBX_STATFS	statvfs64
#	else
#		define ZBX_STATFS	statvfs
#	endif
#	define ZBX_FFREE	f_favail
#else
#	ifdef HAVE_SYS_STATFS64
#		define ZBX_STATFS	statfs64
#	else
#		define ZBX_STATFS	statfs
#	endif
#	define ZBX_FFREE	f_ffree
#endif
	char			*fsname, *mode;
	zbx_uint64_t		total;
	struct ZBX_STATFS	s;

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

	if (0 != ZBX_STATFS(fsname, &s))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s",
				zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* default parameter */
	{
		SET_UI64_RESULT(result, s.f_files);
	}
	else if (0 == strcmp(mode, "free"))
	{
		SET_UI64_RESULT(result, s.ZBX_FFREE);
	}
	else if (0 == strcmp(mode, "used"))
	{
		SET_UI64_RESULT(result, s.f_files - s.f_ffree);
	}
	else if (0 == strcmp(mode, "pfree"))
	{
		total = s.f_files;
#ifdef HAVE_SYS_STATVFS_H
		total -= s.f_ffree - s.f_favail;
#endif
		if (0 != total)
			SET_DBL_RESULT(result, (double)(100.0 * s.ZBX_FFREE) / total);
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
			return SYSINFO_RET_FAIL;
		}
	}
	else if (0 == strcmp(mode, "pused"))
	{
		total = s.f_files;
#ifdef HAVE_SYS_STATVFS_H
		total -= s.f_ffree - s.f_favail;
#endif
		if (0 != total)
		{
			SET_DBL_RESULT(result, 100.0 - (double)(100.0 * s.ZBX_FFREE) / total);
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
			return SYSINFO_RET_FAIL;
		}
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
}
