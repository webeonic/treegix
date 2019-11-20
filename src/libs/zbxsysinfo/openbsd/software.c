

#include "sysinfo.h"
#include "log.h"

#ifdef HAVE_SYS_UTSNAME_H
#       include <sys/utsname.h>
#endif

int	SYSTEM_SW_ARCH(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct utsname	name;

	if (-1 == uname(&name))
	{
		SET_MSG_RESULT(result, trx_dsprintf(NULL, "Cannot obtain system information: %s", trx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_STR_RESULT(result, trx_strdup(NULL, name.machine));

	return SYSINFO_RET_OK;
}
