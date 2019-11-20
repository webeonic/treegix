

#include "common.h"
#include "sysinfo.h"

static int	read_uint64_from_procfs(const char *path, trx_uint64_t *value)
{
	int	ret = SYSINFO_RET_FAIL;
	char	line[MAX_STRING_LEN];
	FILE	*f;

	if (NULL != (f = fopen(path, "r")))
	{
		if (NULL != fgets(line, sizeof(line), f))
		{
			if (1 == sscanf(line, TRX_FS_UI64 "\n", value))
				ret = SYSINFO_RET_OK;
		}
		trx_fclose(f);
	}

	return ret;
}

int	KERNEL_MAXFILES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	trx_uint64_t	value;

	TRX_UNUSED(request);

	if (SYSINFO_RET_FAIL == read_uint64_from_procfs("/proc/sys/fs/file-max", &value))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain data from /proc/sys/fs/file-max."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);
	return SYSINFO_RET_OK;
}

int	KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	trx_uint64_t	value;

	TRX_UNUSED(request);

	if (SYSINFO_RET_FAIL == read_uint64_from_procfs("/proc/sys/kernel/pid_max", &value))
	{
		SET_MSG_RESULT(result, trx_strdup(NULL, "Cannot obtain data from /proc/sys/kernel/pid_max."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);
	return SYSINFO_RET_OK;
}
