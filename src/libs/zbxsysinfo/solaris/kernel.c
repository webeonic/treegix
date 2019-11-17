

#include "common.h"
#include "sysinfo.h"
#include "log.h"

int	KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int		ret = SYSINFO_RET_FAIL;
	kstat_ctl_t	*kc;
	kstat_t		*kt;
	struct var	*v;

	if (NULL == (kc = kstat_open()))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open kernel statistics facility: %s",
				zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (kt = kstat_lookup(kc, "unix", 0, "var")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot look up in kernel statistics facility: %s",
				zbx_strerror(errno)));
		goto clean;
	}

	if (KSTAT_TYPE_RAW != kt->ks_type)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Information looked up in kernel statistics facility"
				" is of the wrong type."));
		goto clean;
	}

	if (-1 == kstat_read(kc, kt, NULL))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot read from kernel statistics facility: %s",
				zbx_strerror(errno)));
		goto clean;
	}

	v = (struct var *)kt->ks_data;

	/* int	v_proc;	    Max processes system wide */
	SET_UI64_RESULT(result, v->v_proc);
	ret = SYSINFO_RET_OK;
clean:
	kstat_close(kc);

	return ret;
}
