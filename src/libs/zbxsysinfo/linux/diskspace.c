

#include "common.h"
#include "sysinfo.h"
#include "trxjson.h"
#include "log.h"

static int	get_fs_size_stat(const char *fs, trx_uint64_t *total, trx_uint64_t *free,
		trx_uint64_t *used, double *pfree, double *pused, char **error)
{
#ifdef HAVE_SYS_STATVFS_H
#	define TRX_STATFS	statvfs
#	define TRX_BSIZE	f_frsize
#else
#	define TRX_STATFS	statfs
#	define TRX_BSIZE	f_bsize
#endif
	struct TRX_STATFS	s;

	if (NULL == fs || '\0' == *fs)
	{
		*error = trx_strdup(NULL, "Filesystem name cannot be empty.");
		return SYSINFO_RET_FAIL;
	}

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

static int	vfs_fs_size(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*fsname, *mode, *error;
	trx_uint64_t	total, free, used;
	double		pfree, pused;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	fsname = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (SYSINFO_RET_OK != get_fs_size_stat(fsname, &total, &free, &used, &pfree, &pused, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* default parameter */
		SET_UI64_RESULT(result, total);
	else if (0 == strcmp(mode, "free"))
		SET_UI64_RESULT(result, free);
	else if (0 == strcmp(mode, "used"))
		SET_UI64_RESULT(result, used);
	else if (0 == strcmp(mode, "pfree"))
		SET_DBL_RESULT(result, pfree);
	else if (0 == strcmp(mode, "pused"))
		SET_DBL_RESULT(result, pused);
	else
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int	VFS_FS_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return trx_execute_threaded_metric(vfs_fs_size, request, result);
}

int	VFS_FS_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		line[MAX_STRING_LEN], *p, *mpoint, *mtype;
	FILE		*f;
	struct trx_json	j;

	TRX_UNUSED(request);

	if (NULL == (f = fopen("/proc/mounts", "r")))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot open /proc/mounts: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	trx_json_initarray(&j, TRX_JSON_STAT_BUF_LEN);

	while (NULL != fgets(line, sizeof(line), f))
	{
		if (NULL == (p = strchr(line, ' ')))
			continue;

		mpoint = ++p;

		if (NULL == (p = strchr(mpoint, ' ')))
			continue;

		*p = '\0';

		mtype = ++p;

		if (NULL == (p = strchr(mtype, ' ')))
			continue;

		*p = '\0';

		trx_json_addobject(&j, NULL);
		trx_json_addstring(&j, "{#FSNAME}", mpoint, TRX_JSON_TYPE_STRING);
		trx_json_addstring(&j, "{#FSTYPE}", mtype, TRX_JSON_TYPE_STRING);
		trx_json_close(&j);
	}

	trx_fclose(f);

	trx_json_close(&j);

	SET_STR_RESULT(result, trx_strdup(NULL, j.buffer));

	trx_json_free(&j);

	return SYSINFO_RET_OK;
}
