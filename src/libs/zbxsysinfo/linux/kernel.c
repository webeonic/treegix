

#include "common.h"
#include "sysinfo.h"

static int	read_uint64_from_procfs(const char *path, zbx_uint64_t *value)
{
	int	ret = SYSINFO_RET_FAIL;
	char	line[MAX_STRING_LEN];
	FILE	*f;

	if (NULL != (f = fopen(path, "r")))
	{
		if (NULL != fgets(line, sizeof(line), f))
		{
			if (1 == sscanf(line, ZBX_FS_UI64 "\n", value))
				ret = SYSINFO_RET_OK;
		}
		zbx_fclose(f);
	}

	return ret;
}

int	KERNEL_MAXFILES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	zbx_uint64_t	value;

	ZBX_UNUSED(request);

	if (SYSINFO_RET_FAIL == read_uint64_from_procfs("/proc/sys/fs/file-max", &value))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain data from /proc/sys/fs/file-max."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);
	return SYSINFO_RET_OK;
}

int	KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	zbx_uint64_t	value;

	ZBX_UNUSED(request);

	if (SYSINFO_RET_FAIL == read_uint64_from_procfs("/proc/sys/kernel/pid_max", &value))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain data from /proc/sys/kernel/pid_max."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);
	return SYSINFO_RET_OK;
}
